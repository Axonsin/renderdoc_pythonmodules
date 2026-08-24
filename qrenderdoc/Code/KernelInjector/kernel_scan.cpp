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

#include "kernel_scan.h"
#include "nt_internals.h"

#if defined(Q_OS_WIN)

#include <new>

namespace KernelInjector
{
namespace KernelScan
{
namespace
{
struct ModuleInfo
{
  uint64_t base = 0;
  uint32_t size = 0;
  char name[64] = {0};
};

bool EnumerateKernelModules(std::vector<ModuleInfo> *out)
{
  ULONG size = 0;
  NTSTATUS status =
      NtQuerySystemInformation(NtInternals::SystemModuleInformation, nullptr, 0, &size);
  if(status != STATUS_INFO_LENGTH_MISMATCH)
    return false;

  std::vector<uint8_t> buffer(size);
  status = NtQuerySystemInformation(NtInternals::SystemModuleInformation, buffer.data(), size,
                                    nullptr);
  if(!NT_SUCCESS(status))
    return false;

  const auto *modules = (const NtInternals::RtlModuleList *)buffer.data();

  for(ULONG i = 0; i < modules->NumberOfModules; i++)
  {
    const auto &m = modules->Modules[i];
    ModuleInfo info;
    info.base = (uint64_t)m.ImageBase;
    info.size = m.ImageSize;

    const char *filePart = (const char *)m.FullPathName + m.OffsetToFileName;
    strncpy(info.name, filePart, sizeof(info.name) - 1);

    out->push_back(info);
  }

  return true;
}
}    // namespace

uint64_t GetKernelModuleAddress(const char *name)
{
  std::vector<ModuleInfo> modules;
  if(!EnumerateKernelModules(&modules))
    return 0;

  for(const ModuleInfo &m : modules)
  {
    if(_stricmp(m.name, name) == 0)
      return m.base;
  }

  return 0;
}

uint64_t FindPatternAtKernel(KernelMem *mem, uint64_t address, size_t len, const uint8_t *pattern,
                             const char *mask)
{
  if(address == 0 || len == 0 || len > 1024 * 1024 * 1024)
    return 0;

  std::vector<uint8_t> data;
  try
  {
    data.resize(len);
  }
  catch(const std::bad_alloc &)
  {
    return 0;
  }

  if(!mem->ReadVirtual(address, data.data(), len))
    return 0;

  // The mask length drives the comparison: pattern bytes beyond strlen(mask)
  // are silently ignored (inherited kdmapper behaviour - note that the second
  // MmSetPageProtection pattern has fewer mask chars than pattern bytes).
  const size_t maskLen = strlen(mask);
  for(size_t i = 0; i + maskLen <= len; i++)
  {
    bool match = true;
    for(size_t j = 0; j < maskLen; j++)
    {
      if(mask[j] == 'x' && data[i + j] != pattern[j])
      {
        match = false;
        break;
      }
    }
    if(match)
      return address + i;
  }

  return 0;
}

uint64_t FindSectionAtKernel(KernelMem *mem, const char *sectionName, uint64_t modulePtr,
                             uint32_t *outSize)
{
  if(modulePtr == 0)
    return 0;

  uint8_t headers[0x1000];
  if(!mem->ReadVirtual(modulePtr, headers, sizeof(headers)))
    return 0;

  const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)headers;
  if(dos->e_magic != IMAGE_DOS_SIGNATURE)
    return 0;

  const IMAGE_NT_HEADERS64 *nt = (const IMAGE_NT_HEADERS64 *)(headers + dos->e_lfanew);
  if(nt->Signature != IMAGE_NT_SIGNATURE)
    return 0;

  for(WORD i = 0; i < nt->FileHeader.NumberOfSections; i++)
  {
    const IMAGE_SECTION_HEADER *sec =
        (const IMAGE_SECTION_HEADER *)(headers + dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) +
                                       i * sizeof(IMAGE_SECTION_HEADER));

    if(strncmp((const char *)sec->Name, sectionName, IMAGE_SIZEOF_SHORT_NAME) != 0)
      continue;

    if(outSize != nullptr)
      *outSize = sec->Misc.VirtualSize ? sec->Misc.VirtualSize : sec->SizeOfRawData;
    return modulePtr + sec->VirtualAddress;
  }

  return 0;
}

uint64_t FindPatternInSectionAtKernel(KernelMem *mem, const char *sectionName, uint64_t modulePtr,
                                      const uint8_t *pattern, const char *mask)
{
  uint32_t sectionSize = 0;
  uint64_t section = FindSectionAtKernel(mem, sectionName, modulePtr, &sectionSize);
  if(section == 0 || sectionSize == 0)
    return 0;
  return FindPatternAtKernel(mem, section, sectionSize, pattern, mask);
}

uint64_t ResolveRelativeAddress(KernelMem *mem, uint64_t instruction, int offsetOffset,
                                int instructionSize)
{
  LONG ripOffset = 0;
  if(!mem->ReadVirtual(instruction + offsetOffset, &ripOffset, sizeof(ripOffset)))
    return 0;
  return instruction + instructionSize + ripOffset;
}
}    // namespace KernelScan
}    // namespace KernelInjector

#endif    // Q_OS_WIN
