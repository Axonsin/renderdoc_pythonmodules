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

#include <ntddk.h>

// This driver is manually mapped into the kernel by the qrenderdoc coordinator
// (never loaded through the service manager, so it never appears in
// PsLoadedModuleList or any driver-tracking structure). Requirements:
//   - imports must only come from ntoskrnl.exe
//   - no CRT usage, compiled with /GS-
//   - static state is explicitly zeroed at entry (manually mapped .bss is not
//     guaranteed to be zeroed)
//
// DriverEntry is invoked with a NULL DriverObject; the driver builds a fake
// one. It then creates a control device so the coordinator can request
// injections:
//
//   IOCTL_RDI_INJECT: {ULONG ProcessId, WCHAR DllPath[1024], ULONG64 Secret}
//   -> while attached to the target process, writes the DLL path into its
//      address space and creates a real user thread in the target whose start
//      routine is the target's kernel32!LoadLibraryW. The injected DLL (the
//      renderdoc shim) then performs the capture setup on its own.
//
// The device is created without a DACL and the IOCTL allows FILE_ANY_ACCESS,
// so requests are authenticated instead: the first request carrying a
// non-zero Secret arms the driver with the caller PID + secret, and all
// later requests must match both (see RdiAuthorize in driver.c).
//
// 64-bit targets only. The remote path allocation is deliberately leaked: the
// injected thread references it until the DLL load completes and it cannot be
// freed safely from the driver.

#define RDI_DEVICE_NAME L"\\Device\\RenderDicInj"
#define RDI_SYMLINK_NAME L"\\DosDevices\\RenderDicInj"

#define RDI_DEVICE_TYPE FILE_DEVICE_UNKNOWN
#define RDI_IOCTL_INJECT CTL_CODE(RDI_DEVICE_TYPE, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _RDI_INJECT_REQUEST
{
  ULONG ProcessId;
  WCHAR DllPath[1024];
  ULONG64 Secret;
} RDI_INJECT_REQUEST;

// inject.c
NTSTATUS RdiInjectDll(HANDLE ProcessId, PCWSTR DllPath);
