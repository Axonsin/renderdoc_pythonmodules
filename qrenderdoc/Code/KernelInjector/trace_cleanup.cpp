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

#include "trace_cleanup.h"
#include "kernel_mem.h"
#include "kernel_scan.h"
#include "nt_internals.h"

#if defined(Q_OS_WIN)

#include <windows.h>
#include <winternl.h>
#include <QDebug>

#include <cstring>
#include <cwchar>
#include <memory>
#include <vector>

namespace KernelInjector
{
namespace TraceCleanup
{
using namespace KernelScan;

namespace
{
// ---------------------------------------------------------------------------
// Kernel structures (offsets taken from kdmapper's nt.hpp, stable across
// supported Windows builds)
// ---------------------------------------------------------------------------

struct RtlBalancedLinks
{
  uint64_t Parent;
  uint64_t LeftChild;
  uint64_t RightChild;
  uint8_t Balance;
  uint8_t Reserved[3];
};

struct RtlAvlTable
{
  RtlBalancedLinks BalancedRoot;    // +0x00 (padded to 0x20 by alignment)
  uint64_t OrderedPointer;          // +0x20
  uint32_t WhichOrderedElement;     // +0x28
  uint32_t NumberGenericTableElements;    // +0x2C
  uint32_t DepthOfTree;             // +0x30
  uint32_t pad1;                    // +0x34
  uint64_t RestartKey;              // +0x38
  uint32_t DeleteCount;             // +0x40
  uint32_t pad2;                    // +0x44
  uint64_t CompareRoutine;          // +0x48
  uint64_t AllocateRoutine;         // +0x50
  uint64_t FreeRoutine;             // +0x58
  uint64_t TableContext;            // +0x60
};
static_assert(offsetof(RtlAvlTable, DeleteCount) == 0x40, "RtlAvlTable.DeleteCount offset");

// Field-for-field match of kdmapper's nt.hpp (PiDDBCacheEntry /
// HashBucketEntry). The embedded UNICODE_STRING keeps the standard x64
// layout: Length/MaximumLength first, then 4 bytes of padding, then Buffer.
// A previous revision swapped Buffer/Length and moved TimeDateStamp to
// +0x1C, which made the PiDDB lookup key garbage and the CI hash walk read
// the wrong fields - both cleanups silently (or crashingly) dead.
struct PiDdbCacheEntry
{
  uint64_t ListFlink;                // +0x00 LIST_ENTRY.Flink
  uint64_t ListBlink;                // +0x08 LIST_ENTRY.Blink
  uint16_t DriverNameLength;         // +0x10 UNICODE_STRING.Length
  uint16_t DriverNameMaximumLength;  // +0x12 UNICODE_STRING.MaximumLength
  uint32_t reserved;                 // +0x14 (UNICODE_STRING padding)
  uint64_t DriverNameBuffer;         // +0x18 UNICODE_STRING.Buffer
  uint32_t TimeDateStamp;            // +0x20
  uint32_t LoadStatus;               // +0x24
};
static_assert(offsetof(PiDdbCacheEntry, DriverNameLength) == 0x10, "PiDDB DriverName.Length offset");
static_assert(offsetof(PiDdbCacheEntry, DriverNameBuffer) == 0x18, "PiDDB DriverName.Buffer offset");
static_assert(offsetof(PiDdbCacheEntry, TimeDateStamp) == 0x20, "PiDDB TimeDateStamp offset");

struct HashBucketEntry
{
  uint64_t Next;                     // +0x00 (single Next pointer)
  uint16_t DriverNameLength;         // +0x08 UNICODE_STRING.Length
  uint16_t DriverNameMaximumLength;  // +0x0A UNICODE_STRING.MaximumLength
  uint32_t reserved;                 // +0x0C (UNICODE_STRING padding)
  uint64_t DriverNameBuffer;         // +0x10 UNICODE_STRING.Buffer
  // CertHash[5] follows at +0x18; not needed.
};
static_assert(offsetof(HashBucketEntry, DriverNameLength) == 0x08, "CI bucket DriverName.Length offset");
static_assert(offsetof(HashBucketEntry, DriverNameBuffer) == 0x10, "CI bucket DriverName.Buffer offset");

// ---------------------------------------------------------------------------
// Shared helpers used by the cleanups
// ---------------------------------------------------------------------------

bool ExAcquireResourceExclusiveLite(KernelMem *mem, uint64_t resource, bool wait)
{
  if(resource == 0)
    return false;

  uint64_t fn = mem->FindExport("ExAcquireResourceExclusiveLite");
  if(fn == 0)
    return false;

  BOOLEAN out = FALSE;
  return mem->CallKernelFunction(&out, fn, resource, wait ? TRUE : FALSE) && out;
}

bool ExReleaseResourceLite(KernelMem *mem, uint64_t resource)
{
  if(resource == 0)
    return false;

  uint64_t fn = mem->FindExport("ExReleaseResourceLite");
  if(fn == 0)
    return false;

  return mem->CallKernelFunction<void>(nullptr, fn, resource);
}

bool RtlDeleteElementGenericTableAvl(KernelMem *mem, uint64_t table, uint64_t buffer)
{
  uint64_t fn = mem->FindExport("RtlDeleteElementGenericTableAvl");
  if(fn == 0)
    return false;

  BOOLEAN out = FALSE;
  return mem->CallKernelFunction(&out, fn, table, buffer) && out;
}

uint64_t RtlLookupElementGenericTableAvl(KernelMem *mem, uint64_t table, void *buffer)
{
  uint64_t fn = mem->FindExport("RtlLookupElementGenericTableAvl");
  if(fn == 0)
    return 0;

  uint64_t out = 0;
  if(!mem->CallKernelFunction(&out, fn, table, buffer))
    return 0;
  return out;
}
}    // namespace

bool ClearPiDDBCacheTable(KernelMem *mem, const QString &driverName, uint32_t timeDateStamp)
{
  const uint64_t ntBase = mem->NtoskrnlBase();
  if(ntBase == 0)
    return false;

  // The PiDDBLock/PiDDBCacheTable pointers are resolved by pattern scanning
  // the PAGE section of ntoskrnl, with fallback patterns for newer builds.
  // Patterns and offsets come from kdmapper.
  static const uint8_t piddbLockPattern1[] = {
      0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0x00, 0x00, 0x00, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25,
      0x00, 0x00, 0x00, 0x00, 0x66, 0xFF, 0x88, 0x00, 0x00, 0x00, 0x00, 0xB2, 0x01, 0x48, 0x8D,
      0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x00, 0x24};
  static const char piddbLockMask1[] = "xxxxxx????xxxxx????xxx????xxxxx????x????xx?x";

  static const uint8_t piddbLockPattern2[] = {0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x48, 0x85,
                                              0xC9, 0x0F, 0x85, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D,
                                              0x0D, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00,
                                              0x00, 0xE8};
  static const char piddbLockMask2[] = "xxx????xxxxx????xxx????x????x";

  static const uint8_t piddbLockPattern3[] = {
      0x8B, 0xD8, 0x85, 0xC0, 0x0F, 0x88, 0x00, 0x00, 0x00, 0x00, 0x65, 0x48, 0x8B, 0x04, 0x25,
      0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, 0xB2, 0x01, 0x66, 0xFF,
      0x88, 0x00, 0x00, 0x00, 0x00, 0x90, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x00, 0x24};
  static const char piddbLockMask3[] = "xxxxxx????xxxxx????xxx????xxxxx????xx????xx?x";

  static const uint8_t piddbTablePattern1[] = {0x66, 0x03, 0xD2, 0x48, 0x8D, 0x0D};
  static const char piddbTableMask1[] = "xxxxxx";

  static const uint8_t piddbTablePattern2[] = {0x48, 0x8B, 0xF9, 0x33, 0xC0, 0x48, 0x8D, 0x0D};
  static const char piddbTableMask2[] = "xxxxxxxx";

  uint64_t piddbLockPtr = FindPatternInSectionAtKernel(mem, "PAGE", ntBase, piddbLockPattern1,
                                                       piddbLockMask1);
  int lockOffset = 0;

  if(piddbLockPtr == 0)
  {
    piddbLockPtr = FindPatternInSectionAtKernel(mem, "PAGE", ntBase, piddbLockPattern2,
                                                piddbLockMask2);
    lockOffset = 16;
    if(piddbLockPtr != 0)
      qInfo() << "KernelInjector: PiDDBLock found with second pattern";
  }
  else
  {
    lockOffset = 28;
  }

  if(piddbLockPtr == 0)
  {
    piddbLockPtr = FindPatternInSectionAtKernel(mem, "PAGE", ntBase, piddbLockPattern3,
                                                piddbLockMask3);
    lockOffset = 19;
    if(piddbLockPtr != 0)
      qInfo() << "KernelInjector: PiDDBLock found with third pattern";
  }

  if(piddbLockPtr == 0)
  {
    qWarning() << "KernelInjector: PiDDBLock not found";
    return false;
  }

  uint64_t piddbTablePtr = FindPatternInSectionAtKernel(mem, "PAGE", ntBase, piddbTablePattern1,
                                                        piddbTableMask1);
  int tableOffset = 0;

  if(piddbTablePtr == 0)
  {
    piddbTablePtr = FindPatternInSectionAtKernel(mem, "PAGE", ntBase, piddbTablePattern2,
                                                 piddbTableMask2);
    tableOffset = 2;
    if(piddbTablePtr != 0)
      qInfo() << "KernelInjector: PiDDBCacheTable found with second pattern";
  }
  else
  {
    tableOffset = 0;
  }

  if(piddbTablePtr == 0)
  {
    qWarning() << "KernelInjector: PiDDBCacheTable not found";
    return false;
  }

  const uint64_t piddbLock = ResolveRelativeAddress(mem, piddbLockPtr + lockOffset, 3, 7);
  const uint64_t piddbTable = ResolveRelativeAddress(mem, piddbTablePtr + tableOffset, 6, 10);

  if(piddbLock == 0 || piddbTable == 0)
    return false;

  if(!ExAcquireResourceExclusiveLite(mem, piddbLock, true))
  {
    qWarning() << "KernelInjector: can't lock PiDDBCacheTable";
    return false;
  }

  // Build a lookup key in our own memory; the kernel compare routine reads the
  // UNICODE_STRING buffer pointer in our context (CallKernelFunction runs on
  // our thread).
  std::wstring name = driverName.toStdWString();
  PiDdbCacheEntry localEntry = {};
  localEntry.DriverNameBuffer = (uint64_t)name.c_str();
  localEntry.DriverNameLength = (uint16_t)(name.size() * sizeof(wchar_t));
  localEntry.DriverNameMaximumLength = localEntry.DriverNameLength + 2;
  localEntry.TimeDateStamp = timeDateStamp;

  const uint64_t found = RtlLookupElementGenericTableAvl(mem, piddbTable, &localEntry);

  if(found == 0)
  {
    qWarning() << "KernelInjector: driver not found in PiDDBCacheTable";
    ExReleaseResourceLite(mem, piddbLock);
    return false;
  }

  uint64_t prev = 0, next = 0;
  if(!mem->ReadVirtual(found + offsetof(PiDdbCacheEntry, ListBlink), &prev, sizeof(prev)) ||
     !mem->ReadVirtual(found + offsetof(PiDdbCacheEntry, ListFlink), &next, sizeof(next)))
  {
    ExReleaseResourceLite(mem, piddbLock);
    return false;
  }

  // Unlink from the LRU list, then delete from the AVL table.
  bool ok = mem->WriteVirtual(prev + offsetof(LIST_ENTRY, Flink), &next, sizeof(next));
  ok = ok && mem->WriteVirtual(next + offsetof(LIST_ENTRY, Blink), &prev, sizeof(prev));
  ok = ok && RtlDeleteElementGenericTableAvl(mem, piddbTable, found);

  if(ok)
  {
    // Keep the table's DeleteCount consistent.
    uint32_t deleteCount = 0;
    if(mem->ReadVirtual(piddbTable + offsetof(RtlAvlTable, DeleteCount), &deleteCount,
                        sizeof(deleteCount)) &&
       deleteCount > 0)
    {
      deleteCount--;
      mem->WriteVirtual(piddbTable + offsetof(RtlAvlTable, DeleteCount), &deleteCount,
                        sizeof(deleteCount));
    }
  }

  ExReleaseResourceLite(mem, piddbLock);

  if(ok)
    qInfo() << "KernelInjector: PiDDBCacheTable cleaned";
  else
    qWarning() << "KernelInjector: failed to remove entry from PiDDBCacheTable";

  return ok;
}

bool ClearKernelHashBucketList(KernelMem *mem, const QString &driverName, const QString &tempPath)
{
  const uint64_t ci = GetKernelModuleAddress("ci.dll");
  if(ci == 0)
  {
    qWarning() << "KernelInjector: ci.dll not found";
    return false;
  }

  static const uint8_t hashListPattern[] = {0x48, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00,
                                            0xEB, 0x00, 0xF7, 0x43, 0x40, 0x00, 0x20,
                                            0x00, 0x00};
  static const char hashListMask[] = "xxx????x?xxxxxxx";

  const uint64_t sig = FindPatternInSectionAtKernel(mem, "PAGE", ci, hashListPattern, hashListMask);
  if(sig == 0)
  {
    qWarning() << "KernelInjector: g_KernelHashBucketList not found";
    return false;
  }

  static const uint8_t lockPattern[] = {0x48, 0x8D, 0x0D};
  const uint64_t sig2 = FindPatternAtKernel(mem, sig - 50, 50, lockPattern, "xxx");
  if(sig2 == 0)
  {
    qWarning() << "KernelInjector: g_HashCacheLock not found";
    return false;
  }

  const uint64_t hashBucketList = ResolveRelativeAddress(mem, sig, 3, 7);
  const uint64_t hashCacheLock = ResolveRelativeAddress(mem, sig2, 3, 7);

  if(hashBucketList == 0 || hashCacheLock == 0)
    return false;

  if(!ExAcquireResourceExclusiveLite(mem, hashCacheLock, true))
  {
    qWarning() << "KernelInjector: can't lock g_HashCacheLock";
    return false;
  }

  // The hash entries are keyed by the full driver image path. The kernel
  // stores the path without the drive prefix ("C:"), hence the length gate;
  // the match itself is a substring search for the random service name, which
  // is the only part of the path we control and therefore stable.
  std::wstring searchPath = tempPath.toStdWString();
  std::wstring serviceName = driverName.toStdWString();
  const SIZE_T expectedLen = (searchPath.size() - 2) * 2;

  uint64_t prevAddr = hashBucketList;
  uint64_t entry = 0;
  if(!mem->ReadVirtual(prevAddr, &entry, sizeof(entry)))
  {
    ExReleaseResourceLite(mem, hashCacheLock);
    return false;
  }

  if(entry == 0)
  {
    // Nothing cached (e.g. the driver was not hash validated).
    ExReleaseResourceLite(mem, hashCacheLock);
    qInfo() << "KernelInjector: CI hash bucket list is empty, nothing to clean";
    return true;
  }

  bool ok = false;

  while(entry != 0)
  {
    uint16_t nameLen = 0;
    if(!mem->ReadVirtual(entry + offsetof(HashBucketEntry, DriverNameLength), &nameLen,
                         sizeof(nameLen)) ||
       nameLen == 0)
      break;

    if(nameLen == expectedLen)
    {
      uint64_t buffer = 0;
      if(mem->ReadVirtual(entry + offsetof(HashBucketEntry, DriverNameBuffer), &buffer,
                          sizeof(buffer)) &&
         buffer != 0)
      {
        std::vector<wchar_t> name(nameLen / 2 + 1, 0);
        if(mem->ReadVirtual(buffer, name.data(), nameLen) &&
           wcsstr(name.data(), serviceName.c_str()) != nullptr)
        {
          // Unlink by patching the previous entry's Next pointer, then free
          // the entry (kdmapper does the same ExFreePool).
          uint64_t next = 0;
          if(mem->ReadVirtual(entry + offsetof(HashBucketEntry, Next), &next, sizeof(next)) &&
             mem->WriteVirtual(prevAddr, &next, sizeof(next)))
          {
            ok = true;
            qInfo() << "KernelInjector: CI hash bucket cleaned";

            const uint64_t exFreePool = mem->FindExport("ExFreePool");
            if(exFreePool != 0)
            {
              if(!mem->CallKernelFunction<void>(nullptr, exFreePool, entry))
                qWarning() << "KernelInjector: ExFreePool of CI hash entry failed";
            }
          }
          break;
        }
      }
    }

    prevAddr = entry + offsetof(HashBucketEntry, Next);
    if(!mem->ReadVirtual(prevAddr, &entry, sizeof(entry)))
      break;
  }

  ExReleaseResourceLite(mem, hashCacheLock);
  return ok;
}

bool ClearMmUnloadedDrivers(KernelMem *mem, void *deviceHandle)
{
  // Find the kernel object for our device handle via SystemExtendedHandleInformation,
  // then walk device object -> driver object -> driver section and zero the
  // driver name length so MiRememberUnloadedDriver won't record an unload.
  ULONG bufferSize = 0;
  NTSTATUS status = NtQuerySystemInformation(NtInternals::SystemExtendedHandleInformation, nullptr,
                                             0, &bufferSize);

  std::vector<uint8_t> buffer;
  while(status == STATUS_INFO_LENGTH_MISMATCH)
  {
    buffer.resize(bufferSize);
    status = NtQuerySystemInformation(NtInternals::SystemExtendedHandleInformation, buffer.data(),
                                      bufferSize, &bufferSize);
  }

  if(!NT_SUCCESS(status) || buffer.empty())
    return false;

  const auto *handleInfo = (const NtInternals::SystemHandleInfoEx *)buffer.data();
  const uintptr_t count = handleInfo->HandleCount;

  uint64_t object = 0;

  for(uintptr_t i = 0; i < count; i++)
  {
    const NtInternals::SystemHandle &h = handleInfo->Handles[i];
    if(h.UniqueProcessId != (HANDLE)(uintptr_t)GetCurrentProcessId())
      continue;
    if(h.HandleValue != deviceHandle)
      continue;

    object = (uint64_t)h.Object;
    break;
  }

  if(object == 0)
    return false;

  uint64_t deviceObject = 0;
  if(!mem->ReadVirtual(object + 0x8, &deviceObject, sizeof(deviceObject)) || deviceObject == 0)
    return false;

  uint64_t driverObject = 0;
  if(!mem->ReadVirtual(deviceObject + 0x8, &driverObject, sizeof(driverObject)) || driverObject == 0)
    return false;

  uint64_t driverSection = 0;
  if(!mem->ReadVirtual(driverObject + 0x28, &driverSection, sizeof(driverSection)) ||
     driverSection == 0)
    return false;

  struct
  {
    uint16_t Length;
    uint16_t MaximumLength;
    uint64_t Buffer;
  } unicodeString = {};

  if(!mem->ReadVirtual(driverSection + 0x58, &unicodeString, sizeof(unicodeString)) ||
     unicodeString.Length == 0)
    return false;

  unicodeString.Length = 0;
  if(!mem->WriteVirtual(driverSection + 0x58, &unicodeString, sizeof(unicodeString)))
    return false;

  qInfo() << "KernelInjector: MmUnloadedDrivers cleaned";
  return true;
}

bool ClearWdFilterDriverList(KernelMem *mem, const QString &driverName)
{
  const uint64_t wdFilter = GetKernelModuleAddress("WdFilter.sys");
  if(wdFilter == 0)
  {
    qInfo() << "KernelInjector: WdFilter.sys not loaded, cleanup skipped";
    return true;
  }

  static const uint8_t runtimeListPattern[] = {0x48, 0x8B, 0x0D, 0x00, 0x00, 0x00,
                                               0x00, 0xFF, 0x05};
  static const char runtimeListMask[] = "xxx????xx";

  static const uint8_t runtimeCountPattern[] = {0xFF, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x39, 0x11};
  static const char runtimeCountMask[] = "xx????xxx";

  static const uint8_t freeDriverInfoPattern1[] = {0x89, 0x00, 0x08, 0xE8, 0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE9};
  static const char freeDriverInfoMask1[] = "x?xx???????????x";

  static const uint8_t freeDriverInfoPattern2[] = {0x89, 0x00, 0x08, 0x00, 0x00, 0x00, 0xE8, 0x00,
                                                   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                   0x00, 0x00, 0xE9};
  static const char freeDriverInfoMask2[] = "x?x???x???????????x";

  uint64_t runtimeList = FindPatternInSectionAtKernel(mem, "PAGE", wdFilter, runtimeListPattern,
                                                      runtimeListMask);
  uint64_t runtimeCountRef = FindPatternInSectionAtKernel(mem, "PAGE", wdFilter,
                                                          runtimeCountPattern, runtimeCountMask);
  if(runtimeList == 0 || runtimeCountRef == 0)
  {
    qWarning() << "KernelInjector: WdFilter RuntimeDriversList not found";
    return false;
  }

  uint64_t freeDriverInfoRef = FindPatternInSectionAtKernel(mem, "PAGE", wdFilter,
                                                            freeDriverInfoPattern1,
                                                            freeDriverInfoMask1);
  if(freeDriverInfoRef != 0)
  {
    freeDriverInfoRef += 0x3;    // skip until the call instruction
  }
  else
  {
    freeDriverInfoRef = FindPatternInSectionAtKernel(mem, "PAGE", wdFilter, freeDriverInfoPattern2,
                                                     freeDriverInfoMask2);
    if(freeDriverInfoRef == 0)
    {
      qWarning() << "KernelInjector: WdFilter MpFreeDriverInfoEx not found";
      return false;
    }
    freeDriverInfoRef += 0x3 + 0x3;    // second pattern: skip call-site padding + call
  }

  runtimeList = ResolveRelativeAddress(mem, runtimeList, 3, 7);
  if(runtimeList == 0)
    return false;

  const uint64_t runtimeListHead = runtimeList - 0x8;
  const uint64_t runtimeCount = ResolveRelativeAddress(mem, runtimeCountRef, 2, 6);
  const uint64_t freeDriverInfo = ResolveRelativeAddress(mem, freeDriverInfoRef, 1, 5);

  uint64_t runtimeArray = 0;
  if(runtimeCount == 0 || !mem->ReadVirtual(runtimeCount + 0x8, &runtimeArray, sizeof(runtimeArray)))
    return false;

  std::wstring name = driverName.toStdWString();

  auto readListEntry = [&](uint64_t addr, uint64_t *out) {
    return mem->ReadVirtual(addr, out, sizeof(uint64_t));
  };

  uint64_t entry = 0;
  if(!readListEntry(runtimeListHead, &entry))
    return false;

  bool ok = false;

  while(entry != runtimeListHead)
  {
    struct
    {
      uint16_t Length;
      uint16_t MaximumLength;
      uint64_t Buffer;
    } imageName = {};

    if(mem->ReadVirtual(entry + 0x10, &imageName, sizeof(imageName)) && imageName.Length != 0)
    {
      std::vector<wchar_t> imageNameBuf(imageName.Length / 2 + 1, 0);
      if(mem->ReadVirtual(imageName.Buffer, imageNameBuf.data(), imageName.Length) &&
         wcsstr(imageNameBuf.data(), name.c_str()) != nullptr)
      {
        // Remove from the RuntimeDriversArray slot (the array stores pointers
        // to the entry minus 0x10).
        const uint64_t sameIndexList = entry - 0x10;
        bool removedFromArray = false;

        for(int k = 0; k < 256; k++)
        {
          uint64_t value = 0;
          mem->ReadVirtual(runtimeArray + k * 8, &value, sizeof(value));
          if(value == sameIndexList)
          {
            uint64_t empty = runtimeCount + 1;
            mem->WriteVirtual(runtimeArray + k * 8, &empty, sizeof(empty));
            removedFromArray = true;
            break;
          }
        }

        if(removedFromArray)
        {
          uint64_t next = 0, prev = 0;
          readListEntry(entry + offsetof(LIST_ENTRY, Flink), &next);
          readListEntry(entry + offsetof(LIST_ENTRY, Blink), &prev);

          ok = mem->WriteVirtual(next + offsetof(LIST_ENTRY, Blink), &prev, sizeof(prev));
          ok = ok && mem->WriteVirtual(prev + offsetof(LIST_ENTRY, Flink), &next, sizeof(next));

          if(ok)
          {
            uint32_t count = 0;
            if(mem->ReadVirtual(runtimeCount, &count, sizeof(count)) && count > 0)
            {
              count--;
              mem->WriteVirtual(runtimeCount, &count, sizeof(count));
            }

            // Release the driver info structure. Verify the magic first: newer
            // WdFilter versions changed the layout and freeing blindly BSODs.
            uint64_t driverInfo = entry - 0x20;
            uint16_t magic = 0;
            if(mem->ReadVirtual(driverInfo, &magic, sizeof(magic)) && magic == 0xDA18)
            {
              if(!mem->CallKernelFunction<void>(nullptr, freeDriverInfo, driverInfo))
                qWarning() << "KernelInjector: MpFreeDriverInfoEx call failed";
            }
            else
            {
              qWarning() << "KernelInjector: WdFilter DriverInfo magic invalid, entry left "
                            "allocated to avoid BSOD";
            }

            qInfo() << "KernelInjector: WdFilter runtime driver list cleaned";
          }
        }
        else
        {
          qWarning() << "KernelInjector: failed to remove from RuntimeDriversArray";
        }

        break;
      }
    }

    if(!readListEntry(entry + offsetof(LIST_ENTRY, Flink), &entry))
      break;
  }

  return ok;
}

bool CleanupAll(KernelMem *mem, void *deviceHandle, const QString &driverName,
                const QString &tempPath, uint32_t timeDateStamp)
{
  bool ok = true;

  ok = ClearPiDDBCacheTable(mem, driverName, timeDateStamp) && ok;
  ok = ClearKernelHashBucketList(mem, driverName, tempPath) && ok;
  ok = ClearMmUnloadedDrivers(mem, deviceHandle) && ok;
  ok = ClearWdFilterDriverList(mem, driverName) && ok;

  return ok;
}
}    // namespace TraceCleanup
}    // namespace KernelInjector

#endif    // Q_OS_WIN
