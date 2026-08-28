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

#include "ObjWriter.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

ObjWriter::~ObjWriter()
{
  delete m_Stream;
  delete m_File;
}

QString ObjWriter::sanitiseName(const QString &name)
{
  QString ret = name.simplified();
  // OBJ names can't contain whitespace
  ret.replace(QRegularExpression(QLatin1String("[\\s]+")), QStringLiteral("_"));
  ret.remove(QRegularExpression(QLatin1String("[^\\w\\-.]")));
  if(ret.isEmpty())
    ret = QStringLiteral("mesh");
  return ret;
}

bool ObjWriter::open(const QString &objPath, const QString &mtlFilename, QString &err)
{
  m_ObjPath = objPath;
  m_MtlFilename = mtlFilename;

  m_File = new QFile(objPath);
  if(!m_File->open(QIODevice::WriteOnly | QIODevice::Text))
  {
    err = QObject::tr("Failed to open %1 for writing").arg(objPath);
    delete m_File;
    m_File = NULL;
    return false;
  }

  m_Stream = new QTextStream(m_File);
  m_Stream->setRealNumberPrecision(6);

  *m_Stream << "# Exported by RenderDic\n";
  if(!mtlFilename.isEmpty())
    *m_Stream << "mtllib " << mtlFilename << "\n";

  return true;
}

void ObjWriter::addMesh(const ExportMeshData &mesh, const QString &materialName,
                        const QString &mapKdRelativePath)
{
  Q_ASSERT(m_Stream);

  const ExportMeshAttribute *pos = mesh.find(QStringLiteral("POSITION"));
  const ExportMeshAttribute *uvs = NULL;
  const ExportMeshAttribute *normals = mesh.find(QStringLiteral("NORMAL"));

  for(const ExportMeshAttribute &a : mesh.attributes)
  {
    if(a.semantic == QLatin1String("TEXCOORD0"))
      uvs = &a;
  }

  if(!pos)
    return;

  QString group = sanitiseName(mesh.name);
  QString material = sanitiseName(materialName.isEmpty() ? mesh.name : materialName);

  *m_Stream << "g " << group << "\n";

  bool haveMaterial = false;
  for(const Material &m : m_Materials)
  {
    if(m.name == material)
      haveMaterial = true;
  }
  if(!haveMaterial)
    m_Materials.push_back({material, mapKdRelativePath});
  *m_Stream << "usemtl " << material << "\n";

  // OBJ has no vertex colour support, POSITION/NORMAL/TEXCOORD only
  for(size_t v = 0; v < mesh.numVertices; v++)
  {
    size_t i = size_t(v) * pos->numComponents;
    *m_Stream << "v " << pos->data[i] << " " << pos->data[i + 1] << " " << pos->data[i + 2] << "\n";
  }

  if(uvs && uvs->numComponents >= 2)
  {
    for(size_t v = 0; v < mesh.numVertices; v++)
    {
      size_t i = size_t(v) * uvs->numComponents;
      *m_Stream << "vt " << uvs->data[i] << " " << uvs->data[i + 1] << "\n";
    }
  }

  if(normals && normals->numComponents >= 3)
  {
    for(size_t v = 0; v < mesh.numVertices; v++)
    {
      size_t i = size_t(v) * normals->numComponents;
      *m_Stream << "vn " << normals->data[i] << " " << normals->data[i + 1] << " "
                << normals->data[i + 2] << "\n";
    }
  }

  bool hasUV = (uvs && uvs->numComponents >= 2);
  bool hasNormals = (normals && normals->numComponents >= 3);

  // v/vt/vn are three independent 1-based index spaces
  auto corner = [&](uint32_t idx) {
    uint32_t v = idx + 1 + m_VertexBase;
    if(hasUV && hasNormals)
      return QStringLiteral("%1/%2/%3").arg(v).arg(idx + 1 + m_UVBase).arg(idx + 1 + m_NormalBase);
    if(hasUV)
      return QStringLiteral("%1/%2").arg(v).arg(idx + 1 + m_UVBase);
    if(hasNormals)
      return QStringLiteral("%1//%2").arg(v).arg(idx + 1 + m_NormalBase);
    return QString::number(v);
  };

  for(size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
  {
    *m_Stream << "f " << corner(mesh.indices[t]) << " " << corner(mesh.indices[t + 1]) << " "
              << corner(mesh.indices[t + 2]) << "\n";
  }

  m_VertexBase += mesh.numVertices;
  m_UVBase += (uvs && uvs->numComponents >= 2) ? mesh.numVertices : 0;
  m_NormalBase += (normals && normals->numComponents >= 3) ? mesh.numVertices : 0;
  m_MeshCount++;
}

bool ObjWriter::finish(QString &err)
{
  if(!m_Stream || !m_File)
  {
    err = QObject::tr("OBJ writer was not opened");
    return false;
  }

  m_Stream->flush();
  m_File->close();

  delete m_Stream;
  m_Stream = NULL;
  delete m_File;
  m_File = NULL;

  if(m_MeshCount == 0)
  {
    QFile::remove(m_ObjPath);
    return true;
  }

  // write the companion .mtl next to the .obj
  QString mtlPath = QFileInfo(m_ObjPath).dir().filePath(m_MtlFilename);
  QFile mtl(mtlPath);
  if(!mtl.open(QIODevice::WriteOnly | QIODevice::Text))
  {
    err = QObject::tr("Failed to open %1 for writing").arg(mtlPath);
    return false;
  }

  QTextStream ts(&mtl);
  ts.setCodec("UTF-8");
  for(const Material &m : m_Materials)
  {
    ts << "newmtl " << m.name << "\n";
    ts << "Kd 1.000 1.000 1.000\n";
    ts << "Ks 0.000 0.000 0.000\n";
    ts << "d 1.000\n";
    ts << "illum 1\n";
    if(!m.mapKd.isEmpty())
      ts << "map_Kd " << m.mapKd << "\n";
    ts << "\n";
  }
  mtl.close();

  return true;
}
