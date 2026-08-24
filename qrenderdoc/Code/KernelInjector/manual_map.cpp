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

#include "manual_map.h"
#include "kernel_mem.h"
#include "kernel_scan.h"
#include "nt_internals.h"

#if defined(Q_OS_WIN)

#include <windows.h>
#include <winternl.h>
#include <QDebug>

#include <cstring>
#include <string>
#include <vector>

namespace KernelInjector
{
namespace
{
// ---------------------------------------------------------------------------
// Local-image PE helpers (ported from kdmapper's portable_executable.cpp)
// ---------------------------------------------------------------------------

PIMAGE_NT_HEADERS64 GetNtHeaders(void *imageBase)
{
  const auto *dos = (const IMAGE_DOS_HEADER *)imageBase;
  if(dos->e_magic != IMAGE_DOS_SIGNATURE)
    return nullptr;

  auto *nt = (PIMAGE_NT_HEADERS64)((uintptr_t)imageBase + dos->e_lfanew);
  if(nt->Signature != IMAGE_NT_SIGNATURE)
    return nullptr;

  return nt;
}

struct RelocInfo
{
  uint64_t address = 0;
  uint16_t *item = nullptr;
  size_t count = 0;
};

std::vector<RelocInfo> GetRelocs(void *imageBase)
{
  PIMAGE_NT_HEADERS64 nt = GetNtHeaders(imageBase);
  if(nt == nullptr)
    return {};

  const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
  if(dir.VirtualAddress == 0 || dir.Size == 0)
    return {};

  std::vector<RelocInfo> relocs;

  auto *current = (PIMAGE_BASE_RELOCATION)((uintptr_t)imageBase + dir.VirtualAddress);
  const auto *end = (PIMAGE_BASE_RELOCATION)((uintptr_t)current + dir.Size);

  while(current < end && current->SizeOfBlock != 0)
  {
    RelocInfo info;
    info.address = (uintptr_t)imageBase + current->VirtualAddress;
    info.item = (uint16_t *)((uintptr_t)current + sizeof(IMAGE_BASE_RELOCATION));
    info.count = (current->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);

    relocs.push_back(info);

    current = (PIMAGE_BASE_RELOCATION)((uintptr_t)current + current->SizeOfBlock);
  }

  return relocs;
}

struct ImportFunctionInfo
{
  const char *name = nullptr;
  uint64_t *address = nullptr;    // IAT slot in the local image
};

struct ImportInfo
{
  std::string moduleName;
  std::vector<ImportFunctionInfo> functions;
};

std::vector<ImportInfo> GetImports(void *imageBase)
{
  PIMAGE_NT_HEADERS64 nt = GetNtHeaders(imageBase);
  if(nt == nullptr)
    return {};

  const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if(dir.VirtualAddress == 0)
    return {};

  std::vector<ImportInfo> imports;

  auto *desc = (PIMAGE_IMPORT_DESCRIPTOR)((uintptr_t)imageBase + dir.VirtualAddress);

  while(desc->FirstThunk != 0)
  {
    ImportInfo info;
    info.moduleName = (const char *)((uintptr_t)imageBase + desc->Name);

    auto *firstThunk = (PIMAGE_THUNK_DATA64)((uintptr_t)imageBase + desc->FirstThunk);
    // Some linkers omit the lookup table; then the first thunk doubles as the
    // name table.
    auto *origThunk = (PIMAGE_THUNK_DATA64)((uintptr_t)imageBase +
                                            (desc->OriginalFirstThunk ? desc->OriginalFirstThunk
                                                                      : desc->FirstThunk));

    while(origThunk->u1.Function != 0)
    {
      auto *byName = (PIMAGE_IMPORT_BY_NAME)((uintptr_t)imageBase + origThunk->u1.AddressOfData);

      ImportFunctionInfo fn;
      fn.name = byName->Name;
      fn.address = &firstThunk->u1.Function;

      info.functions.push_back(fn);

      ++origThunk;
      ++firstThunk;
    }

    imports.push_back(info);
    ++desc;
  }

  return imports;
}

void RelocateImageByDelta(const std::vector<RelocInfo> &relocs, uint64_t delta)
{
  for(const RelocInfo &reloc : relocs)
  {
    for(size_t i = 0; i < reloc.count; i++)
    {
      const uint16_t type = reloc.item[i] >> 12;
      const uint16_t offset = reloc.item[i] & 0xFFF;

      if(type == IMAGE_REL_BASED_DIR64)
        *(uint64_t *)(reloc.address + offset) += delta;
    }
  }
}

// Fixes the __security_cookie if the driver uses /GS (kdmapper's FixSecurityCookie).
bool FixSecurityCookie(void *localImage, uint64_t kernelImageBase)
{
  PIMAGE_NT_HEADERS64 nt = GetNtHeaders(localImage);
  if(nt == nullptr)
    return false;

  const IMAGE_DATA_DIRECTORY &loadConfig =
      nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
  if(loadConfig.VirtualAddress == 0)
    return true;    // no /GS cookie

  auto *cfg = (PIMAGE_LOAD_CONFIG_DIRECTORY)((uintptr_t)localImage + loadConfig.VirtualAddress);
  uint64_t cookie = (uint64_t)cfg->SecurityCookie;
  if(cookie == 0)
    return true;

  cookie = cookie - kernelImageBase + (uintptr_t)localImage;

  if(*(uint64_t *)cookie != 0x2B992DDFA232ULL)
  {
    qWarning() << "KernelInjector: security cookie already fixed, aborting";
    return false;
  }

  uint64_t newCookie = 0x2B992DDFA232ULL ^ GetCurrentProcessId() ^ GetCurrentThreadId();
  if(newCookie == 0x2B992DDFA232ULL)
    newCookie = 0x2B992DDFA233ULL;

  *(uint64_t *)cookie = newCookie;
  return true;
}

// ---------------------------------------------------------------------------
// Kernel helpers resolved by pattern scan (kdmapper's patterns, tested from
// 1803 to 24H2)
// ---------------------------------------------------------------------------

uint64_t FindMmAllocateIndependentPagesEx(KernelMem *mem)
{
  using namespace KernelScan;

  // KeAllocateInterrupt -> 41 8B D6 B9 00 10 00 00 E8 ?? ?? ?? ?? 48 8B D8
  static const uint8_t pattern[] = {0x41, 0x8B, 0xD6, 0xB9, 0x00, 0x10, 0x00, 0x00,
                                    0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD8};
  static const char mask[] = "xxxxxxxxx????xxx";

  uint64_t found = FindPatternInSectionAtKernel(mem, ".text", mem->NtoskrnlBase(), pattern, mask);
  if(found == 0)
    return 0;

  found += 8;
  return ResolveRelativeAddress(mem, found, 1, 5);
}

uint64_t FindMmFreeIndependentPages(KernelMem *mem)
{
  using namespace KernelScan;

  static const uint8_t pattern1[] = {0xBA, 0x00, 0x60, 0x00, 0x00, 0x48, 0x8B, 0xCB,
                                     0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8B,
                                     0x00, 0xF0, 0xFF, 0xFF};
  static const char mask1[] = "xxxxxxxxx????xxxxxxx";

  uint64_t found = FindPatternInSectionAtKernel(mem, "PAGE", mem->NtoskrnlBase(), pattern1, mask1);
  if(found != 0)
  {
    found += 8;
    return ResolveRelativeAddress(mem, found, 1, 5);
  }

  // Windows 11 variant
  static const uint8_t pattern2[] = {0x8B, 0x15, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B,
                                     0xCB, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x8B};
  static const char mask2[] = "xx????xxxx????xxx";

  found = FindPatternInSectionAtKernel(mem, "PAGE", mem->NtoskrnlBase(), pattern2, mask2);
  if(found == 0)
    return 0;

  found += 9;
  return ResolveRelativeAddress(mem, found, 1, 5);
}

uint64_t FindMmSetPageProtection(KernelMem *mem)
{
  using namespace KernelScan;

  // 0F 45 ? ? 8D ? ? ? FF FF E8
  static const uint8_t pattern1[] = {0x0F, 0x45, 0x00, 0x00, 0x8D, 0x00, 0x00,
                                     0x00, 0xFF, 0xFF, 0xE8};
  static const char mask1[] = "xx??x???xxx";

  uint64_t found = FindPatternInSectionAtKernel(mem, "PAGELK", mem->NtoskrnlBase(), pattern1, mask1);
  if(found != 0)
  {
    found += 10;
    return ResolveRelativeAddress(mem, found, 1, 5);
  }

  // 0F 45 ? ? 45 8B ? ? ? ? 8D ? ? ? ? ? ? FF FF E8 (some builds have an extra instruction)
  static const uint8_t pattern2[] = {0x0F, 0x45, 0x00, 0x00, 0x45, 0x8B, 0x00, 0x00, 0x00, 0x00,
                                     0x8D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xE8};
  static const char mask2[] = "xx??xx????x???xxx";

  found = FindPatternInSectionAtKernel(mem, "PAGELK", mem->NtoskrnlBase(), pattern2, mask2);
  if(found == 0)
    return 0;

  found += 13;
  return ResolveRelativeAddress(mem, found, 1, 5);
}

// The device name the injection driver creates at DriverEntry.
const wchar_t kInjectorDevicePath[] = L"\\\\.\\RenderDicInj";
}    // namespace

bool ManualMapper::MapDriver(KernelMem *mem, const unsigned char *driverBytes, size_t size,
                             void **outDevice, QString *errorDetail)
{
  if(mem == nullptr || driverBytes == nullptr || outDevice == nullptr || errorDetail == nullptr)
    return false;

  *errorDetail = QString();
  *outDevice = nullptr;

  if(size < sizeof(IMAGE_DOS_HEADER))
  {
    *errorDetail = QStringLiteral("Injector driver image is empty - build kernel_driver/ and "
                                  "regenerate driver_resources.h");
    return false;
  }

  if(!mem->FindNtoskrnlBase())
  {
    *errorDetail = QStringLiteral("Failed to resolve ntoskrnl base");
    return false;
  }

  PIMAGE_NT_HEADERS64 nt = GetNtHeaders((void *)driverBytes);
  if(nt == nullptr)
  {
    *errorDetail = QStringLiteral("Invalid PE image for the injector driver");
    return false;
  }

  if(nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
  {
    *errorDetail = QStringLiteral("Injector driver is not a 64-bit image");
    return false;
  }

  const uint32_t imageSize = nt->OptionalHeader.SizeOfImage;

  // Local working copy of the image. Relocations and imports are fixed up in
  // this copy and the result is written to the kernel allocation.
  void *localImage = VirtualAlloc(nullptr, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if(localImage == nullptr)
  {
    *errorDetail = QStringLiteral("VirtualAlloc failed for the local image");
    return false;
  }

  // ------------------------------------------------------------------
  // Allocate executable kernel memory for the image
  // ------------------------------------------------------------------
  uint64_t allocPages = FindMmAllocateIndependentPagesEx(mem);
  uint64_t freePages = FindMmFreeIndependentPages(mem);
  uint64_t setProtection = FindMmSetPageProtection(mem);

  if(allocPages == 0 || setProtection == 0)
  {
    *errorDetail = QStringLiteral(
        "Failed to find MmAllocateIndependentPagesEx/MmSetPageProtection - "
        "unsupported Windows build (manual mapping needs executable kernel pages)");
    VirtualFree(localImage, 0, MEM_RELEASE);
    return false;
  }

  uint64_t kernelImageBase = 0;
  // kdmapper passes (size, -1, 0, 0). The full SizeOfImage is allocated and
  // written - the image layout in kernel memory is a straight copy of the
  // local one, so section addresses, the entry point and relocations all key
  // off the allocation base.
  if(!mem->CallKernelFunction(&kernelImageBase, allocPages, imageSize,
                              (uint64_t)-1, 0, 0) ||
     kernelImageBase == 0)
  {
    *errorDetail = QStringLiteral("Failed to allocate kernel pages for the injector driver");
    VirtualFree(localImage, 0, MEM_RELEASE);
    return false;
  }

  qInfo() << "KernelInjector: injector driver allocated at 0x"
          << QString::number(kernelImageBase, 16);

  // Once DriverEntry has been invoked the driver may have created its device
  // object and symlink, both pointing into this image - freeing the pages
  // would leave dangling kernel objects (any later open/IOCTL into the device
  // is a BSOD). After the entry point is attempted the pages stay mapped,
  // matching the "never unload it" policy of the success path.
  bool entryAttempted = false;

  auto failCleanup = [&](const QString &reason) {
    *errorDetail = reason;
    if(!entryAttempted && freePages != 0)
    {
      mem->CallKernelFunction<void>(nullptr, freePages, kernelImageBase, imageSize);
    }
    else if(entryAttempted)
    {
      *errorDetail += QStringLiteral(" (kernel pages intentionally left mapped - reboot the "
                                     "machine before retrying)");
    }
    VirtualFree(localImage, 0, MEM_RELEASE);
  };

  // ------------------------------------------------------------------
  // Build the fixed-up image in the local copy
  // ------------------------------------------------------------------
  memcpy(localImage, driverBytes, nt->OptionalHeader.SizeOfHeaders);

  const PIMAGE_SECTION_HEADER sections = IMAGE_FIRST_SECTION(nt);
  for(WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
  {
    if(sections[i].Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA)
      continue;

    void *dst = (void *)((uintptr_t)localImage + sections[i].VirtualAddress);
    memcpy(dst, driverBytes + sections[i].PointerToRawData, sections[i].SizeOfRawData);
  }

  RelocateImageByDelta(GetRelocs(localImage), kernelImageBase - nt->OptionalHeader.ImageBase);

  if(!FixSecurityCookie(localImage, kernelImageBase))
  {
    failCleanup(QStringLiteral("Failed to fix the security cookie"));
    return false;
  }

  // Resolve imports. The injector driver only imports from ntoskrnl.exe.
  for(const ImportInfo &import : GetImports(localImage))
  {
    uint64_t moduleBase = KernelScan::GetKernelModuleAddress(import.moduleName.c_str());
    if(moduleBase == 0)
    {
      failCleanup(QStringLiteral("Dependency %1 wasn't found").arg(
          QString::fromStdString(import.moduleName)));
      return false;
    }

    for(const ImportFunctionInfo &fn : import.functions)
    {
      uint64_t addr = mem->FindModuleExport(moduleBase, fn.name);
      if(addr == 0 && moduleBase != mem->NtoskrnlBase())
        addr = mem->FindExport(fn.name);

      if(addr == 0)
      {
        failCleanup(QStringLiteral("Failed to resolve import %1").arg(fn.name));
        return false;
      }

      *fn.address = addr;
    }
  }

  // ------------------------------------------------------------------
  // Write the image into the kernel allocation
  // ------------------------------------------------------------------
  if(!mem->WriteVirtual(kernelImageBase, localImage, imageSize))
  {
    failCleanup(QStringLiteral("Failed to write the injector driver image to kernel memory"));
    return false;
  }

  // Per-section page protection: code sections need execute permission.
  for(WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
  {
    const IMAGE_SECTION_HEADER *sec = &sections[i];
    const uint32_t secSize = sec->Misc.VirtualSize;
    if(secSize == 0)
      continue;

    ULONG prot = PAGE_READONLY;
    if(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)
      prot = (sec->Characteristics & IMAGE_SCN_MEM_WRITE) ? PAGE_EXECUTE_READWRITE
                                                          : PAGE_EXECUTE_READ;
    else if(sec->Characteristics & IMAGE_SCN_MEM_WRITE)
      prot = PAGE_READWRITE;

    BOOLEAN ok = FALSE;
    if(!mem->CallKernelFunction(&ok, setProtection, kernelImageBase + sec->VirtualAddress,
                                secSize, prot) ||
       !ok)
    {
      qWarning() << "KernelInjector: failed to set protection for section "
                 << QString::fromLatin1((const char *)sec->Name, 8);
    }
  }

  // ------------------------------------------------------------------
  // Invoke DriverEntry. The driver builds its own fake DRIVER_OBJECT when
  // given NULL, and creates \Device\RenderDicInj + \DosDevices\RenderDicInj.
  // ------------------------------------------------------------------
  const uint64_t entryPoint = kernelImageBase + nt->OptionalHeader.AddressOfEntryPoint;

  qInfo() << "KernelInjector: calling DriverEntry at 0x" << QString::number(entryPoint, 16);

  // Conservative: mark the entry as attempted before calling - even a failed
  // NtAddAtom trampoline restore may mean the entry point already ran.
  entryAttempted = true;

  NTSTATUS entryStatus = STATUS_UNSUCCESSFUL;
  if(!mem->CallKernelFunction(&entryStatus, entryPoint, 0, 0))
  {
    failCleanup(QStringLiteral("Failed to call the injector driver entry point"));
    return false;
  }

  if(!NT_SUCCESS(entryStatus))
  {
    failCleanup(QStringLiteral("Injector driver DriverEntry failed with 0x%1")
                    .arg((quint32)entryStatus, 8, 16, QLatin1Char('0')));
    return false;
  }

  qInfo() << "KernelInjector: injector driver DriverEntry returned 0x"
          << QString::number((quint32)entryStatus, 16);

  // Open the driver's control device. It is opened a few times in a row in
  // case the driver is not fully visible yet.
  void *device = nullptr;
  for(int attempt = 0; attempt < 50 && device == nullptr; attempt++)
  {
    HANDLE h = CreateFileW(kInjectorDevicePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(h != INVALID_HANDLE_VALUE)
      device = h;
    else
      Sleep(100);
  }

  if(device == nullptr)
  {
    failCleanup(QStringLiteral("Injector driver mapped but its device %1 could not be opened")
                    .arg(QString::fromWCharArray(kInjectorDevicePath)));
    return false;
  }

  // The driver stays mapped for the session - never unload it.
  VirtualFree(localImage, 0, MEM_RELEASE);
  *outDevice = device;
  return true;
}
}    // namespace KernelInjector

#endif    // Q_OS_WIN
