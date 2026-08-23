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
// Which vulnerable driver provides the physical R/W primitive.
enum class BackendId
{
  Portwell,
  Tbt,
};

// State of a successfully loaded vulnerable driver. The service name is the
// random name used both for the registry service key and the %TEMP% file, so
// the trace cleanup can look the driver up by name.
struct LoadedDriver
{
  void *device = nullptr;    // CreateFileW handle to the driver device
  QString serviceName;
  QString tempPath;
};

// Loads the embedded vulnerable driver as a kernel service (random name,
// %TEMP% file, NtLoadDriver), opens its device and runs the kdmapper-style
// trace cleanup. Mirrors RenderPro's rpro_kdmapper_load_dxreport_chain.
class DriverLoader
{
public:
  // Returns false and fills errorDetail with a readable message on failure.
  // On success *out holds the device handle plus the names used for cleanup.
  static bool Load(BackendId backend, LoadedDriver *out, QString *errorDetail);

  // Unloads the driver: NtUnloadDriver, delete the service key, overwrite the
  // %TEMP% file with random data and delete it (kdmapper's Unload behaviour).
  static void Unload(const LoadedDriver &driver);

  // PE TimeDateStamp of the embedded driver image (PiDDB lookup key).
  static uint32_t DriverTimeDateStamp(BackendId backend);

private:
  static QString RandomServiceName();
  static bool EnablePrivilege(int privilege);
  static bool WriteTempFile(const QString &path, const void *bytes, size_t size);
  static bool CreateServiceRegistryKey(const QString &serviceName, const QString &imagePath);
  static bool DeleteServiceRegistryKey(const QString &serviceName);
  static bool NtLoadDriverByName(const QString &serviceName);
  static bool NtUnloadDriverByName(const QString &serviceName);
  static void *OpenDevice(const wchar_t *devicePath);
};
}    // namespace KernelInjector

#endif    // Q_OS_WIN
