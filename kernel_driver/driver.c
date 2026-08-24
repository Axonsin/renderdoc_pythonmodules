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

  // A manually mapped driver object never goes through driver creation, so
  // the I/O manager has not pre-filled the dispatch table with
  // IopInvalidDeviceRequest - every unset slot is NULL and dispatching any
  // IRP there crashes the kernel. IRP_MJ_CLEANUP in particular arrives for
  // every handle close, so the whole table must be filled.
  for(ULONG i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    DriverObject->MajorFunction[i] = DispatchCreateClose;

  DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;

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

      // Guarantee termination inside the request buffer.
      req->DllPath[ARRAYSIZE(req->DllPath) - 1] = 0;

      status = RdiInjectDll((HANDLE)(ULONG_PTR)req->ProcessId, req->DllPath);
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
