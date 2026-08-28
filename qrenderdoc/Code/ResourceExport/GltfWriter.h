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

#include <QByteArray>
#include <QList>
#include <QString>
#include "Code/ResourceExport/ResourceExport.h"

// Writes a glTF 2.0 asset from one or more fully decoded meshes. Two container
// modes are supported:
//  - .glb   : single binary file, meshes and images embedded in the BIN chunk
//  - .gltf  : JSON file + <name>.bin + image files alongside, referenced by uri
class GltfWriter
{
public:
  struct MeshEntry
  {
    const ExportMeshData *mesh = NULL;
    ResourceId baseColorTex;    // optional, must appear in textures
  };

  struct TextureEntry
  {
    ResourceId id;
    QString relativePath;     // .gltf mode: filename relative to the .gltf
    QByteArray embeddedData;    // .glb mode: encoded image bytes
    QString mimeType;           // image/png or image/jpeg
  };

  // path is the destination .glb or .gltf. For .gltf the bin file is written
  // next to it as <basename>.bin.
  static bool Write(const QString &path, const QList<MeshEntry> &meshes,
                    const QList<TextureEntry> &textures, QString &err);

  static QString sanitiseName(const QString &name);
};
