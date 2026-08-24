/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2014-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "kernel_injector.h"
#include "kernel_mem.h"
#include "driver_loader.h"
#include "driver_resources.h"
#include "trace_cleanup.h"
#include "manual_map.h"

#include "../../../renderdocshim/renderdocshim.h"

#if defined(Q_OS_WIN)

#include <windows.h>
#include <QDebug>
#include <QRandomGenerator>
#include <QThread>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>

#include <memory>
#include <atomic>

namespace KernelInjector
{
namespace
{
// Matches RDI_IOCTL_INJECT in kernel_driver/driver.h.
constexpr uint32_t kInjectIoctl = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED,
                                           FILE_ANY_ACCESS);

// Serialises Capture/Shutdown/IsActive: the driver chain is session state and
// must not be torn down while a capture is in flight (closeEvent can run on
// the UI thread while a capture worker is still busy). Capture holds the lock
// for its whole duration, which blocks Shutdown until the pipeline finishes.
QMutex g_chainMutex;

// Session state: the vulnerable driver and the mapped injection driver stay
// loaded so repeated captures are fast. Only touched under g_chainMutex.
LoadedDriver g_loadedDriver;
std::unique_ptr<KernelMem> g_backend;
void *g_injectorDevice = nullptr;
BackendId g_activeBackend = BackendId::Portwell;
bool g_chainUp = false;

// One-time secret for the injection device's caller authentication (see
// kernel_driver/driver.h). Generated when the chain comes up; every inject
// request carries it.
uint64_t g_deviceSecret = 0;

// Set by Shutdown() before it blocks on g_chainMutex so an in-flight Capture
// can abandon its status poll instead of making closeEvent stall on the UI
// thread for the whole timeout. Only the polling phase is interruptible - a
// capture that is still bringing up the chain (driver load + physical scan)
// has to finish that first.
std::atomic<bool> g_cancelRequested{false};

const int kStatusTimeoutMs = 30000;
const int kStatusPollMs = 100;

// The shim reports completion by writing status + the target control ident
// back into the shared mapping. Status 2/3 are intermediate failure reports
// that other processes (e.g. under the global hook) can also write, so the
// coordinator keeps polling through them and only acts at timeout.
const uint32_t kShimStatusDone = 1;
const uint32_t kShimStatusMismatch = 2;
const uint32_t kShimStatusSetupFailed = 3;

bool IsHVCIEnabled()
{
  // Hypervisor-Enforced Code Integrity (memory integrity). Manual mapping of
  // executable kernel pages BSODs under HVCI, so the feature must refuse.
  // Enabled: 0 = off, 1 = on (UEFI locked), 2 = on (not locked) - any non-zero
  // value means the feature is active.
  HKEY key = nullptr;
  LONG status = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\"
                              L"HypervisorEnforcedCodeIntegrity",
                              0, KEY_READ, &key);
  if(status != ERROR_SUCCESS)
    return false;

  DWORD enabled = 0;
  DWORD size = sizeof(enabled);
  status = RegQueryValueExW(key, L"Enabled", nullptr, nullptr, (LPBYTE)&enabled, &size);
  RegCloseKey(key);

  return status == ERROR_SUCCESS && enabled != 0;
}

bool CreateShimDataMapping(const KernelInjectorCore::CaptureRequest &req, HANDLE *outMapping,
                           ShimData **outView)
{
  HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      sizeof(ShimData), GLOBAL_HOOK_DATA_NAME);
  if(mapping == nullptr)
    return false;

  // A mapping with this name already exists: another kernel capture (or the
  // global hook flow) is active. Refuse rather than clobbering it.
  if(GetLastError() == ERROR_ALREADY_EXISTS)
  {
    CloseHandle(mapping);
    return false;
  }

  ShimData *view = (ShimData *)MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
                                             sizeof(ShimData));
  if(view == nullptr)
  {
    CloseHandle(mapping);
    return false;
  }

  memset(view, 0, sizeof(ShimData));

  wcsncpy_s(view->pathmatchstring, req.exeName.toStdWString().c_str(), _TRUNCATE);
  wcsncpy_s(view->rdocpath, req.renderdocPath.toStdWString().c_str(), _TRUNCATE);
  strncpy_s(view->capfile, req.captureFile.toUtf8().constData(), _TRUNCATE);

  if(sizeof(CaptureOptions) <= sizeof(view->opts))
    memcpy(view->opts, &req.opts, sizeof(CaptureOptions));

  view->status = 0;
  view->ident = 0;

  *outMapping = mapping;
  *outView = view;
  return true;
}

bool SendInjectRequest(uint32_t pid, const QString &shimPath)
{
  if(g_injectorDevice == nullptr)
    return false;

  // Must match RDI_INJECT_REQUEST in kernel_driver/driver.h.
  struct InjectRequest
  {
    uint32_t processId;
    wchar_t dllPath[1024];
    uint64_t secret;
  } req = {};

  req.processId = pid;
  req.secret = g_deviceSecret;

  std::wstring path = shimPath.toStdWString();
  if(path.size() >= 1024)
    return false;
  wcscpy_s(req.dllPath, path.c_str());

  DWORD returned = 0;
  return DeviceIoControl((HANDLE)g_injectorDevice, kInjectIoctl, &req, sizeof(req), nullptr, 0,
                         &returned, nullptr) != FALSE;
}

bool EnsureChainUp(BackendId backend, QString *error)
{
  if(g_chainUp)
  {
    if(backend != g_activeBackend)
    {
      qWarning() << "KernelInjector: driver chain already up with backend"
                 << (int)g_activeBackend << "- request for backend" << (int)backend
                 << "is ignored (restart to switch)";
    }
    return true;
  }

  if(IsHVCIEnabled())
  {
    *error = QStringLiteral(
        "Windows Memory Integrity (HVCI) is enabled. Manual mapping of the injection driver "
        "requires it to be off - disable it under Windows Security -> Device Security and "
        "reboot before using kernel capture.");
    return false;
  }

  // 0. The mapped injection driver can't be unmapped, so its device object
  // survives a previous session until the next reboot. Check the \DosDevices
  // symlink instead of opening the device: opening would dispatch IRPs into
  // a driver that may be stale (or dangling after a failed map), while the
  // symlink lookup only touches the object manager namespace.
  {
    wchar_t dosTarget[512];
    if(QueryDosDeviceW(L"RenderDicInj", dosTarget, (DWORD)ARRAYSIZE(dosTarget)) != 0)
    {
      *error = QStringLiteral(
          "The kernel injection device \\\\.\\RenderDicInj already exists - a previous "
          "session left the injection driver mapped in memory. Restart the machine and try again.");
      return false;
    }
  }

  // 1. Load the vulnerable driver.
  LoadedDriver loaded;
  QString loadError;
  if(!DriverLoader::Load(backend, &loaded, &loadError))
  {
    *error = loadError;
    return false;
  }

  // 2. Backend over the driver device.
  std::unique_ptr<KernelMem> mem;
  if(backend == BackendId::Portwell)
    mem.reset(new PortwellBackend(loaded.device));
  else
    mem.reset(new TbtBackend(loaded.device));

  if(!mem->FindNtoskrnlBase())
  {
    *error = QStringLiteral("Failed to resolve ntoskrnl base");
    DriverLoader::Unload(loaded);
    return false;
  }

  // 3. Trace cleanup for the vulnerable driver itself.
  const uint32_t stamp = DriverLoader::DriverTimeDateStamp(backend);
  if(!TraceCleanup::CleanupAll(mem.get(), loaded.device, loaded.serviceName, loaded.tempPath, stamp))
  {
    qWarning() << "KernelInjector: trace cleanup reported failures (continuing anyway)";
  }

  // 4. Manual map the injection driver.
  void *injectorDevice = nullptr;
  QString mapError;
  if(!ManualMapper::MapDriver(mem.get(), injector_driver_bytes, injector_driver_bytes_size,
                              &injectorDevice, &mapError))
  {
    *error = mapError;
    DriverLoader::Unload(loaded);
    return false;
  }

  g_loadedDriver = loaded;
  g_backend = std::move(mem);
  g_injectorDevice = injectorDevice;
  g_activeBackend = backend;
  g_deviceSecret = QRandomGenerator::system()->generate64();
  g_chainUp = true;
  return true;
}
}    // namespace

bool KernelInjectorCore::Capture(const CaptureRequest &req, uint32_t *outIdent, QString *error)
{
  if(outIdent == nullptr || error == nullptr)
    return false;

  *error = QString();

  // Held for the whole pipeline: protects the chain from concurrent Shutdown
  // and from a second concurrent Capture.
  QMutexLocker lock(&g_chainMutex);

  // Fresh capture: clear any cancel flag a previous Shutdown left behind.
  g_cancelRequested = false;

  if(!EnsureChainUp(req.backend, error))
    return false;

  if(req.exeName.isEmpty() || req.shimPath.isEmpty() || req.renderdocPath.isEmpty())
  {
    *error = QStringLiteral("Invalid capture request (missing exe/shim/renderdoc paths)");
    return false;
  }

  // 5. ShimData mapping for the shim.
  HANDLE mapping = nullptr;
  ShimData *view = nullptr;
  if(!CreateShimDataMapping(req, &mapping, &view))
  {
    *error = QStringLiteral(
        "Could not create the shim data mapping - another kernel capture or the global hook "
        "may be active");
    return false;
  }

  // 6. Inject the shim into the target.
  if(!SendInjectRequest(req.pid, req.shimPath))
  {
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    *error = QStringLiteral("The injection driver rejected the inject request");
    return false;
  }

  // 7. Wait for the shim to load renderdoc.dll and complete the setup. Status
  // 2/3 are also written by unrelated processes when the global hook is
  // active, so only status 1 terminates the wait; intermediate codes are
  // remembered for the timeout message.
  QElapsedTimer timer;
  timer.start();

  uint32_t shimStatus = 0;
  uint32_t lastStatus = 0;
  bool cancelled = false;
  while(timer.elapsed() < kStatusTimeoutMs)
  {
    if(g_cancelRequested.load())
    {
      cancelled = true;
      break;
    }

    // The shim writes through the same mapping.
    shimStatus = view->status;
    if(shimStatus == kShimStatusDone)
      break;
    if(shimStatus != 0)
      lastStatus = shimStatus;
    QThread::msleep(kStatusPollMs);
  }

  if(shimStatus != kShimStatusDone)
  {
    UnmapViewOfFile(view);
    CloseHandle(mapping);

    if(cancelled)
      *error = QStringLiteral("Kernel capture cancelled - the application is shutting down");
    else if(lastStatus == kShimStatusMismatch)
      *error = QStringLiteral("The shim loaded but did not match the target executable - "
                              "check that the process still runs under the expected name");
    else if(lastStatus == kShimStatusSetupFailed)
      *error = QStringLiteral(
          "The shim loaded but the capture setup failed (renderdic.dll could not be loaded or "
          "its INTERNAL_* exports are missing)");
    else
      *error = QStringLiteral("No response from renderdicshim64.dll - the injection may have "
                              "been blocked or the target exited early");
    return false;
  }

  if(view->ident == 0)
  {
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    *error = QStringLiteral("The shim reported success but no target control ident - the "
                            "renderdic.dll in the target may be incompatible");
    return false;
  }

  *outIdent = view->ident;

  UnmapViewOfFile(view);
  CloseHandle(mapping);

  qInfo() << "KernelInjector: capture setup complete in target, ident" << *outIdent;
  return true;
}

bool KernelInjectorCore::IsActive()
{
  QMutexLocker lock(&g_chainMutex);
  return g_chainUp;
}

void KernelInjectorCore::Shutdown()
{
  // Ask an in-flight Capture to abandon its status poll before blocking on
  // the mutex it holds (see g_cancelRequested above).
  g_cancelRequested = true;

  QMutexLocker lock(&g_chainMutex);

  if(g_injectorDevice != nullptr)
  {
    CloseHandle((HANDLE)g_injectorDevice);
    g_injectorDevice = nullptr;
  }

  g_backend.reset();

  if(g_loadedDriver.device != nullptr)
  {
    DriverLoader::Unload(g_loadedDriver);
    g_loadedDriver = {};
  }

  g_deviceSecret = 0;
  g_chainUp = false;
}
}    // namespace KernelInjector

#endif    // Q_OS_WIN
