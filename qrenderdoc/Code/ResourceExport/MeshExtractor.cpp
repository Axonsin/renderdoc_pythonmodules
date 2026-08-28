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

#include "MeshExtractor.h"
#include <QCoreApplication>
#include <cmath>
#include <limits>
#include "Code/QRDUtils.h"

namespace
{
const uint32_t RESTART_SENTINEL = 0xffffffff;

// see BufferViewer::guessPositionColumn for the matching name-based heuristics.
// Returns an export semantic (POSITION, NORMAL, TANGENT, TEXCOORDn, COLORn) or
// an empty string if the attribute isn't one we know how to export.
QString classifySemantic(const QString &name)
{
  QString upper = name.trimmed().toUpper();

  // strip a trailing digit index (POSITION2, TEXCOORD0, ...)
  int digits = 0;
  while(upper.size() - digits > 0 && upper[upper.size() - digits - 1].isDigit())
    digits++;
  QString base = upper.left(upper.size() - digits);
  int semanticIndex = digits > 0 ? upper.right(digits).toInt() : 0;

  if(base == QLatin1String("POSITION") || base == QLatin1String("POS") ||
     base == QLatin1String("SV_POSITION"))
    return QStringLiteral("POSITION");
  if(base == QLatin1String("NORMAL"))
    return QStringLiteral("NORMAL");
  if(base == QLatin1String("TANGENT"))
    return QStringLiteral("TANGENT");
  if(base == QLatin1String("COLOR") || base == QLatin1String("COLOUR") ||
     base == QLatin1String("VERTCOLOR") || base == QLatin1String("VCOLOR"))
    return QStringLiteral("COLOR%1").arg(semanticIndex);
  if(base == QLatin1String("TEXCOORD") || base == QLatin1String("UV") ||
     base == QLatin1String("TEX"))
    return QStringLiteral("TEXCOORD%1").arg(semanticIndex);

  // GL/SPIR-V style names like in_Position or aPos won't match an exact base
  // name, fall back to substring matching as a second pass
  if(upper.contains(QLatin1String("POSITION")))
    return QStringLiteral("POSITION");
  if(upper.contains(QLatin1String("NORMAL")))
    return QStringLiteral("NORMAL");
  if(upper.contains(QLatin1String("TANGENT")))
    return QStringLiteral("TANGENT");
  if(upper.contains(QLatin1String("TEXCOORD")) || upper.contains(QLatin1String("UV")))
    return QStringLiteral("TEXCOORD%1").arg(semanticIndex);
  if(upper.contains(QLatin1String("COLOR")) || upper.contains(QLatin1String("COLOUR")))
    return QStringLiteral("COLOR%1").arg(semanticIndex);
  if(upper.contains(QLatin1String("POS")))
    return QStringLiteral("POSITION");

  return QString();
}

bool isExportableTopology(Topology topo)
{
  return topo == Topology::TriangleList || topo == Topology::TriangleStrip ||
         topo == Topology::TriangleFan;
}

QString topologyName(Topology topo)
{
  switch(topo)
  {
    case Topology::TriangleList: return QCoreApplication::translate("MeshExtractor", "Triangle List");
    case Topology::TriangleStrip: return QCoreApplication::translate("MeshExtractor", "Triangle Strip");
    case Topology::TriangleFan: return QCoreApplication::translate("MeshExtractor", "Triangle Fan");
    default: break;
  }
  return QCoreApplication::translate("MeshExtractor", "Unsupported");
}

// unsigned exponent/mantissa float as used by R11G11B10 - no sign bit
float decodeUnsignedFloat(uint32_t value, uint32_t expBits, uint32_t mantBits)
{
  uint32_t mant = value & ((1u << mantBits) - 1);
  uint32_t exp = (value >> mantBits) & ((1u << expBits) - 1);
  uint32_t bias = (1u << (expBits - 1)) - 1;

  if(exp == 0)
    return std::ldexp(float(mant), int(1) - int(bias) - int(mantBits));

  return std::ldexp(float(mant) / float(1u << mantBits) + 1.0f, int(exp) - int(bias));
}

float decodeHalf(uint16_t h)
{
  uint32_t sign = (h >> 15) & 1;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t mant = h & 0x3ff;

  float v;
  if(exp == 0)
    v = std::ldexp(float(mant), -24);
  else if(exp == 31)
    v = mant == 0 ? std::numeric_limits<float>::infinity() :
                    std::numeric_limits<float>::quiet_NaN();
  else
    v = std::ldexp(float(mant) / 1024.0f + 1.0f, int(exp) - 15);

  return sign ? -v : v;
}

// decodes one element of an arbitrary vertex ResourceFormat into 4 floats.
// Missing components are zero. Returns false for formats we can't decode (the
// caller skips that attribute and reports it).
bool decodeFormatComponents(const ResourceFormat &fmt, const byte *ptr, float out[4])
{
  out[0] = out[1] = out[2] = out[3] = 0.0f;

  if(fmt.Special())
  {
    switch(fmt.type)
    {
      case ResourceFormatType::R11G11B10:
      {
        uint32_t packed = *(const uint32_t *)ptr;
        out[0] = decodeUnsignedFloat(packed & 0x7ff, 5, 5);
        out[1] = decodeUnsignedFloat((packed >> 11) & 0x7ff, 5, 5);
        out[2] = decodeUnsignedFloat((packed >> 22) & 0x3ff, 5, 4);
        return true;
      }
      case ResourceFormatType::R10G10B10A2:
      {
        uint32_t packed = *(const uint32_t *)ptr;
        uint32_t r = packed & 0x3ff, g = (packed >> 10) & 0x3ff, b = (packed >> 20) & 0x3ff;
        uint32_t a = (packed >> 30) & 0x3;
        if(fmt.compType == CompType::UNorm || fmt.compType == CompType::UNormSRGB)
        {
          out[0] = r / 1023.0f;
          out[1] = g / 1023.0f;
          out[2] = b / 1023.0f;
          out[3] = a / 3.0f;
        }
        else if(fmt.compType == CompType::UInt)
        {
          out[0] = float(r);
          out[1] = float(g);
          out[2] = float(b);
          out[3] = float(a);
        }
        else
        {
          return false;
        }
        if(fmt.BGRAOrder())
          qSwap(out[0], out[2]);
        return true;
      }
      default: return false;
    }
  }

  if(fmt.compCount == 0 || fmt.compCount > 4 || fmt.compByteWidth == 0)
    return false;

  for(uint32_t c = 0; c < fmt.compCount; c++)
  {
    const byte *p = ptr + c * fmt.compByteWidth;
    float v = 0.0f;

    switch(fmt.compType)
    {
      case CompType::Float:
      {
        if(fmt.compByteWidth == 4)
          v = *(const float *)p;
        else if(fmt.compByteWidth == 8)
          v = float(*(const double *)p);
        else if(fmt.compByteWidth == 2)
          v = decodeHalf(*(const uint16_t *)p);
        else
          return false;
        break;
      }
      case CompType::UNorm:
      case CompType::UNormSRGB:
      {
        if(fmt.compByteWidth == 1)
          v = float(p[0]) / 255.0f;
        else if(fmt.compByteWidth == 2)
          v = float(*(const uint16_t *)p) / 65535.0f;
        else
          return false;
        break;
      }
      case CompType::SNorm:
      {
        if(fmt.compByteWidth == 1)
          v = qMax(-1.0f, float(*(const int8_t *)p) / 127.0f);
        else if(fmt.compByteWidth == 2)
          v = qMax(-1.0f, float(*(const int16_t *)p) / 32767.0f);
        else
          return false;
        break;
      }
      case CompType::UInt:
      case CompType::SInt:
      case CompType::UScaled:
      case CompType::SScaled:
      {
        bool signedType = (fmt.compType == CompType::SInt || fmt.compType == CompType::SScaled);
        float raw = 0.0f;
        if(fmt.compByteWidth == 1)
          raw = float(signedType ? *(const int8_t *)p : p[0]);
        else if(fmt.compByteWidth == 2)
          raw = float(signedType ? *(const int16_t *)p : *(const uint16_t *)p);
        else if(fmt.compByteWidth == 4)
          raw = float(signedType ? *(const int32_t *)p : *(const uint32_t *)p);
        else
          return false;

        if(fmt.compType == CompType::UScaled || fmt.compType == CompType::SScaled)
        {
          float scale = fmt.compByteWidth == 1 ? 255.0f : (fmt.compByteWidth == 2 ? 65535.0f : 1.0f);
          v = raw * scale;
        }
        else
        {
          v = raw;
        }
        break;
      }
      default: return false;
    }

    out[c] = v;
  }

  if(fmt.BGRAOrder())
    qSwap(out[0], out[2]);

  return true;
}

uint32_t formatElementSize(const ResourceFormat &fmt)
{
  if(fmt.type == ResourceFormatType::R11G11B10 || fmt.type == ResourceFormatType::R10G10B10A2)
    return 4;
  return fmt.compCount * fmt.compByteWidth;
}

// decode raw index bytes into vertex ids, applying baseVertex and marking
// primitive restart indices with RESTART_SENTINEL (when restartIndex != 0)
void decodeIndices(const bytebuf &idata, uint32_t stride, uint32_t count, int32_t baseVertex,
                   uint32_t restartIndex, rdcarray<uint32_t> &out)
{
  out.reserve(count);
  for(uint32_t i = 0; i < count; i++)
  {
    uint32_t idx;
    if(stride == 1)
      idx = idata[i];
    else if(stride == 2)
      idx = ((const uint16_t *)idata.data())[i];
    else
      idx = ((const uint32_t *)idata.data())[i];

    if(restartIndex != 0 && idx == restartIndex)
    {
      out.push_back(RESTART_SENTINEL);
      continue;
    }

    if(baseVertex < 0)
    {
      uint32_t subtract = (uint32_t)(-baseVertex);
      idx = idx < subtract ? 0 : idx - subtract;
    }
    else if(baseVertex > 0)
    {
      idx += (uint32_t)baseVertex;
    }

    out.push_back(idx);
  }
}

// turn a (possibly restart-separated) sequence of vertex ids into a triangle
// list. Indices referencing vertices >= numVertices are dropped (counted).
void triangulate(Topology topo, const rdcarray<uint32_t> &in, uint32_t numVertices,
                 rdcarray<uint32_t> &out, uint32_t &droppedTris)
{
  auto pushTri = [&](uint32_t a, uint32_t b, uint32_t c) {
    if(a < numVertices && b < numVertices && c < numVertices)
    {
      out.push_back(a);
      out.push_back(b);
      out.push_back(c);
    }
    else
    {
      droppedTris++;
    }
  };

  size_t runStart = 0;
  for(size_t i = 0; i <= in.size(); i++)
  {
    bool end = (i == in.size()) || (in[i] == RESTART_SENTINEL);
    if(!end)
      continue;

    size_t n = i - runStart;
    const uint32_t *v = in.data() + runStart;

    if(topo == Topology::TriangleList)
    {
      for(size_t k = 0; k + 2 < n; k += 3)
        pushTri(v[k], v[k + 1], v[k + 2]);
    }
    else if(topo == Topology::TriangleStrip)
    {
      for(size_t k = 0; k + 2 < n; k++)
      {
        // alternate triangles in a strip are wound the other way, flip them so
        // every output triangle has the strip start's orientation
        if(k & 1)
          pushTri(v[k + 1], v[k], v[k + 2]);
        else
          pushTri(v[k], v[k + 1], v[k + 2]);
      }
    }
    else if(topo == Topology::TriangleFan)
    {
      for(size_t k = 1; k + 1 < n; k++)
        pushTri(v[0], v[k], v[k + 1]);
    }

    runStart = i + 1;
  }
}

bool needWindingFlip(ExportWindingMode mode, const PipeState &pipe)
{
  if(mode == ExportWindingMode::Flip)
    return true;
  if(mode == ExportWindingMode::Keep)
    return false;
  // glTF and OBJ expect CCW front faces. If the API considers CW front,
  // reverse the triangle order.
  return !pipe.GetRasterState().frontCCW;
}

void applyWindingFlip(rdcarray<uint32_t> &indices)
{
  for(size_t i = 0; i + 2 < indices.size(); i += 3)
    qSwap(indices[i + 1], indices[i + 2]);
}

void applyUVFlip(ExportMeshData &mesh)
{
  for(ExportMeshAttribute &a : mesh.attributes)
  {
    if(!a.semantic.startsWith(QLatin1String("TEXCOORD")) || a.numComponents < 2)
      continue;
    for(size_t i = 0; i + 1 < a.data.size(); i += a.numComponents)
      a.data[i + 1] = 1.0f - a.data[i + 1];
  }
}

void applyAxisMapping(ExportMeshData &mesh, ExportAxisMode mode)
{
  if(mode == ExportAxisMode::NoChange)
    return;

  for(ExportMeshAttribute &a : mesh.attributes)
  {
    bool directional = (a.semantic == QLatin1String("POSITION") ||
                        a.semantic == QLatin1String("NORMAL") ||
                        a.semantic == QLatin1String("TANGENT"));
    if(!directional || a.numComponents < 3)
      continue;

    for(size_t i = 0; i + 2 < a.data.size(); i += a.numComponents)
    {
      float y = a.data[i + 1], z = a.data[i + 2];
      if(mode == ExportAxisMode::ZupToYup)
      {
        // rotate +90 degrees around X: (x, y, z) -> (x, -z, y)
        a.data[i + 1] = -z;
        a.data[i + 2] = y;
      }
      else
      {
        // rotate -90 degrees around X: (x, y, z) -> (x, z, -y)
        a.data[i + 1] = z;
        a.data[i + 2] = -y;
      }
    }
  }
}

// shared post-processing applied to both paths once attributes and raw
// indices are populated: triangulate, winding normalisation, UV flip, axis
// mapping and the cull-mode hint.
void finishMeshCommon(ExportMeshData &mesh, Topology topo, const PipeState &pipe, GraphicsAPI api,
                      const ExportSettings &settings)
{
  uint32_t droppedTris = 0;
  // triangulate is not in-place safe (it appends while scanning the input),
  // so build into a fresh array and swap
  rdcarray<uint32_t> triangles;
  triangulate(topo, mesh.indices, mesh.numVertices, triangles, droppedTris);
  mesh.indices.swap(triangles);
  if(needWindingFlip(settings.winding, pipe))
    applyWindingFlip(mesh.indices);
  if(settings.flipUVsForGL(api))
    applyUVFlip(mesh);
  applyAxisMapping(mesh, settings.axis);
  mesh.doubleSided = (pipe.GetRasterState().cullMode == CullMode::NoCull);

  if(droppedTris > 0)
  {
    mesh.warning = QCoreApplication::translate("MeshExtractor", "%1 out-of-range triangles skipped")
                       .arg(droppedTris);
  }
}
}    // namespace

bool MeshExtractor::ExtractVSInput(IReplayController *r, const ActionDescription &action,
                                   const PipeState &pipe, GraphicsAPI api,
                                   const ExportSettings &settings, ExportMeshData &out,
                                   QString &err)
{
  Q_UNUSED(api);

  Topology topo = pipe.GetPrimitiveTopology();
  if(!isExportableTopology(topo))
  {
    err = QCoreApplication::translate("MeshExtractor", "Topology %1 is not exportable")
              .arg(topologyName(topo));
    return false;
  }

  rdcarray<VertexInputAttribute> vertexInputs = pipe.GetVertexInputs();
  rdcarray<BoundVBuffer> vbs = pipe.GetVBuffers();
  BoundVBuffer ib = pipe.GetIBuffer();

  // pick the attributes we can export, grouped per vertex buffer
  struct UsedAttr
  {
    int inputIndex;
    QString semantic;
  };

  QMap<int, QVector<UsedAttr>> perBuffer;

  for(int i = 0; i < vertexInputs.count(); i++)
  {
    const VertexInputAttribute &attr = vertexInputs[i];

    // generic attributes don't come from a buffer (constant for the draw)
    if(attr.genericEnabled)
      continue;
    if(attr.vertexBuffer < 0 || attr.vertexBuffer >= vbs.count())
      continue;
    if(attr.format.compCount == 0)
      continue;

    QString semantic = classifySemantic(attr.name);
    if(semantic.isEmpty())
      continue;

    // one stream per semantic (first declaration wins, matching the viewer)
    bool duplicate = false;
    for(auto it = perBuffer.begin(); it != perBuffer.end(); ++it)
    {
      for(const UsedAttr &u : it.value())
      {
        if(u.semantic == semantic)
          duplicate = true;
      }
    }
    if(duplicate)
      continue;

    perBuffer[attr.vertexBuffer].push_back({i, semantic});
  }

  if(perBuffer.isEmpty())
  {
    err = QCoreApplication::translate("MeshExtractor", "No exportable vertex attributes found");
    return false;
  }

  bool hasPosition = false;
  for(auto it = perBuffer.begin(); it != perBuffer.end(); ++it)
  {
    for(const UsedAttr &u : it.value())
    {
      if(u.semantic == QLatin1String("POSITION"))
        hasPosition = true;
    }
  }
  if(!hasPosition)
  {
    err = QCoreApplication::translate("MeshExtractor", "No POSITION attribute found");
    return false;
  }

  // ---- index data -------------------------------------------------------

  rdcarray<uint32_t> indices;
  uint32_t numIndices = action.numIndices;

  bool indexed = (action.flags & ActionFlags::Indexed) && ib.resourceId != ResourceId() &&
                 ib.byteStride > 0;

  if(indexed)
  {
    uint64_t readBytes = uint64_t(numIndices) * ib.byteStride;
    uint32_t offs = action.indexOffset * ib.byteStride;
    if(ib.byteSize > offs)
      readBytes = qMin(ib.byteSize - offs, readBytes);
    else
      readBytes = 0;

    bytebuf idata;
    if(readBytes > 0)
      idata = r->GetBufferData(ib.resourceId, ib.byteOffset + offs, readBytes);

    uint32_t avail = idata.isEmpty() ? 0 : uint32_t(idata.size()) / ib.byteStride;
    if(avail < numIndices)
      numIndices = avail;

    if(numIndices == 0)
    {
      err = QCoreApplication::translate("MeshExtractor", "No index data available");
      return false;
    }

    uint32_t restart = pipe.IsRestartEnabled() ? pipe.GetRestartIndex() : 0;
    decodeIndices(idata, ib.byteStride, numIndices, action.baseVertex, restart, indices);
  }
  else
  {
    indices.resize(numIndices);
    for(uint32_t i = 0; i < numIndices; i++)
      indices[i] = i;
  }

  // ---- vertex buffer ranges ---------------------------------------------

  uint32_t maxIndex = 0;
  for(uint32_t idx : indices)
  {
    if(idx != RESTART_SENTINEL && idx > maxIndex)
      maxIndex = idx;
  }
  uint32_t numVertices = maxIndex + 1;

  // fetch each used vertex buffer over the range we need. The per-vertex
  // window starts at vertexOffset (matching the mesh viewer's fetch), so the
  // sequential indices of non-indexed draws address the window directly.
  struct BufferFetch
  {
    bytebuf data;
    uint32_t stride = 0;
    bool perInstance = false;
  };

  QMap<int, BufferFetch> fetches;

  for(auto it = perBuffer.begin(); it != perBuffer.end(); ++it)
  {
    const BoundVBuffer &vb = vbs[it.key()];

    uint32_t maxAttrEnd = 0;
    bool perInstance = false;
    for(const UsedAttr &u : it.value())
    {
      const VertexInputAttribute &attr = vertexInputs[u.inputIndex];
      maxAttrEnd = qMax(maxAttrEnd, attr.byteOffset + formatElementSize(attr.format));
      if(attr.perInstance)
        perInstance = true;
    }

    BufferFetch fetch;
    fetch.stride = vb.byteStride;
    fetch.perInstance = perInstance;

    uint64_t offset = 0;
    uint64_t readBytes = 0;
    if(perInstance)
    {
      // only instance 0 is exported
      offset = uint64_t(action.instanceOffset) * vb.byteStride;
      readBytes = uint64_t(vb.byteStride) + maxAttrEnd;
    }
    else
    {
      offset = uint64_t(action.vertexOffset) * vb.byteStride;
      readBytes = uint64_t(numVertices) * vb.byteStride + maxAttrEnd;
      if(vb.byteStride == 0)
        readBytes += 16;
    }

    if(vb.byteSize > offset)
      readBytes = qMin(vb.byteSize - offset, readBytes);
    else
      readBytes = 0;

    if(readBytes > 0)
      fetch.data = r->GetBufferData(vb.resourceId, vb.byteOffset + offset, readBytes);

    fetches[it.key()] = fetch;
  }

  // ---- decode attributes ------------------------------------------------

  for(auto it = perBuffer.begin(); it != perBuffer.end(); ++it)
  {
    const BufferFetch &fetch = fetches[it.key()];

    for(const UsedAttr &u : it.value())
    {
      const VertexInputAttribute &attr = vertexInputs[u.inputIndex];

      QString streamErr;
      ExportMeshAttribute stream;
      stream.semantic = u.semantic;
      stream.numComponents = qMin(4U, (uint32_t)attr.format.compCount);
      stream.data.resize(size_t(numVertices) * stream.numComponents);

      float comps[4];
      for(uint32_t v = 0; v < numVertices; v++)
      {
        // per-instance attributes read row 0 for every vertex of instance 0
        uint32_t srcRow = fetch.perInstance ? 0 : v;
        size_t rowByte = size_t(srcRow) * fetch.stride + attr.byteOffset;

        if(rowByte + formatElementSize(attr.format) > fetch.data.size())
          continue;

        if(!decodeFormatComponents(attr.format, fetch.data.data() + rowByte, comps))
        {
          streamErr = QCoreApplication::translate("MeshExtractor",
                                                  "Attribute %1 has unsupported format %2")
                          .arg(attr.name)
                          .arg(attr.format.Name());
          break;
        }

        for(uint32_t c = 0; c < stream.numComponents; c++)
          stream.data[size_t(v) * stream.numComponents + c] = comps[c];
      }

      if(!streamErr.isEmpty())
      {
        if(u.semantic == QLatin1String("POSITION"))
        {
          err = streamErr;
          return false;
        }
        // non-fatal: skip this attribute but remember why
        out.warning += (out.warning.isEmpty() ? QString() : QStringLiteral("; ")) + streamErr;
        continue;
      }

      out.attributes.push_back(stream);
    }
  }

  if(out.find(QStringLiteral("POSITION")) == NULL)
  {
    err = QCoreApplication::translate("MeshExtractor", "No usable POSITION attribute");
    return false;
  }

  out.numVertices = numVertices;
  out.indices = indices;
  out.instanced = bool(action.flags & ActionFlags::Instanced);

  finishMeshCommon(out, topo, pipe, api, settings);

  return true;
}

bool MeshExtractor::ExtractVSOutput(IReplayController *r, const ActionDescription &action,
                                    const PipeState &pipe, GraphicsAPI api,
                                    const ExportSettings &settings, ExportMeshData &out,
                                    QString &err)
{
  Q_UNUSED(action);
  Q_UNUSED(api);

  MeshFormat fmt = r->GetPostVSData(0, 0, MeshDataStage::VSOut);

  if(!fmt.status.empty())
  {
    err = QString(fmt.status);
    return false;
  }

  if(fmt.vertexResourceId == ResourceId() || fmt.vertexByteStride == 0 || fmt.numIndices == 0)
  {
    err = QCoreApplication::translate("MeshExtractor", "No post-VS data available");
    return false;
  }

  Topology topo = fmt.topology;
  if(!isExportableTopology(topo))
  {
    err = QCoreApplication::translate("MeshExtractor", "Topology %1 is not exportable")
              .arg(topologyName(topo));
    return false;
  }

  // the post-VS buffer contains one vertex per referenced index (APIs compact
  // to unique sorted indices and remap the index buffer accordingly), so
  // numIndices is the upper bound on rows
  uint64_t readBytes = uint64_t(fmt.numIndices) * fmt.vertexByteStride;
  if(fmt.vertexByteSize > 0)
    readBytes = qMin(fmt.vertexByteSize, readBytes);

  bytebuf vdata = r->GetBufferData(fmt.vertexResourceId, fmt.vertexByteOffset, readBytes);
  uint32_t numVertices = uint32_t(vdata.size()) / fmt.vertexByteStride;

  if(numVertices == 0)
  {
    err = QCoreApplication::translate("MeshExtractor", "Post-VS vertex buffer is empty");
    return false;
  }

  // ---- position ---------------------------------------------------------

  {
    ResourceFormat posFmt = fmt.format;
    if(posFmt.compCount == 0 || posFmt.compCount > 4)
      posFmt.compCount = 4;

    ExportMeshAttribute pos;
    pos.semantic = QStringLiteral("POSITION");
    pos.numComponents = qMin(4U, (uint32_t)posFmt.compCount);
    pos.data.resize(size_t(numVertices) * pos.numComponents);

    float comps[4];
    for(uint32_t v = 0; v < numVertices; v++)
    {
      const byte *ptr = vdata.data() + size_t(v) * fmt.vertexByteStride;
      if(ptr + formatElementSize(posFmt) > vdata.data() + vdata.size())
        continue;

      if(!decodeFormatComponents(posFmt, ptr, comps))
        memset(comps, 0, sizeof(comps));

      for(uint32_t c = 0; c < pos.numComponents; c++)
        pos.data[size_t(v) * pos.numComponents + c] = comps[c];
    }

    out.attributes.push_back(pos);
  }

  // ---- secondary attributes from the vertex shader output signature ------

  const ShaderReflection *reflection = pipe.GetShaderReflection(ShaderStage::Vertex);

  if(reflection && !reflection->outputSignature.empty())
  {
    // post-VS buffers pack the position first, then the remaining outputs in
    // signature order, each contributing compCount floats (doubles would take
    // two slots per component and are skipped entirely below)
    auto elementFloats = [](const SigParameter &p) {
      return (p.varType == VarType::Double ? 2u : 1u) * p.compCount;
    };

    uint32_t posFloats = 0;
    for(const SigParameter &p : reflection->outputSignature)
    {
      if(p.systemValue == ShaderBuiltin::Position)
      {
        posFloats = elementFloats(p);
        break;
      }
    }

    uint32_t offset = posFloats > 0 ? posFloats : out.attributes[0].numComponents;

    for(const SigParameter &p : reflection->outputSignature)
    {
      uint32_t floats = elementFloats(p);

      if(p.systemValue == ShaderBuiltin::Position || p.varType == VarType::Double ||
         p.compCount == 0)
      {
        // keep the byte-offset book-keeping even for skipped attributes
        continue;
      }

      QString semantic;
      if(!p.semanticIdxName.empty())
        semantic = classifySemantic(p.semanticIdxName);
      if(semantic.isEmpty() && !p.varName.empty())
        semantic = classifySemantic(p.varName);

      bool want = (semantic == QLatin1String("NORMAL") || semantic == QLatin1String("TANGENT") ||
                   semantic.startsWith(QLatin1String("TEXCOORD")) ||
                   semantic.startsWith(QLatin1String("COLOR")));

      if(want && !out.find(semantic) &&
         (size_t(offset) + p.compCount) * sizeof(float) <= fmt.vertexByteStride)
      {
        ExportMeshAttribute stream;
        stream.semantic = semantic;
        stream.numComponents = qMin(4U, (uint32_t)p.compCount);
        stream.data.resize(size_t(numVertices) * stream.numComponents);

        ResourceFormat f;
        f.compCount = p.compCount;
        f.compByteWidth = 4;
        f.compType = CompType::Float;

        float comps[4];
        for(uint32_t v = 0; v < numVertices; v++)
        {
          const byte *ptr = vdata.data() + size_t(v) * fmt.vertexByteStride +
                            size_t(offset) * sizeof(float);
          if(ptr + p.compCount * sizeof(float) > vdata.data() + vdata.size())
            continue;

          if(decodeFormatComponents(f, ptr, comps))
          {
            for(uint32_t c = 0; c < stream.numComponents; c++)
              stream.data[size_t(v) * stream.numComponents + c] = comps[c];
          }
        }

        out.attributes.push_back(stream);
      }

      offset += floats;
    }
  }

  // ---- indices ----------------------------------------------------------

  rdcarray<uint32_t> indices;

  if(fmt.indexResourceId != ResourceId() && fmt.indexByteStride > 0)
  {
    uint64_t idxRead = uint64_t(fmt.numIndices) * fmt.indexByteStride;
    if(fmt.indexByteSize > 0)
      idxRead = qMin(fmt.indexByteSize, idxRead);

    bytebuf idata = r->GetBufferData(fmt.indexResourceId, fmt.indexByteOffset, idxRead);
    uint32_t avail = idata.isEmpty() ? 0 : uint32_t(idata.size()) / fmt.indexByteStride;
    uint32_t count = qMin(avail, fmt.numIndices);

    uint32_t restart = fmt.allowRestart ? fmt.restartIndex : 0;
    decodeIndices(idata, fmt.indexByteStride, count, fmt.baseVertex, restart, indices);
  }
  else
  {
    indices.resize(fmt.numIndices);
    for(uint32_t i = 0; i < fmt.numIndices; i++)
      indices[i] = i;
  }

  // ---- API-specific space handling --------------------------------------

  // GL lower-left clip origin / Vulkan negative viewports are flagged by the
  // driver; flip Y so the exported geometry isn't upside down
  if(fmt.flipY)
  {
    for(ExportMeshAttribute &a : out.attributes)
    {
      if(a.semantic != QLatin1String("POSITION"))
        continue;
      for(size_t i = 0; i + 1 < a.data.size(); i += a.numComponents)
        a.data[i + 1] = -a.data[i + 1];
    }
  }

  out.numVertices = numVertices;
  out.indices = indices;
  out.instanced = fmt.instanced;

  finishMeshCommon(out, topo, pipe, api, settings);

  return true;
}

QStringList MeshExtractor::PreviewVSInputAttributes(const PipeState &pipe)
{
  QStringList ret;

  rdcarray<VertexInputAttribute> vertexInputs = pipe.GetVertexInputs();
  for(const VertexInputAttribute &attr : vertexInputs)
  {
    if(attr.genericEnabled)
      continue;
    QString semantic = classifySemantic(attr.name);
    if(semantic.isEmpty() || ret.contains(semantic))
      continue;
    ret << QStringLiteral("%1 (%2)").arg(semantic).arg(attr.format.Name());
  }

  return ret;
}

QStringList MeshExtractor::PreviewVSOutputAttributes(const PipeState &pipe)
{
  QStringList ret;

  const ShaderReflection *reflection = pipe.GetShaderReflection(ShaderStage::Vertex);
  if(!reflection)
    return ret;

  bool hasPosition = false;
  for(const SigParameter &p : reflection->outputSignature)
  {
    if(p.systemValue == ShaderBuiltin::Position)
    {
      ret << QStringLiteral("POSITION (float%1)").arg(p.compCount);
      hasPosition = true;
      break;
    }
  }
  if(!hasPosition)
    ret << QStringLiteral("POSITION (float4)");

  for(const SigParameter &p : reflection->outputSignature)
  {
    if(p.systemValue == ShaderBuiltin::Position)
      continue;

    QString semantic;
    if(!p.semanticIdxName.empty())
      semantic = classifySemantic(p.semanticIdxName);
    if(semantic.isEmpty() && !p.varName.empty())
      semantic = classifySemantic(p.varName);

    if(!semantic.isEmpty() && !ret.contains(semantic))
      ret << QStringLiteral("%1 (float%2)").arg(semantic).arg(p.compCount);
  }

  return ret;
}
