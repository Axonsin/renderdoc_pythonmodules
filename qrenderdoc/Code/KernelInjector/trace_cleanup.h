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

#if defined(Q_OS_WIN)

namespace KernelInjector
{
class KernelMem;

// kdmapper-style trace cleanup: removes the just-loaded vulnerable driver from
// the kernel structures that would reveal it (PiDDBCacheTable, CI hash
// buckets, MmUnloadedDrivers, WdFilter's runtime driver list). Ported from
// TheCruZ/kdmapper intel_driver.cpp, which RenderPro also copied.
namespace TraceCleanup
{
// All four cleanups. Each one fails soft: returns false but the caller can
// decide whether a partial cleanup is acceptable. driverName is the random
// service name, tempPath the %TEMP% file path (CI buckets are keyed by path).
bool ClearPiDDBCacheTable(KernelMem *mem, const QString &driverName, uint32_t timeDateStamp);
bool ClearKernelHashBucketList(KernelMem *mem, const QString &driverName, const QString &tempPath);
bool ClearMmUnloadedDrivers(KernelMem *mem, void *deviceHandle);
bool ClearWdFilterDriverList(KernelMem *mem, const QString &driverName);

bool CleanupAll(KernelMem *mem, void *deviceHandle, const QString &driverName,
                const QString &tempPath, uint32_t timeDateStamp);
}    // namespace TraceCleanup
}    // namespace KernelInjector

#endif    // Q_OS_WIN
