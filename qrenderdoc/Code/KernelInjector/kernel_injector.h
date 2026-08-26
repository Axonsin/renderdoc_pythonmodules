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

#pragma once

#include <cstdint>
#include <QString>

#include "renderdoc_replay.h"
#include "kernel_mem.h"
#include "driver_loader.h"

#if defined(Q_OS_WIN)

namespace KernelInjector
{
// Orchestrates one kernel-mode capture. The pipeline mirrors RenderPro's:
//
//   1. environment checks (admin, HVCI off)
//   2. load the vulnerable driver as a service (DriverLoader)
//   3. verify the R/W backend contract (SelfTest)
//   4. kdmapper-style trace cleanup (PiDDB / CI hash / MmUnloaded / WdFilter)
//   5. manual-map the injection driver (ManualMapper) - never in module lists
//   6. create the ShimData mapping the shim will read (capture options etc.)
//   7. queue LoadLibraryW(shim) in the target via the injection driver
//   8. wait for the shim to report completion through ShimData.status
//
// The loaded drivers stay resident for the session so repeated captures don't
// redo the whole chain; Shutdown() unloads everything (called from
// MainWindow::closeEvent).
class KernelInjectorCore
{
public:
  struct CaptureRequest
  {
    BackendId backend = BackendId::Portwell;
    uint32_t pid = 0;              // target process
    bool targetIsWow64 = false;    // 32-bit target: inject the 32-bit shim and use the
                                   // 32-bit mapping name (RenderDicGlobalHookData32)
    QString exeName;               // target exe file name (shim path match)
    QString shimPath;              // full path to renderdicshim64/32.dll
    QString renderdocPath;         // full path to renderdic.dll (matching the target bitness)
    QString captureFile;           // .rdc path the target will write
    CaptureOptions opts;           // capture options for the target
  };

  // Runs the full pipeline. Call from a worker thread; can take several
  // seconds on first use (driver load + PML4 scan). On success *outIdent is
  // the target control ident for LiveCapture. Returns false with a readable
  // error otherwise. Never partially loads: failures clean up after
  // themselves (except the manual-mapped driver, which cannot be unmapped
  // safely - see manual_map.h).
  static bool Capture(const CaptureRequest &req, uint32_t *outIdent, QString *error);

  // Unloads the vulnerable driver and closes all handles. Safe to call even
  // if Capture never ran.
  static void Shutdown();

  // True when the driver chain has been brought up in this session.
  static bool IsActive();
};
}    // namespace KernelInjector

#endif    // Q_OS_WIN
