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

#include "TextureExporter.h"
#include <QRegularExpression>
#include "Code/Interface/QRDInterface.h"

namespace
{
bool isBaseColorName(const QString &name)
{
  if(name.isEmpty())
    return false;
  static QRegularExpression re(QLatin1String(
      "diffuse|albedo|base[ _-]?colou?r|colou?r[ _-]?map|map[ _-]?kd"),
      QRegularExpression::CaseInsensitiveOption);
  return re.match(name).hasMatch();
}
}    // namespace

QList<ExportTextureCandidate> TextureExporter::CollectPixelTextures(ICaptureContext &ctx,
                                                                   const PipeState &pipe)
{
  QList<ExportTextureCandidate> ret;

  rdcarray<UsedDescriptor> resources = pipe.GetReadOnlyResources(ShaderStage::Pixel, true);

  const ShaderReflection *reflection = pipe.GetShaderReflection(ShaderStage::Pixel);

  for(const UsedDescriptor &u : resources)
  {
    ResourceId id = u.descriptor.resource;
    if(id == ResourceId())
      continue;

    const TextureDescription *tex = ctx.GetTexture(id);
    if(!tex)
      continue;

    ExportTextureCandidate c;
    c.resourceId = id;
    c.name = ctx.GetResourceName(id);
    // dimension is a plain 1/2/3 count, 2 covers 2D and 2DMS textures
    c.is2D = (tex->dimension == 2);

    // the bind name (if any) comes from the reflection entry the descriptor
    // access points at
    if(reflection && u.access.index != DescriptorAccess::NoShaderBinding &&
       u.access.index < reflection->readOnlyResources.count())
    {
      c.bindName = reflection->readOnlyResources[u.access.index].name;
    }

    c.likelyBaseColor = isBaseColorName(c.name) || isBaseColorName(c.bindName);

    // dedupe by resource
    bool dup = false;
    for(const ExportTextureCandidate &existing : ret)
    {
      if(existing.resourceId == id)
        dup = true;
    }
    if(!dup)
      ret.push_back(c);
  }

  return ret;
}

ResourceId TextureExporter::GuessBaseColor(const QList<ExportTextureCandidate> &candidates)
{
  // prefer an explicitly 2D texture with base-colour-ish naming, then any
  // named candidate, then the first 2D texture
  for(const ExportTextureCandidate &c : candidates)
  {
    if(c.likelyBaseColor && c.is2D)
      return c.resourceId;
  }
  for(const ExportTextureCandidate &c : candidates)
  {
    if(c.likelyBaseColor)
      return c.resourceId;
  }
  for(const ExportTextureCandidate &c : candidates)
  {
    if(c.is2D)
      return c.resourceId;
  }
  return ResourceId();
}

bool TextureExporter::SaveTexture(IReplayController *r, ResourceId id, ExportTextureFormat fmt,
                                  int jpegQuality, const QString &path, QString &err)
{
  TextureSave save;
  save.resourceId = id;
  save.destType = (fmt == ExportTextureFormat::JPG) ? FileType::JPG : FileType::PNG;
  save.mip = 0;
  save.alpha = AlphaMapping::Preserve;
  save.jpegQuality = qBound(1, jpegQuality, 100);

  ResultDetails result = r->SaveTexture(save, path.toUtf8().data());
  if(!result.OK())
  {
    err = result.Message();
    return false;
  }

  return true;
}

QString TextureExporter::ExtensionFor(ExportTextureFormat fmt)
{
  return (fmt == ExportTextureFormat::JPG) ? QStringLiteral("jpg") : QStringLiteral("png");
}

QString TextureExporter::MimeTypeFor(ExportTextureFormat fmt)
{
  return (fmt == ExportTextureFormat::JPG) ? QStringLiteral("image/jpeg") :
                                             QStringLiteral("image/png");
}

QString TextureExporter::TextureBaseName(ICaptureContext &ctx, ResourceId id)
{
  QString name = ctx.GetResourceName(id);

  name.remove(QRegularExpression(QLatin1String("[^\\w\\-.]")));
  name.replace(QRegularExpression(QLatin1String("[\\s]+")), QStringLiteral("_"));
  if(name.isEmpty())
    name = QStringLiteral("texture");

  // callers are responsible for de-duplicating same-named resources
  return name;
}
