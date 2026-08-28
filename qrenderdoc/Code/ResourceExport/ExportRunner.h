/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 ******************************************************************************/

#pragma once

#include <QList>
#include <QMap>
#include <QObject>
#include <atomic>
#include "Code/ResourceExport/ResourceExport.h"

struct ICaptureContext;
class LambdaThread;

// Orchestrates a one-shot export of the given events on a background thread.
// All controller access goes through ReplayManager::BlockInvoke; the replay
// controller's current event is temporarily switched per event and restored
// once the run completes (or is cancelled). There is deliberately no queue -
// one run at a time, cancellable, nothing persisted.
class ExportRunner : public QObject
{
  Q_OBJECT

public:
  ExportRunner(ICaptureContext &ctx, QObject *parent = NULL);
  ~ExportRunner();

  // copies of the actions to export (snapshotted by the caller on the UI
  // thread), per-event manual base colour overrides and any extra textures
  // the user ticked beyond the base colour.
  void start(rdcarray<ActionDescription> actions, const ExportSettings &settings,
             const QMap<uint32_t, ResourceId> &baseColorOverrides,
             const QMap<uint32_t, QList<ResourceId>> &extraTextures);

  void cancel();
  bool isRunning() const { return m_Running; }
  // blocks until the run finishes (used on shutdown)
  void wait();

signals:
  void progress(int done, int total, const QString &stage);
  void itemResult(uint32_t eventId, bool ok, const QString &message);
  void finished(int okCount, int failCount, bool cancelled);

private:
  void run();

  ICaptureContext &m_Ctx;

  rdcarray<ActionDescription> m_Actions;
  ExportSettings m_Settings;
  QMap<uint32_t, ResourceId> m_BaseColorOverrides;
  QMap<uint32_t, QList<ResourceId>> m_ExtraTextures;

  // snapshot on the UI thread in start()
  uint32_t m_RestoreEventId = 0;
  GraphicsAPI m_API = GraphicsAPI::D3D11;
  QString m_CaptureBaseName;

  std::atomic<bool> m_Cancel{false};
  bool m_Running = false;

  LambdaThread *m_Thread = NULL;
};
