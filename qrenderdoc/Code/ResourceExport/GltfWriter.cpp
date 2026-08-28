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

#include "GltfWriter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

// glTF component types
static const int GLTF_COMPONENT_FLOAT = 5126;
static const int GLTF_COMPONENT_USHORT = 5123;
static const int GLTF_COMPONENT_UINT = 5125;

// glTF bufferView targets
static const int GLTF_TARGET_ARRAY_BUFFER = 34962;
static const int GLTF_TARGET_ELEMENT_ARRAY_BUFFER = 34963;

namespace
{
struct Builder
{
  QByteArray bin;
  QJsonArray bufferViews;
  QJsonArray accessors;

  // appends data into the single bin buffer, 4-byte aligned, and returns the
  // bufferView index
  int addBufferView(const QByteArray &data, int target)
  {
    while(bin.size() % 4 != 0)
      bin.append(char(0));

    QJsonObject view;
    view.insert(QStringLiteral("buffer"), 0);
    view.insert(QStringLiteral("byteOffset"), bin.size());
    view.insert(QStringLiteral("byteLength"), data.size());
    if(target)
      view.insert(QStringLiteral("target"), target);
    bufferViews.append(view);

    bin.append(data);
    return bufferViews.size() - 1;
  }

  int addAccessor(const QJsonObject &accessor)
  {
    accessors.append(accessor);
    return accessors.size() - 1;
  }
};

QJsonObject vec3MinMax(const ExportMeshAttribute &attr)
{
  float mn[3] = {0.0f, 0.0f, 0.0f}, mx[3] = {0.0f, 0.0f, 0.0f};
  bool first = true;
  for(size_t i = 0; i + 2 < attr.data.size(); i += attr.numComponents)
  {
    for(int c = 0; c < 3; c++)
    {
      float v = attr.data[i + c];
      if(first || v < mn[c])
        mn[c] = v;
      if(first || v > mx[c])
        mx[c] = v;
    }
    first = false;
  }

  QJsonArray mnArr, mxArr;
  for(int c = 0; c < 3; c++)
  {
    mnArr.append(double(mn[c]));
    mxArr.append(double(mx[c]));
  }

  QJsonObject ret;
  ret.insert(QStringLiteral("min"), mnArr);
  ret.insert(QStringLiteral("max"), mxArr);
  return ret;
}
}    // namespace

QString GltfWriter::sanitiseName(const QString &name)
{
  QString ret = name.simplified();
  ret.remove(QRegularExpression(QLatin1String("[^\\w\\-. ]")));
  if(ret.isEmpty())
    ret = QStringLiteral("mesh");
  return ret;
}

bool GltfWriter::Write(const QString &path, const QList<MeshEntry> &meshes,
                       const QList<TextureEntry> &textures, QString &err)
{
  QFileInfo info(path);
  bool binary = (info.suffix().toLower() == QLatin1String("glb"));
  QString baseName = info.completeBaseName();

  Builder b;

  QJsonArray nodesArr, meshesArr, materialsArr;
  QJsonArray texturesArr, imagesArr;

  // ---- samplers / textures / images -------------------------------------

  // one conservative sampler (linear filtering, repeat wrapping) shared by all
  QJsonArray samplersArr;
  {
    QJsonObject s;
    s.insert(QStringLiteral("magFilter"), 9729);      // LINEAR
    s.insert(QStringLiteral("minFilter"), 9987);      // LINEAR_MIPMAP_LINEAR
    s.insert(QStringLiteral("wrapS"), 10497);         // REPEAT
    s.insert(QStringLiteral("wrapT"), 10497);         // REPEAT
    samplersArr.append(s);
  }

  QMap<ResourceId, int> textureIndices;
  for(int i = 0; i < textures.size(); i++)
  {
    const TextureEntry &t = textures[i];
    if(t.id == ResourceId())
      continue;

    QJsonObject image;
    if(binary)
    {
      if(t.embeddedData.isEmpty())
        continue;
      int bv = b.addBufferView(t.embeddedData, 0);
      image.insert(QStringLiteral("bufferView"), bv);
      image.insert(QStringLiteral("mimeType"), t.mimeType);
    }
    else
    {
      image.insert(QStringLiteral("uri"), t.relativePath);
    }
    imagesArr.append(image);

    QJsonObject tex;
    tex.insert(QStringLiteral("sampler"), 0);
    tex.insert(QStringLiteral("source"), imagesArr.size() - 1);
    texturesArr.append(tex);

    textureIndices[t.id] = texturesArr.size() - 1;
  }

  // ---- meshes -----------------------------------------------------------

  for(const MeshEntry &entry : meshes)
  {
    const ExportMeshData &mesh = *entry.mesh;

    if(mesh.indices.size() < 3 || mesh.numVertices == 0)
      continue;

    QJsonObject primitive;

    // indices - uint16 where the vertex range allows it, uint32 otherwise
    {
      bool use16 = (mesh.numVertices <= 65536);
      QByteArray idxData;
      idxData.resize(int(mesh.indices.size()) * (use16 ? 2 : 4));
      if(use16)
      {
        uint16_t *dst = (uint16_t *)idxData.data();
        for(size_t i = 0; i < mesh.indices.size(); i++)
          dst[i] = uint16_t(mesh.indices[i]);
      }
      else
      {
        memcpy(idxData.data(), mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
      }

      int bv = b.addBufferView(idxData, GLTF_TARGET_ELEMENT_ARRAY_BUFFER);
      QJsonObject acc;
      acc.insert(QStringLiteral("bufferView"), bv);
      acc.insert(QStringLiteral("componentType"), use16 ? GLTF_COMPONENT_USHORT : GLTF_COMPONENT_UINT);
      acc.insert(QStringLiteral("count"), qint64(mesh.indices.size()));
      acc.insert(QStringLiteral("type"), QStringLiteral("SCALAR"));
      primitive.insert(QStringLiteral("indices"), b.addAccessor(acc));
    }

    QJsonObject attributes;

    for(const ExportMeshAttribute &a : mesh.attributes)
    {
      bool isPosition = (a.semantic == QLatin1String("POSITION"));
      bool isNormal = (a.semantic == QLatin1String("NORMAL"));
      bool isUV = a.semantic.startsWith(QLatin1String("TEXCOORD"));
      bool isColor = a.semantic.startsWith(QLatin1String("COLOR"));
      if(!isPosition && !isNormal && !isUV && !isColor)
        continue;

      // glTF requires VEC2 for TEXCOORD_n, drop the attribute if the vertex
      // stream doesn't carry at least two components
      if(isUV && a.numComponents < 2)
        continue;

      uint32_t comps = a.numComponents;
      if(isPosition || isNormal)
        comps = qMin(3U, comps);
      if(isUV)
        comps = qMin(2U, comps);
      if(comps == 0)
        continue;

      QByteArray data;
      data.resize(int(mesh.numVertices) * int(comps) * int(sizeof(float)));
      float *dst = (float *)data.data();
      for(size_t v = 0; v < mesh.numVertices; v++)
      {
        for(uint32_t c = 0; c < comps; c++)
        {
          float val = 0.0f;
          size_t src = size_t(v) * a.numComponents + qMin(c, a.numComponents - 1);
          if(src < a.data.size())
            val = a.data[src];
          dst[v * comps + c] = val;
        }
      }

      int bv = b.addBufferView(data, GLTF_TARGET_ARRAY_BUFFER);
      QJsonObject acc;
      acc.insert(QStringLiteral("bufferView"), bv);
      acc.insert(QStringLiteral("componentType"), GLTF_COMPONENT_FLOAT);
      acc.insert(QStringLiteral("count"), qint64(mesh.numVertices));
      acc.insert(QStringLiteral("type"),
                 QStringLiteral("VEC%1").arg(comps));
      if(isPosition)
      {
        // the spec requires min/max on POSITION accessors
        QJsonObject mm = vec3MinMax(a);
        for(const QString &k : mm.keys())
          acc.insert(k, mm.value(k));
      }
      int accIdx = b.addAccessor(acc);

      QString attribName;
      if(isPosition)
        attribName = QStringLiteral("POSITION");
      else if(isNormal)
        attribName = QStringLiteral("NORMAL");
      else if(isUV)
        attribName = QStringLiteral("TEXCOORD_0");
      else
        attribName = QStringLiteral("COLOR_0");
      attributes.insert(attribName, accIdx);
    }

    if(!attributes.contains(QStringLiteral("POSITION")))
      continue;

    primitive.insert(QStringLiteral("attributes"), attributes);
    primitive.insert(QStringLiteral("mode"), 4);    // TRIANGLES

    // material: simplified PBR with an optional base colour texture
    int materialIdx = -1;
    {
      QJsonObject mat;
      QJsonObject pbr;
      if(entry.baseColorTex != ResourceId() && textureIndices.contains(entry.baseColorTex))
      {
        QJsonObject bcTex;
        bcTex.insert(QStringLiteral("index"), textureIndices[entry.baseColorTex]);
        pbr.insert(QStringLiteral("baseColorTexture"), bcTex);
      }
      pbr.insert(QStringLiteral("metallicFactor"), 1.0);
      pbr.insert(QStringLiteral("roughnessFactor"), 1.0);
      mat.insert(QStringLiteral("pbrMetallicRoughness"), pbr);
      mat.insert(QStringLiteral("doubleSided"), mesh.doubleSided);
      mat.insert(QStringLiteral("name"), sanitiseName(mesh.name));
      materialsArr.append(mat);
      materialIdx = materialsArr.size() - 1;
    }
    primitive.insert(QStringLiteral("material"), materialIdx);

    QJsonObject m;
    m.insert(QStringLiteral("name"), sanitiseName(mesh.name));
    QJsonArray prims;
    prims.append(primitive);
    m.insert(QStringLiteral("primitives"), prims);
    meshesArr.append(m);

    QJsonObject node;
    node.insert(QStringLiteral("mesh"), meshesArr.size() - 1);
    node.insert(QStringLiteral("name"), sanitiseName(mesh.name));
    nodesArr.append(node);
  }

  if(meshesArr.size() == 0)
  {
    err = QObject::tr("No exportable meshes found");
    return false;
  }

  // ---- assemble the document --------------------------------------------

  QJsonObject root;
  {
    QJsonObject asset;
    asset.insert(QStringLiteral("version"), QStringLiteral("2.0"));
    asset.insert(QStringLiteral("generator"), QStringLiteral("RenderDic"));
    root.insert(QStringLiteral("asset"), asset);

    QJsonArray scenes;
    QJsonObject scene;
    QJsonArray sceneNodes;
    for(int i = 0; i < nodesArr.size(); i++)
      sceneNodes.append(i);
    scene.insert(QStringLiteral("nodes"), sceneNodes);
    scenes.append(scene);
    root.insert(QStringLiteral("scenes"), scenes);
    root.insert(QStringLiteral("scene"), 0);

    root.insert(QStringLiteral("nodes"), nodesArr);
    root.insert(QStringLiteral("meshes"), meshesArr);
    if(materialsArr.size() > 0)
      root.insert(QStringLiteral("materials"), materialsArr);
    if(texturesArr.size() > 0)
    {
      root.insert(QStringLiteral("samplers"), samplersArr);
      root.insert(QStringLiteral("textures"), texturesArr);
      root.insert(QStringLiteral("images"), imagesArr);
    }
    root.insert(QStringLiteral("accessors"), b.accessors);
    root.insert(QStringLiteral("bufferViews"), b.bufferViews);

    QJsonObject buffer;
    buffer.insert(QStringLiteral("byteLength"), b.bin.size());
    if(!binary)
    {
      // the bin file sits next to the .gltf
      buffer.insert(QStringLiteral("uri"),
                    sanitiseName(baseName) + QStringLiteral(".bin"));
    }
    QJsonArray buffers;
    buffers.append(buffer);
    root.insert(QStringLiteral("buffers"), buffers);
  }

  QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

  if(!binary)
  {
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly))
    {
      err = QObject::tr("Failed to open %1 for writing").arg(path);
      return false;
    }
    f.write(json);
    f.close();

    QString binPath = info.dir().filePath(baseName + QStringLiteral(".bin"));
    QFile bf(binPath);
    if(!bf.open(QIODevice::WriteOnly))
    {
      err = QObject::tr("Failed to open %1 for writing").arg(binPath);
      return false;
    }
    bf.write(b.bin);
    bf.close();

    return true;
  }

  // ---- GLB container -----------------------------------------------------

  // pad the JSON chunk with spaces and the BIN chunk with zeroes to 4 bytes
  QByteArray jsonChunk = json;
  while(jsonChunk.size() % 4 != 0)
    jsonChunk.append(char(0x20));
  QByteArray binChunk = b.bin;
  while(binChunk.size() % 4 != 0)
    binChunk.append(char(0));

  uint32_t totalLength = 12 + 8 + uint32_t(jsonChunk.size()) + 8 + uint32_t(binChunk.size());

  QFile f(path);
  if(!f.open(QIODevice::WriteOnly))
  {
    err = QObject::tr("Failed to open %1 for writing").arg(path);
    return false;
  }

  auto writeLE32 = [&f](uint32_t v) {
    byte b[4] = {byte(v & 0xff), byte((v >> 8) & 0xff), byte((v >> 16) & 0xff), byte((v >> 24) & 0xff)};
    f.write((const char *)b, 4);
  };

  writeLE32(0x46546C67);    // magic 'glTF'
  writeLE32(2);             // version
  writeLE32(totalLength);

  writeLE32(uint32_t(jsonChunk.size()));
  writeLE32(0x4E4F534A);    // 'JSON'
  f.write(jsonChunk);

  writeLE32(uint32_t(binChunk.size()));
  writeLE32(0x004E4942);    // 'BIN'
  f.write(binChunk);

  f.close();

  return true;
}
