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

#include "kernel_mem.h"

#if defined(Q_OS_WIN)

#include <windows.h>
#include <winternl.h>

#include <cstring>
#include <vector>

// Shared kernel-module resolution and pattern-scanning helpers used by the
// trace cleanup and the manual mapper. Ported from TheCruZ/kdmapper's utils
// and intel_driver.cpp.

namespace KernelInjector
{
namespace KernelScan
{
// Base of a loaded kernel module by file name (user mode, no kernel access).
uint64_t GetKernelModuleAddress(const char *name);

// Pattern scan over a kernel virtual address range. mask uses 'x' for exact
// bytes and '?' for wildcards. Returns the matching VA or 0.
uint64_t FindPatternAtKernel(KernelMem *mem, uint64_t address, size_t len,
                             const uint8_t *pattern, const char *mask);

// Virtual address of a section by name inside a kernel module image.
uint64_t FindSectionAtKernel(KernelMem *mem, const char *sectionName, uint64_t modulePtr,
                             uint32_t *outSize);

uint64_t FindPatternInSectionAtKernel(KernelMem *mem, const char *sectionName, uint64_t modulePtr,
                                      const uint8_t *pattern, const char *mask);

// Resolves a RIP-relative address at instruction+offsetOffset with the given
// total instruction size.
uint64_t ResolveRelativeAddress(KernelMem *mem, uint64_t instruction, int offsetOffset,
                                int instructionSize);
}    // namespace KernelScan
}    // namespace KernelInjector

#endif    // Q_OS_WIN
