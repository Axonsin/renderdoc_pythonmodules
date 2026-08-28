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
#include <QString>
#include "Code/ResourceExport/ResourceExport.h"

struct ICaptureContext;
struct IReplayController;
struct PipeState;

// Enumerates pixel-shader bound textures for an event, guesses which one is
// the base colour map and saves textures through the internal SaveTexture
// path (which also applies GL row-order flipping for disk formats).
class TextureExporter
{
public:
  // must be called with the pipe state of the event in question
  static QList<ExportTextureCandidate> CollectPixelTextures(ICaptureContext &ctx,
                                                            const PipeState &pipe);

  // name heuristic over the candidates: looks for diffuse/albedo/basecolour
  // naming and falls back to the first 2D texture
  static ResourceId GuessBaseColor(const QList<ExportTextureCandidate> &candidates);

  // saves one texture with export defaults (mip 0, alpha preserved). Runs on
  // the replay thread.
  static bool SaveTexture(IReplayController *r, ResourceId id, ExportTextureFormat fmt,
                          int jpegQuality, const QString &path, QString &err);

  static QString ExtensionFor(ExportTextureFormat fmt);
  static QString MimeTypeFor(ExportTextureFormat fmt);

  // unique, filesystem-safe base name for a texture resource
  static QString TextureBaseName(ICaptureContext &ctx, ResourceId id);
};
