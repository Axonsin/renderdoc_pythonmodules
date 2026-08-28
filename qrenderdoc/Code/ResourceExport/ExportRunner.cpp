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

#include "ExportRunner.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include "Code/Interface/QRDInterface.h"
#include "Code/QRDUtils.h"
#include "Code/ResourceExport/GltfWriter.h"
#include "Code/ResourceExport/MeshExtractor.h"
#include "Code/ResourceExport/ObjWriter.h"
#include "Code/ResourceExport/TextureExporter.h"

ExportRunner::ExportRunner(ICaptureContext &ctx, QObject *parent) : QObject(parent), m_Ctx(ctx)
{
}

ExportRunner::~ExportRunner()
{
  if(m_Thread)
  {
    m_Thread->wait();
    delete m_Thread;
  }
}

void ExportRunner::start(rdcarray<ActionDescription> actions, const ExportSettings &settings,
                         const QMap<uint32_t, ResourceId> &baseColorOverrides,
                         const QMap<uint32_t, QList<ResourceId>> &extraTextures)
{
  if(m_Running)
    return;

  if(m_Thread)
  {
    m_Thread->wait();
    delete m_Thread;
    m_Thread = NULL;
  }

  m_Actions = actions;
  m_Settings = settings;
  m_BaseColorOverrides = baseColorOverrides;
  m_ExtraTextures = extraTextures;

  // snapshot everything the worker needs from UI-owned state up front
  m_RestoreEventId = m_Ctx.CurEvent();
  m_API = m_Ctx.APIProps().pipelineType;
  QString captureFile = m_Ctx.GetCaptureFilename();

  m_CaptureBaseName = QFileInfo(captureFile).completeBaseName();
  m_CaptureBaseName.remove(QRegularExpression(QLatin1String("[^\\w\\-. ]")));

  m_Cancel.store(false);
  m_Running = true;

  m_Thread = new LambdaThread([this]() { run(); });
  m_Thread->start();
}

void ExportRunner::cancel()
{
  m_Cancel.store(true);
}

void ExportRunner::wait()
{
  if(m_Thread)
    m_Thread->wait();
}

namespace
{
QString actionName(const ActionDescription &a)
{
  if(!a.customName.empty())
    return a.customName;
  return QStringLiteral("E%1").arg(a.eventId);
}

QString meshBaseName(const ActionDescription &a)
{
  QString name = QStringLiteral("E%1_").arg(a.eventId) +
                 (a.customName.empty() ? QStringLiteral("Draw") : QString(a.customName));
  return ObjWriter::sanitiseName(name);
}
}    // namespace

void ExportRunner::run()
{
  int okCount = 0, failCount = 0;
  bool cancelled = false;

  QDir outputDir(m_Settings.outputDir);
  outputDir.mkpath(QStringLiteral("."));

  bool isGLTF = (m_Settings.fileFormat == ExportFileFormat::GLB ||
                 m_Settings.fileFormat == ExportFileFormat::GLTF);
  bool merge = m_Settings.mergeIntoSingleScene;

  // texture dedupe across events: resource -> unique basename (no extension)
  QMap<ResourceId, QString> texBaseNames;
  QList<GltfWriter::MeshEntry> mergedMeshes;
  QList<ExportMeshData> mergedData;
  // mergedMeshes holds pointers into mergedData, so allocate it up front
  mergedData.reserve(int(m_Actions.size()));
  QList<GltfWriter::TextureEntry> mergedTextures;
  QMap<ResourceId, QString> mergedTexRels;
  ObjWriter objWriter;
  QString objErr;

  if(merge && !isGLTF)
    objWriter.open(outputDir.filePath(m_CaptureBaseName + QStringLiteral(".obj")),
                   m_CaptureBaseName + QStringLiteral(".mtl"), objErr);

  // returns the saved file's basename (no extension), or empty on failure
  auto saveTexture = [&](ResourceId id, QString &err) -> QString {
    auto it = texBaseNames.find(id);
    if(it != texBaseNames.end())
      return it.value();

    QString base = TextureExporter::TextureBaseName(m_Ctx, id);
    QString unique = base;
    int suffix = 2;
    QStringList taken;
    for(const QString &v : texBaseNames.values())
      taken << v;
    while(taken.contains(unique))
      unique = base + QStringLiteral("_%1").arg(suffix++);
    texBaseNames[id] = unique;

    QString ext = TextureExporter::ExtensionFor(m_Settings.texFormat);

    bool ok = false;
    if(m_Settings.fileFormat == ExportFileFormat::GLB)
    {
      // GLB embeds the encoded image - save to a temp file and read it back
      QString tmp = outputDir.filePath(QStringLiteral(".export_tmp_") + unique + QLatin1Char('.') +
                                       ext);
      m_Ctx.Replay().BlockInvoke(
          [&](IReplayController *r) { ok = TextureExporter::SaveTexture(r, id, m_Settings.texFormat, m_Settings.jpegQuality, tmp, err); });
      if(ok)
      {
        QFile f(tmp);
        if(f.open(QIODevice::ReadOnly))
        {
          QByteArray data = f.readAll();
          f.close();
          QFile::remove(tmp);
          GltfWriter::TextureEntry t;
          t.id = id;
          t.embeddedData = data;
          t.mimeType = TextureExporter::MimeTypeFor(m_Settings.texFormat);
          mergedTextures.push_back(t);
          mergedTexRels[id] = unique + QLatin1Char('.') + ext;
          return unique;
        }
        QFile::remove(tmp);
        err = QObject::tr("Failed to read back temporary texture file");
      }
      texBaseNames.remove(id);
      return QString();
    }
    else
    {
      QString path = outputDir.filePath(unique + QLatin1Char('.') + ext);
      m_Ctx.Replay().BlockInvoke(
          [&](IReplayController *r) { ok = TextureExporter::SaveTexture(r, id, m_Settings.texFormat, m_Settings.jpegQuality, path, err); });
      if(!ok)
      {
        texBaseNames.remove(id);
        return QString();
      }

      if(isGLTF)
      {
        GltfWriter::TextureEntry t;
        t.id = id;
        t.relativePath = unique + QLatin1Char('.') + ext;
        t.mimeType = TextureExporter::MimeTypeFor(m_Settings.texFormat);
        mergedTextures.push_back(t);
      }
      mergedTexRels[id] = unique + QLatin1Char('.') + ext;
      return unique;
    }
  };

  for(size_t i = 0; i < m_Actions.size(); i++)
  {
    if(m_Cancel.load())
    {
      cancelled = true;
      break;
    }

    const ActionDescription &action = m_Actions[i];

    emit progress(int(i), int(m_Actions.size()), actionName(action));

    if(!(action.flags & ActionFlags::Drawcall) || (action.flags & ActionFlags::MeshDispatch))
    {
      emit itemResult(action.eventId, false,
                      QObject::tr("Event is not a vertex-shader draw call"));
      failCount++;
      continue;
    }

    ExportMeshData mesh;
    mesh.eventId = action.eventId;
    mesh.name = actionName(action);

    QString extractErr;
    QList<ExportTextureCandidate> texCandidates;
    bool extracted = false;

    m_Ctx.Replay().BlockInvoke([&](IReplayController *r) {
      // switch the controller to this event for the fetches below; restored
      // after the whole run
      r->SetFrameEvent(action.eventId, true);
      const PipeState &pipe = r->GetPipelineState();

      if(m_Settings.meshSource == ExportMeshSource::VSInput)
        extracted = MeshExtractor::ExtractVSInput(r, action, pipe, m_API, m_Settings, mesh,
                                                  extractErr);
      else
        extracted = MeshExtractor::ExtractVSOutput(r, action, pipe, m_API, m_Settings, mesh,
                                                   extractErr);

      if(extracted)
        texCandidates = TextureExporter::CollectPixelTextures(m_Ctx, pipe);
    });

    if(!extracted)
    {
      emit itemResult(action.eventId, false, extractErr);
      failCount++;
      continue;
    }

    // resolve which texture is the base colour: manual override, else heuristic
    ResourceId baseColor = m_BaseColorOverrides.value(action.eventId);
    if(baseColor == ResourceId())
      baseColor = TextureExporter::GuessBaseColor(texCandidates);

    // the extra textures the user ticked for this event
    QList<ResourceId> texIds;
    if(baseColor != ResourceId())
      texIds.push_back(baseColor);
    for(const ResourceId &id : m_ExtraTextures.value(action.eventId))
    {
      if(!texIds.contains(id))
        texIds.push_back(id);
    }

    QString texErr;
    for(const ResourceId &id : texIds)
    {
      QString err;
      if(saveTexture(id, err).isEmpty())
        texErr += (texErr.isEmpty() ? QString() : QStringLiteral("; ")) + err;
    }

    if(!texErr.isEmpty())
    {
      mesh.warning += (mesh.warning.isEmpty() ? QString() : QStringLiteral("; ")) +
                      QObject::tr("texture export: %1").arg(texErr);
    }

    // ---- write this mesh --------------------------------------------------

    QString info = QObject::tr("%1 vertices, %2 triangles")
                       .arg(mesh.numVertices)
                       .arg(mesh.indices.size() / 3);

    bool written = false;
    QString writeErr;

    if(merge && isGLTF)
    {
      mergedData.push_back(mesh);
      GltfWriter::MeshEntry e;
      e.mesh = &mergedData.back();
      e.baseColorTex = baseColor;
      mergedMeshes.push_back(e);
      written = true;
    }
    else if(merge)
    {
      QString mapKd;
      if(baseColor != ResourceId() && mergedTexRels.contains(baseColor))
        mapKd = mergedTexRels[baseColor];
      objWriter.addMesh(mesh, mesh.name, mapKd);
      written = true;
    }
    else if(isGLTF)
    {
      QString ext = (m_Settings.fileFormat == ExportFileFormat::GLB) ?
                        QStringLiteral("glb") :
                        QStringLiteral("gltf");
      QString path = outputDir.filePath(meshBaseName(action) + QLatin1Char('.') + ext);

      QList<GltfWriter::MeshEntry> entries;
      GltfWriter::MeshEntry e;
      e.mesh = &mesh;
      e.baseColorTex = baseColor;
      entries.push_back(e);

      written = GltfWriter::Write(path, entries, mergedTextures, writeErr);
    }
    else
    {
      QString path = outputDir.filePath(meshBaseName(action) + QStringLiteral(".obj"));
      ObjWriter w;
      QString mapKd;
      if(baseColor != ResourceId() && mergedTexRels.contains(baseColor))
        mapKd = mergedTexRels[baseColor];
      if(w.open(path, meshBaseName(action) + QStringLiteral(".mtl"), writeErr))
      {
        w.addMesh(mesh, mesh.name, mapKd);
        written = w.finish(writeErr);
      }
    }

    if(!written)
    {
      emit itemResult(action.eventId, false, writeErr);
      failCount++;
      continue;
    }

    QString message = info;
    if(!mesh.warning.isEmpty())
      message += QStringLiteral(" - ") + mesh.warning;
    if(mesh.instanced)
      message += QObject::tr(" - instanced draw, instance 0 only");

    emit itemResult(action.eventId, true, message);
    okCount++;
  }

  // ---- finish merged outputs ---------------------------------------------

  if(merge && isGLTF && mergedMeshes.size() > 0 && !cancelled)
  {
    emit progress(int(m_Actions.size()), int(m_Actions.size()), QObject::tr("writing merged scene"));

    QString ext = (m_Settings.fileFormat == ExportFileFormat::GLB) ?
                      QStringLiteral("glb") :
                      QStringLiteral("gltf");
    QString path = outputDir.filePath(m_CaptureBaseName + QStringLiteral("_merged.") + ext);

    QString writeErr;
    if(!GltfWriter::Write(path, mergedMeshes, mergedTextures, writeErr))
    {
      emit itemResult(0, false, QObject::tr("merged scene: %1").arg(writeErr));
      failCount++;
    }
    else
    {
      emit itemResult(0, true,
                      QObject::tr("merged scene with %1 meshes written").arg(mergedMeshes.size()));
    }
  }

  if(merge && !isGLTF && !cancelled)
  {
    emit progress(int(m_Actions.size()), int(m_Actions.size()), QObject::tr("writing merged OBJ"));
    if(!objWriter.finish(objErr))
    {
      emit itemResult(0, false, QObject::tr("merged OBJ: %1").arg(objErr));
      failCount++;
    }
    else
    {
      emit itemResult(0, true, QObject::tr("merged OBJ written"));
    }
  }

  // ---- restore the replay controller to the UI's current event -----------

  m_Ctx.Replay().BlockInvoke(
      [this](IReplayController *r) { r->SetFrameEvent(m_RestoreEventId, true); });

  m_Running = false;

  emit finished(okCount, failCount, cancelled);
  emit progress(int(m_Actions.size()), int(m_Actions.size()), QString());
}
