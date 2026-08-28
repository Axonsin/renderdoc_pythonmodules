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

#include <QFrame>
#include <QList>
#include <QMap>
#include "Code/Interface/QRDInterface.h"

class ExportRunner;
class QListWidgetItem;
class QTreeWidgetItem;

namespace Ui
{
class ExportWindow;
}

// Event-centric resource export window: select draw calls from the current
// capture, export their meshes (glTF/OBJ) with simplified materials and the
// textures they had bound. See Code/ResourceExport/ for the export core.
class ExportWindow : public QFrame, public IExportWindow, public ICaptureViewer
{
  Q_OBJECT

public:
  explicit ExportWindow(ICaptureContext &ctx, QWidget *parent = 0);
  ~ExportWindow();

  // IExportWindow
  QWidget *Widget() override { return this; }
  // ICaptureViewer
  void OnCaptureLoaded() override;
  void OnCaptureClosed() override;
  void OnSelectedEventChanged(uint32_t eventId) override;
  void OnEventChanged(uint32_t eventId) override;

private slots:
  // automatic slots
  void on_browse_clicked();
  void on_fileFormat_currentIndexChanged(int index);
  void on_meshSource_currentIndexChanged(int index);
  void on_eventFilter_textChanged(const QString &text);
  void on_drawsOnly_toggled(bool checked);
  void on_eventList_itemSelectionChanged();
  void on_baseColorCombo_currentIndexChanged(int index);
  void on_textureList_itemChanged(QListWidgetItem *item);
  void on_texFormat_currentIndexChanged(int index);
  void on_exportButton_clicked();
  void on_cancelButton_clicked();

  // manual slots
  void exportProgress(int done, int total, const QString &stage);
  void exportItemResult(uint32_t eventId, bool ok, const QString &message);
  void exportFinished(int okCount, int failCount, bool cancelled);

private:
  void populateEventList();
  void updateDetailsPanel();
  void updateMaterialPanel();
  void setControlsEnabled(bool enabled);

  Ui::ExportWindow *ui;
  ICaptureContext &m_Ctx;

  ExportRunner *m_Runner = NULL;

  // per-event UI state (overrides applied on export)
  QMap<uint32_t, ResourceId> m_BaseColorOverrides;
  QMap<uint32_t, QList<ResourceId>> m_ExtraTextures;

  // guard against selection feedback loops when syncing with the current event
  bool m_SyncingSelection = false;
  // guard against textureList itemChanged storms while repopulating
  bool m_FillingTextureList = false;
};
