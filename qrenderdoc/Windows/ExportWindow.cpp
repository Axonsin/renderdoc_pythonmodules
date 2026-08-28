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

#include "ExportWindow.h"
#include <QDir>
#include <QFileInfo>
#include <functional>
#include "Code/QRDUtils.h"
#include "Code/ResourceExport/ExportRunner.h"
#include "Code/ResourceExport/MeshExtractor.h"
#include "Code/ResourceExport/TextureExporter.h"
#include "ui_ExportWindow.h"

static const int EIDRole = Qt::UserRole;

// role used to stash the ResourceId on texture list / combo items
static const int TexRole = Qt::UserRole + 1;

ExportWindow::ExportWindow(ICaptureContext &ctx, QWidget *parent)
    : QFrame(parent), ui(new Ui::ExportWindow), m_Ctx(ctx)
{
  ui->setupUi(this);

  ui->fileFormat->addItem(tr("glTF binary (.glb)"), int(ExportFileFormat::GLB));
  ui->fileFormat->addItem(tr("glTF separate files (.gltf)"), int(ExportFileFormat::GLTF));
  ui->fileFormat->addItem(tr("OBJ + MTL"), int(ExportFileFormat::OBJ));

  ui->meshSource->addItem(tr("Vertex shader input"), int(ExportMeshSource::VSInput));
  ui->meshSource->addItem(tr("Vertex shader output"), int(ExportMeshSource::VSOutput));

  ui->uvFlip->addItem(tr("Auto (flip for OpenGL)"), int(ExportUVFlipMode::Auto));
  ui->uvFlip->addItem(tr("Always flip"), int(ExportUVFlipMode::On));
  ui->uvFlip->addItem(tr("Never flip"), int(ExportUVFlipMode::Off));

  ui->winding->addItem(tr("Auto (normalise to CCW)"), int(ExportWindingMode::Auto));
  ui->winding->addItem(tr("Keep as-is"), int(ExportWindingMode::Keep));
  ui->winding->addItem(tr("Flip"), int(ExportWindingMode::Flip));

  ui->axisMap->addItem(tr("None"), int(ExportAxisMode::NoChange));
  ui->axisMap->addItem(tr("Z-up to Y-up"), int(ExportAxisMode::ZupToYup));
  ui->axisMap->addItem(tr("Y-up to Z-up"), int(ExportAxisMode::YupToZup));

  ui->texFormat->addItem(tr("PNG"), int(ExportTextureFormat::PNG));
  ui->texFormat->addItem(tr("JPG"), int(ExportTextureFormat::JPG));
  ui->jpegQuality->setVisible(false);

  ui->log->setMaximumBlockCount(1000);

  m_Runner = new ExportRunner(m_Ctx, this);
  QObject::connect(m_Runner, &ExportRunner::progress, this, &ExportWindow::exportProgress);
  QObject::connect(m_Runner, &ExportRunner::itemResult, this, &ExportWindow::exportItemResult);
  QObject::connect(m_Runner, &ExportRunner::finished, this, &ExportWindow::exportFinished);

  setControlsEnabled(false);

  m_Ctx.AddCaptureViewer(this);
}

ExportWindow::~ExportWindow()
{
  if(m_Runner->isRunning())
  {
    m_Runner->cancel();
    m_Runner->wait();
  }

  m_Ctx.BuiltinWindowClosed(this);
  m_Ctx.RemoveCaptureViewer(this);
  delete ui;
}

void ExportWindow::OnCaptureLoaded()
{
  // default the output directory next to the capture
  if(ui->outputDir->text().isEmpty())
  {
    QFileInfo info(m_Ctx.GetCaptureFilename());
    ui->outputDir->setText(info.dir().filePath(QStringLiteral("export")));
  }

  populateEventList();
  setControlsEnabled(true);
  ui->log->clear();
  ui->progressBar->setValue(0);
}

void ExportWindow::OnCaptureClosed()
{
  m_BaseColorOverrides.clear();
  m_ExtraTextures.clear();
  ui->eventList->clear();
  ui->detailsLabel->setText(tr("No capture loaded"));
  ui->baseColorCombo->clear();
  ui->textureList->clear();
  ui->progressBar->setValue(0);
  setControlsEnabled(false);
}

void ExportWindow::OnSelectedEventChanged(uint32_t eventId) {}

void ExportWindow::OnEventChanged(uint32_t eventId)
{
  // keep the list in sync when the event changes elsewhere (e.g. event browser)
  if(m_SyncingSelection)
    return;

  for(int i = 0; i < ui->eventList->topLevelItemCount(); i++)
  {
    QTreeWidgetItem *item = ui->eventList->topLevelItem(i);
    if(uint32_t(item->data(0, EIDRole).toUInt()) == eventId)
    {
      m_SyncingSelection = true;
      ui->eventList->scrollToItem(item, QAbstractItemView::PositionAtCenter);
      item->setSelected(true);
      m_SyncingSelection = false;
      break;
    }
  }

  updateDetailsPanel();
  updateMaterialPanel();
}

void ExportWindow::on_browse_clicked()
{
  QString dir = RDDialog::getExistingDirectory(this, tr("Choose export directory"),
                                               ui->outputDir->text());
  if(!dir.isEmpty())
    ui->outputDir->setText(dir);
}

void ExportWindow::on_fileFormat_currentIndexChanged(int index)
{
  Q_UNUSED(index);
  ExportFileFormat fmt =
      (ExportFileFormat)ui->fileFormat->currentData().toInt();
  // OBJ always writes texture files alongside, merging is still useful (groups)
  ui->mergeScene->setEnabled(true);
  on_texFormat_currentIndexChanged(ui->texFormat->currentIndex());
}

void ExportWindow::on_meshSource_currentIndexChanged(int index)
{
  Q_UNUSED(index);
  updateDetailsPanel();
}

void ExportWindow::on_eventFilter_textChanged(const QString &text) { populateEventList(); }

void ExportWindow::on_drawsOnly_toggled(bool checked) { populateEventList(); }

void ExportWindow::on_eventList_itemSelectionChanged()
{
  if(m_SyncingSelection)
    return;

  QList<QTreeWidgetItem *> sel = ui->eventList->selectedItems();
  if(sel.size() == 1)
  {
    // selecting an event here behaves like clicking it in the event browser:
    // the whole UI (pipeline state etc) syncs to it, and our detail panels
    // then read from the (now valid) current pipeline state
    uint32_t eid = sel[0]->data(0, EIDRole).toUInt();
    m_SyncingSelection = true;
    m_Ctx.SetEventID({}, eid, eid);
    m_SyncingSelection = false;
  }

  updateDetailsPanel();
  updateMaterialPanel();
}

void ExportWindow::on_baseColorCombo_currentIndexChanged(int index)
{
  // single-selection detail panel: record the explicit override (or clear it)
  QList<QTreeWidgetItem *> sel = ui->eventList->selectedItems();
  if(sel.size() != 1)
    return;

  uint32_t eid = sel[0]->data(0, EIDRole).toUInt();

  if(index <= 0)
    m_BaseColorOverrides.remove(eid);
  else
    m_BaseColorOverrides[eid] = ui->baseColorCombo->currentData().value<ResourceId>();
}

void ExportWindow::on_textureList_itemChanged(QListWidgetItem *item)
{
  if(m_FillingTextureList || item == NULL)
    return;

  QList<QTreeWidgetItem *> sel = ui->eventList->selectedItems();
  if(sel.size() != 1)
    return;

  uint32_t eid = sel[0]->data(0, EIDRole).toUInt();
  ResourceId id = item->data(TexRole).value<ResourceId>();

  QList<ResourceId> extras = m_ExtraTextures.value(eid);
  extras.removeAll(id);
  if(item->checkState() == Qt::Checked)
    extras.push_back(id);
  m_ExtraTextures[eid] = extras;
}

void ExportWindow::on_texFormat_currentIndexChanged(int index)
{
  Q_UNUSED(index);
  ui->jpegQuality->setVisible(
      (ExportTextureFormat)ui->texFormat->currentData().toInt() == ExportTextureFormat::JPG);
}

void ExportWindow::on_exportButton_clicked()
{
  if(m_Runner->isRunning())
    return;

  QList<uint32_t> eventIds;
  for(QTreeWidgetItem *item : ui->eventList->selectedItems())
    eventIds.push_back(item->data(0, EIDRole).toUInt());

  if(eventIds.isEmpty())
  {
    ui->log->appendPlainText(tr("Select at least one event to export."));
    return;
  }

  if(ui->outputDir->text().isEmpty())
  {
    ui->log->appendPlainText(tr("Choose an output directory first."));
    return;
  }

  ExportSettings settings;
  settings.outputDir = ui->outputDir->text();
  settings.fileFormat = (ExportFileFormat)ui->fileFormat->currentData().toInt();
  settings.meshSource = (ExportMeshSource)ui->meshSource->currentData().toInt();
  settings.uvFlip = (ExportUVFlipMode)ui->uvFlip->currentData().toInt();
  settings.winding = (ExportWindingMode)ui->winding->currentData().toInt();
  settings.axis = (ExportAxisMode)ui->axisMap->currentData().toInt();
  settings.texFormat = (ExportTextureFormat)ui->texFormat->currentData().toInt();
  settings.jpegQuality = ui->jpegQuality->value();
  settings.mergeIntoSingleScene = ui->mergeScene->isChecked();

  // snapshot the actions on the UI thread
  rdcarray<ActionDescription> actions;
  for(uint32_t eid : eventIds)
  {
    const ActionDescription *action = m_Ctx.GetAction(eid);
    if(action)
      actions.push_back(*action);
  }

  if(actions.empty())
  {
    ui->log->appendPlainText(tr("No valid actions found for the selection."));
    return;
  }

  // only keep overrides/extras for events that are actually selected
  QMap<uint32_t, ResourceId> baseColors;
  for(uint32_t eid : eventIds)
  {
    if(m_BaseColorOverrides.contains(eid))
      baseColors[eid] = m_BaseColorOverrides[eid];
  }
  QMap<uint32_t, QList<ResourceId>> extras;
  for(uint32_t eid : eventIds)
  {
    if(m_ExtraTextures.contains(eid))
      extras[eid] = m_ExtraTextures[eid];
  }

  ui->log->appendPlainText(tr("Exporting %n event(s) to %1 ...", "", int(eventIds.size()))
                               .arg(settings.outputDir));
  ui->progressBar->setRange(0, int(eventIds.size()));
  ui->progressBar->setValue(0);
  ui->exportButton->setEnabled(false);
  ui->cancelButton->setEnabled(true);

  m_Runner->start(actions, settings, baseColors, extras);
}

void ExportWindow::on_cancelButton_clicked() { m_Runner->cancel(); }

void ExportWindow::exportProgress(int done, int total, const QString &stage)
{
  ui->progressBar->setRange(0, qMax(1, total));
  ui->progressBar->setValue(done);
  if(!stage.isEmpty())
    ui->progressBar->setFormat(tr("%p% - %1").arg(stage));
  else
    ui->progressBar->setFormat(QStringLiteral("%p%"));
}

void ExportWindow::exportItemResult(uint32_t eventId, bool ok, const QString &message)
{
  QString prefix = ok ? tr("[OK]") : tr("[FAIL]");
  QString eid = eventId > 0 ? (QStringLiteral("E%1 ").arg(eventId)) : QString();
  ui->log->appendPlainText(prefix + QStringLiteral(" ") + eid + message);
}

void ExportWindow::exportFinished(int okCount, int failCount, bool cancelled)
{
  ui->exportButton->setEnabled(true);
  ui->cancelButton->setEnabled(false);
  ui->progressBar->setFormat(QStringLiteral("%p%"));

  QString summary;
  if(cancelled)
    summary = tr("Export cancelled: %1 succeeded, %2 failed.").arg(okCount).arg(failCount);
  else
    summary = tr("Export finished: %1 succeeded, %2 failed.").arg(okCount).arg(failCount);
  ui->log->appendPlainText(summary);
}

void ExportWindow::populateEventList()
{
  if(!m_Ctx.IsCaptureLoaded())
    return;

  QString filter = ui->eventFilter->text();
  bool drawsOnly = ui->drawsOnly->isChecked();

  ui->eventList->clear();

  std::function<void(const rdcarray<ActionDescription> &)> walk =
      [&](const rdcarray<ActionDescription> &actions) {
        for(const ActionDescription &a : actions)
        {
          bool isDraw = bool(a.flags & (ActionFlags::Drawcall | ActionFlags::MeshDispatch));

          if(drawsOnly && !isDraw)
          {
            if(!a.children.empty())
              walk(a.children);
            continue;
          }

          QString name;
          if(!a.customName.empty())
            name = a.customName;
          else if(isDraw)
            name = tr("Draw");
          else if(a.flags & ActionFlags::SetMarker)
            name = tr("Marker");
          else
            name = tr("Action");

          if(!filter.isEmpty() && !name.contains(filter, Qt::CaseInsensitive) &&
             !QString::number(a.eventId).contains(filter))
          {
            if(!a.children.empty())
              walk(a.children);
            continue;
          }

          QStringList cols;
          cols << QString::number(a.eventId) << name;
          QTreeWidgetItem *item = new QTreeWidgetItem(ui->eventList, cols);
          item->setData(0, EIDRole, a.eventId);
          item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);

          if(!a.children.empty())
            walk(a.children);
        }
      };

  walk(m_Ctx.CurRootActions());

  ui->eventList->resizeColumnToContents(0);
}

void ExportWindow::updateDetailsPanel()
{
  if(!m_Ctx.IsCaptureLoaded())
  {
    ui->detailsLabel->setText(tr("No capture loaded"));
    return;
  }

  QList<QTreeWidgetItem *> sel = ui->eventList->selectedItems();

  if(sel.size() == 0)
  {
    ui->detailsLabel->setText(tr("No event selected"));
    return;
  }

  if(sel.size() > 1)
  {
    ui->detailsLabel->setText(
        tr("%1 events selected.\nBase colour textures will be guessed per event "
           "(name-based heuristic), manual overrides apply to single selections.")
            .arg(sel.size()));
    return;
  }

  uint32_t eid = sel[0]->data(0, EIDRole).toUInt();
  const ActionDescription *action = m_Ctx.GetAction(eid);
  if(!action)
  {
    ui->detailsLabel->setText(tr("Event not found"));
    return;
  }

  // OnEventChanged keeps the app's current event in sync with the selection,
  // so CurPipelineState() describes the selected event here
  const PipeState &pipe = m_Ctx.CurPipelineState();

  ExportMeshSource source = (ExportMeshSource)ui->meshSource->currentData().toInt();
  QStringList attributes = (source == ExportMeshSource::VSInput) ?
                               MeshExtractor::PreviewVSInputAttributes(pipe) :
                               MeshExtractor::PreviewVSOutputAttributes(pipe);

  QString topology = tr("Unknown");
  Topology topo = pipe.GetPrimitiveTopology();
  switch(topo)
  {
    case Topology::TriangleList: topology = tr("Triangle List"); break;
    case Topology::TriangleStrip: topology = tr("Triangle Strip"); break;
    case Topology::TriangleFan: topology = tr("Triangle Fan"); break;
    case Topology::PatchList: topology = tr("Patch List"); break;
    case Topology::LineList: topology = tr("Line List"); break;
    case Topology::LineStrip: topology = tr("Line Strip"); break;
    case Topology::PointList: topology = tr("Point List"); break;
    default: break;
  }

  QString html = QStringLiteral("<b>%1</b><br>").arg(action->customName.empty() ?
                                                         tr("Draw") :
                                                         QString(action->customName));
  html += tr("Topology: %1<br>").arg(topology);
  html += tr("Vertices/Indices: %1<br>").arg(action->numIndices);
  if(action->flags & ActionFlags::Instanced)
    html += tr("Instances: %1 (instance 0 exported)<br>").arg(action->numInstances);
  if(!attributes.isEmpty())
    html += tr("Attributes: %1").arg(attributes.join(QStringLiteral(", ")));

  ui->detailsLabel->setText(html);
}

void ExportWindow::updateMaterialPanel()
{
  m_FillingTextureList = true;

  ui->baseColorCombo->clear();
  ui->textureList->clear();

  QList<QTreeWidgetItem *> sel = ui->eventList->selectedItems();

  if(sel.size() == 1 && m_Ctx.IsCaptureLoaded())
  {
    uint32_t eid = sel[0]->data(0, EIDRole).toUInt();

    QList<ExportTextureCandidate> candidates =
        TextureExporter::CollectPixelTextures(m_Ctx, m_Ctx.CurPipelineState());
    ResourceId guess = TextureExporter::GuessBaseColor(candidates);

    ui->baseColorCombo->addItem(tr("Auto (%1)").arg(guess == ResourceId() ?
                                                        tr("no guess") :
                                                        tr("guessed from names")),
                                QVariant::fromValue(ResourceId()));

    int currentIndex = 0;
    for(const ExportTextureCandidate &c : candidates)
    {
      QString label = c.name;
      if(!c.bindName.isEmpty() && c.bindName != label)
        label += QStringLiteral(" (") + c.bindName + QStringLiteral(")");
      if(!c.is2D)
        label += tr(" [not 2D]");

      ui->baseColorCombo->addItem(label, QVariant::fromValue(c.resourceId));

      QListWidgetItem *item = new QListWidgetItem(label, ui->textureList);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setData(TexRole, QVariant::fromValue(c.resourceId));
      item->setCheckState(Qt::Unchecked);
      ui->textureList->addItem(item);

      // reflect the effective base colour selection in the checkboxes
      ResourceId effective = m_BaseColorOverrides.value(eid);
      if(effective == ResourceId())
        effective = guess;
      if(c.resourceId == effective)
        item->setCheckState(Qt::Checked);
    }

    // reflect a stored override in the combo
    if(m_BaseColorOverrides.contains(eid))
    {
      int idx = ui->baseColorCombo->findData(QVariant::fromValue(m_BaseColorOverrides[eid]));
      if(idx > 0)
        ui->baseColorCombo->setCurrentIndex(idx);
    }

    // tick extra textures the user previously enabled for this event
    const QList<ResourceId> extras = m_ExtraTextures.value(eid);
    for(int i = 0; i < ui->textureList->count(); i++)
    {
      QListWidgetItem *item = ui->textureList->item(i);
      if(item->checkState() == Qt::Checked)
        continue;
      if(extras.contains(item->data(TexRole).value<ResourceId>()))
        item->setCheckState(Qt::Checked);
    }

    Q_UNUSED(eid);
  }
  else if(sel.size() > 1)
  {
    ui->baseColorCombo->addItem(tr("Heuristic per event"), QVariant::fromValue(ResourceId()));
  }
  else
  {
    ui->baseColorCombo->addItem(tr("No event selected"), QVariant::fromValue(ResourceId()));
  }

  m_FillingTextureList = false;
}

void ExportWindow::setControlsEnabled(bool enabled)
{
  ui->exportButton->setEnabled(enabled);
  ui->eventList->setEnabled(enabled);
  ui->eventFilter->setEnabled(enabled);
  ui->drawsOnly->setEnabled(enabled);
  ui->outputDir->setEnabled(enabled);
  ui->browse->setEnabled(enabled);
  if(!enabled)
    ui->cancelButton->setEnabled(false);
}
