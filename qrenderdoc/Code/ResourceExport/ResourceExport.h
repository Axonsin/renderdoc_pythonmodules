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

// Resource export: shared types for the ExportWindow, mesh extraction and the
// glTF/OBJ writers. Everything in Code/ResourceExport is deliberately kept
// self-contained so it can follow upstream without touching core files.

#pragma once

#include <QMap>
#include <QString>
#include "renderdoc_replay.h"

enum class ExportFileFormat
{
  GLB,
  GLTF,
  OBJ,
};

enum class ExportMeshSource
{
  VSInput,
  VSOutput,
};

enum class ExportUVFlipMode
{
  Auto,    // flip V when the capture is OpenGL (bottom-left texture origin)
  On,
  Off,
};

enum class ExportWindingMode
{
  Auto,    // normalise to CCW front-faces using the rasteriser's frontCCW state
  Keep,
  Flip,
};

enum class ExportAxisMode
{
  NoChange,
  ZupToYup,
  YupToZup,
};

enum class ExportTextureFormat
{
  PNG,
  JPG,
};

struct ExportSettings
{
  ExportFileFormat fileFormat = ExportFileFormat::GLB;
  ExportMeshSource meshSource = ExportMeshSource::VSInput;
  ExportUVFlipMode uvFlip = ExportUVFlipMode::Auto;
  ExportWindingMode winding = ExportWindingMode::Auto;
  ExportAxisMode axis = ExportAxisMode::NoChange;
  ExportTextureFormat texFormat = ExportTextureFormat::PNG;
  int jpegQuality = 90;
  bool mergeIntoSingleScene = false;    // only meaningful for glTF/OBJ multi-event export
  QString outputDir;

  bool flipUVsForGL(GraphicsAPI api) const
  {
    if(uvFlip == ExportUVFlipMode::On)
      return true;
    if(uvFlip == ExportUVFlipMode::Off)
      return false;
    return api == GraphicsAPI::OpenGL;
  }
};

// one named vertex attribute stream, numVertices * numComponents floats
struct ExportMeshAttribute
{
  QString semantic;      // POSITION, NORMAL, TANGENT, TEXCOORD0..., COLOR0...
  uint32_t numComponents = 0;
  rdcarray<float> data;

  bool valid() const { return numComponents > 0 && !data.empty(); }
};

// fully decoded, triangulated mesh ready for any writer. indices refer to
// vertices [0, numVertices) and are always triangle lists (3 per primitive).
struct ExportMeshData
{
  uint32_t eventId = 0;
  QString name;
  uint32_t numVertices = 0;
  rdcarray<ExportMeshAttribute> attributes;
  rdcarray<uint32_t> indices;
  bool doubleSided = false;    // from the rasteriser's cull mode
  bool instanced = false;      // draw was instanced, only instance 0 exported
  QString warning;             // non-fatal notes (skipped attributes, dropped triangles)

  const ExportMeshAttribute *find(const QString &semantic) const
  {
    for(const ExportMeshAttribute &a : attributes)
    {
      if(a.semantic == semantic)
        return &a;
    }
    return NULL;
  }

  bool hasNormals() const { return find(QString::fromLatin1("NORMAL")) != NULL; }
  bool hasUVs() const { return find(QString::fromLatin1("TEXCOORD0")) != NULL; }
};

struct ExportTextureCandidate
{
  ResourceId resourceId;
  QString name;             // texture resource name
  QString bindName;         // bind point name in the pixel shader, if known
  bool likelyBaseColor = false;
  bool is2D = false;
};

struct ExportItemResult
{
  uint32_t eventId = 0;
  bool success = false;
  QString message;
};
