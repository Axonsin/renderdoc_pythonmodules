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

#include <windows.h>
#include <winternl.h>
#include <ntstatus.h>

#include <cstdint>

// Minimal NT internals used by the kernel injector. winternl.h only exposes a
// subset of these on any given SDK, so the pieces we need are defined here
// with names that cannot collide with the SDK headers.

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif

namespace KernelInjector
{
namespace NtInternals
{
constexpr SYSTEM_INFORMATION_CLASS SystemModuleInformation = (SYSTEM_INFORMATION_CLASS)11;
constexpr SYSTEM_INFORMATION_CLASS SystemExtendedHandleInformation = (SYSTEM_INFORMATION_CLASS)64;

// RTL_PROCESS_MODULE_INFORMATION / RTL_PROCESS_MODULES equivalents.
struct RtlModuleInformation
{
  void *Section;
  void *MappedBase;
  void *ImageBase;
  ULONG ImageSize;
  ULONG Flags;
  USHORT LoadOrderIndex;
  USHORT InitOrderIndex;
  USHORT LoadCount;
  USHORT OffsetToFileName;
  UCHAR FullPathName[256];
};

struct RtlModuleList
{
  ULONG NumberOfModules;
  RtlModuleInformation Modules[1];
};

// SYSTEM_HANDLE / SYSTEM_HANDLE_INFORMATION_EX equivalents.
struct SystemHandle
{
  void *Object;
  HANDLE UniqueProcessId;
  HANDLE HandleValue;
  ULONG GrantedAccess;
  USHORT CreatorBackTraceIndex;
  USHORT ObjectTypeIndex;
  ULONG HandleAttributes;
  ULONG Reserved;
};

struct SystemHandleInfoEx
{
  ULONG_PTR HandleCount;
  ULONG_PTR Reserved;
  SystemHandle Handles[1];
};
}    // namespace NtInternals
}    // namespace KernelInjector
