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

#include "driver.h"

// Driver object for the manually mapped case. DriverEntry is called with a
// NULL DriverObject; IoCreateDevice still works with a mostly-empty object
// (it only needs the device list head). Static storage in a manually mapped
// image is not guaranteed zeroed, so this is cleared at entry.
static DRIVER_OBJECT g_FakeDriverObject;

// Device authentication state (see driver.h). Cleared in DriverEntry for the
// same reason as g_FakeDriverObject.
static HANDLE g_OwnerPid = NULL;
static ULONG64 g_Secret = 0;

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS DispatchIoctl(PDEVICE_OBJECT DeviceObject, PIRP Irp);

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
  UNREFERENCED_PARAMETER(RegistryPath);

  if(DriverObject == NULL)
  {
    RtlZeroMemory(&g_FakeDriverObject, sizeof(g_FakeDriverObject));
    DriverObject = &g_FakeDriverObject;
  }

  g_OwnerPid = NULL;
  g_Secret = 0;

  // A manually mapped driver object never goes through driver creation, so
  // the I/O manager has not pre-filled the dispatch table with
  // IopInvalidDeviceRequest - every unset slot is NULL and dispatching any
  // IRP there crashes the kernel. IRP_MJ_CLEANUP in particular arrives for
  // every handle close, so the whole table must be filled, and it must be
  // filled before the device exists (no window with NULL slots).
  for(ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    DriverObject->MajorFunction[i] = DispatchCreateClose;

  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;

  UNICODE_STRING deviceName = RTL_CONSTANT_STRING(RDI_DEVICE_NAME);
  UNICODE_STRING symlinkName = RTL_CONSTANT_STRING(RDI_SYMLINK_NAME);

  PDEVICE_OBJECT deviceObject = NULL;
  NTSTATUS status = IoCreateDevice(DriverObject, 0, &deviceName, RDI_DEVICE_TYPE, 0, FALSE,
                                   &deviceObject);
  if(!NT_SUCCESS(status))
    return status;

  status = IoCreateSymbolicLink(&symlinkName, &deviceName);
  if(!NT_SUCCESS(status))
  {
    IoDeleteDevice(deviceObject);
    return status;
  }

  return STATUS_SUCCESS;
}

NTSTATUS DispatchCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
  UNREFERENCED_PARAMETER(DeviceObject);

  Irp->IoStatus.Status = STATUS_SUCCESS;
  Irp->IoStatus.Information = 0;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return STATUS_SUCCESS;
}

// The control device has no DACL and the IOCTL grants FILE_ANY_ACCESS, so
// without this check any local process could use the mapped driver to inject
// arbitrary DLLs into arbitrary processes. The first request carrying a
// non-zero Secret arms the device with the caller's PID and that secret;
// every later request must come from the same PID and present the same
// secret. Residual race: an attacker winning the window between device
// creation and the coordinator's first request arms it with their own secret
// - the coordinator then fails loudly (ACCESS_DENIED, and the residue probe
// on next start demands a reboot), so nothing is gained silently.
static BOOLEAN RdiAuthorize(RDI_INJECT_REQUEST *req)
{
  HANDLE caller = PsGetCurrentProcessId();

  if(g_OwnerPid == NULL)
  {
    if(req->Secret == 0)
      return FALSE;

    g_OwnerPid = caller;
    g_Secret = req->Secret;
    return TRUE;
  }

  return g_OwnerPid == caller && g_Secret == req->Secret;
}

NTSTATUS DispatchIoctl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
  UNREFERENCED_PARAMETER(DeviceObject);

  PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
  NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
  ULONG info = 0;

  if(stack->Parameters.DeviceIoControl.IoControlCode == RDI_IOCTL_INJECT)
  {
    if(stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(RDI_INJECT_REQUEST))
    {
      RDI_INJECT_REQUEST *req = (RDI_INJECT_REQUEST *)Irp->AssociatedIrp.SystemBuffer;

      if(RdiAuthorize(req))
      {
        // Guarantee termination inside the request buffer.
        req->DllPath[ARRAYSIZE(req->DllPath) - 1] = 0;

        status = RdiInjectDll((HANDLE)(ULONG_PTR)req->ProcessId, req->DllPath);
      }
      else
      {
        status = STATUS_ACCESS_DENIED;
      }
    }
    else
    {
      status = STATUS_BUFFER_TOO_SMALL;
    }
  }

  Irp->IoStatus.Status = status;
  Irp->IoStatus.Information = info;
  IoCompleteRequest(Irp, IO_NO_INCREMENT);
  return status;
}
