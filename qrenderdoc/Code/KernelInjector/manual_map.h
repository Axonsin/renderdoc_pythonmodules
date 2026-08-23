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
#include <cstddef>
#include <QString>

#if defined(Q_OS_WIN)

namespace KernelInjector
{
class KernelMem;

// kdmapper-style manual mapping of the second (injection) driver. The driver
// image is allocated as independent kernel pages, copied with relocations and
// imports fixed up, made executable per-section, and its entry point is
// invoked through the NtAddAtom trampoline. The mapped driver never appears
// in PsLoadedModuleList or any driver-tracking structure.
//
// The mapped driver receives a NULL DriverObject (the driver builds its own
// fake object) and creates its control device itself; MapDriver then opens
// that device so the coordinator can issue injection requests.
class ManualMapper
{
public:
  // Maps driverBytes into kernel memory and calls its entry point. On success
  // opens the device the driver created and stores the handle in *outDevice.
  // The driver is intentionally left mapped for the lifetime of the session.
  static bool MapDriver(KernelMem *mem, const unsigned char *driverBytes, size_t size,
                        void **outDevice, QString *errorDetail);
};
}    // namespace KernelInjector

#endif    // Q_OS_WIN
