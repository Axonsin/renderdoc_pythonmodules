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

#include <QStringList>
#include "Code/ResourceExport/ResourceExport.h"

// Extracts a fully decoded, triangulated ExportMeshData from the current event
// on a replay controller. All Extract* functions must be called on the replay
// thread (inside a ReplayManager invoke callback) with the controller already
// switched to the target event. The Preview* functions only need the PipeState
// and do no buffer reads, so they are safe to call from the UI thread with
// ICaptureContext::CurPipelineState().
class MeshExtractor
{
public:
  static bool ExtractVSInput(IReplayController *r, const ActionDescription &action,
                             const PipeState &pipe, GraphicsAPI api,
                             const ExportSettings &settings, ExportMeshData &out, QString &err);

  static bool ExtractVSOutput(IReplayController *r, const ActionDescription &action,
                              const PipeState &pipe, GraphicsAPI api,
                              const ExportSettings &settings, ExportMeshData &out, QString &err);

  static QStringList PreviewVSInputAttributes(const PipeState &pipe);
  static QStringList PreviewVSOutputAttributes(const PipeState &pipe);
};
