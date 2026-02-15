# RenderDoc Python Modules

[English](README.md) | [简体中文](README.zh-CN.md)

预编译的 RenderDoc Python 模块仓库，包含不同 RenderDoc 版本、Python 版本和平台的构建产物。用于从 RenderDoc 的develop 分支构建出python模块用于开发测试和调试。**使用Windows Platform 构建**

## 简介

本仓库专门用于存储和管理 RenderDoc 的 Python 模块构建产物，包括：

- **renderdoc.pyd** - RenderDoc 核心 Python 模块
- **qrenderdoc.pyd** - Qt UI Python 模块
- **renderdoc.dll** - RenderDoc 核心库
- **依赖文件** - D3D 编译器等运行时依赖
- **调试符号** - .pdb 文件用于调试
- **类型存根** - 完整的 Python 类型提示（.py 文件）
- **构建报告** - 详细的构建环境信息和版本记录

## 目录结构

```text
python-releases/
├── v1.43_py3.13.9_x64/
│   ├── pymodules/           # Python 模块和依赖
│   │   ├── renderdoc.pyd
│   │   ├── qrenderdoc.pyd
│   │   ├── renderdoc.dll
│   │   ├── d3dcompiler_47.dll
│   │   ├── renderdoc.pdb    # 调试符号
│   │   ├── qrenderdoc.pdb
│   │   ├── renderdoc.lib    # 导入库
│   │   └── qrenderdoc.lib
│   ├── stubs/               # Python Stubs
│   └── REPORT.md            # 构建使用的环境
└── v{version}_py{python}_{platform}/
```

## 版本规则

发布目录遵循命名格式：

```text
v{RenderDoc版本}_py{Python完整版本}_{平台}

示例：
v1.43_py3.13.9_x64    → RenderDoc 1.43, Python 3.13.9, 64位 Windows
v1.43_py3.12.8_x86    → RenderDoc 1.43, Python 3.12.8, 32位 Windows
v1.44_py3.13.9_x64    → RenderDoc 1.44, Python 3.13.9, 64位 Windows
```

## 快速开始

### 1. 选择对应版本

根据你的 RenderDoc 版本、Python 版本和平台选择对应的发布目录。

**查看可用版本**：

```bash
ls python-releases/
```

### 2. 使用模块

#### 方法 A：添加到 Python 路径

```python
import sys
sys.path.append(r'path\to\v1.43_py3.13.9_x64\pymodules')

import renderdoc
import qrenderdoc

# 打开 RenderDoc 捕获文件
cap = renderdoc.OpenCapture("capture.rdc")
```

#### 方法 B：复制到项目目录

将 `pymodules/` 目录中的 `.pyd` 和 `.dll` 文件复制到你的 Python 项目目录。

### 3. 配置类型提示

每个版本都包含完整的 Python 类型存根，提供 IDE 自动补全和类型检查。

> Stubs经过修改， 使用的`regenerate-stubs.py`使用的是来自Develop构建分支而非Release分支。

**VS Code 配置** (`.vscode/settings.json`)：

```json
{
  "python.analysis.extraPaths": [
    "path/to/v1.43_py3.13.9_x64/stubs"
  ]
}
```

**PyCharm 配置**：

1. 右键点击 `stubs/` 目录
2. 选择 `Mark Directory as` → `Sources Root`

**有关于更多的使用该Stubs作为语法高亮的说明， 详情可以参考[这里](https://renderdoc.org/docs/python_api/dev_environment.html)**

## 系统要求

### 通用要求

- **操作系统**: Windows 10/11 (x64 或 x86)。
- **Python**: 对应目录中指定的 Python 版本。一般是3.13.9
- **Visual C++ Redistributable**: 通常已预安装

### 具体版本示例

**v1.43_py3.13.9_x64**:

- Windows 10/11 x64
- Python 3.13.9
- MSBuild 17.11.5
- Platform Toolset v143
- Windows SDK 10.0.26100.0

## 构建信息

每个发布版本都包含详细的构建报告 (`REPORT.md`)，记录了：

- **Python 环境**: 版本、包含目录、导入库
- **构建工具**: MSBuild、Platform Toolset、Windows SDK 版本
- **Visual Studio**: 版本和安装路径
- **系统信息**: 操作系统版本、架构
- **构建参数**: 完整的 MSBuild 命令行
- **测试结果**: 模块导入测试状态

### 构建环境示例

```text
MSBuild: 17.11.5
Platform Toolset: v143
Windows SDK: 10.0.26100.0
Visual Studio: 2022
Python: 3.13.9
```

**详细文档**: [v1.43_py3.13.9_x64/README.md](python-releases/v1.43_py3.13.9_x64/README.md)

## 常见问题

### Q: 如何选择正确的版本？

**A**: 根据以下优先级选择：

1. **RenderDoc 版本** - RenderDoc 安装版本匹配， 尤其是依赖于远程调试（如安卓系统预装的RenderDoc cmd， 必须版本号一致）
2. **Python 版本** - 与 Python 环境版本一致

### Q: 可以跨版本使用吗？

**A**: **不建议**。

- 不同 RenderDoc 版本的 API 可能不兼容
- 不同 Python 版本的模块不兼容（ABI 变化）
- x86 和 x64 不能混用

### Q: 导入失败怎么办？

**A**: 检查以下几点：

1. Python 版本是否匹配
2. 是否同时复制了 `.pyd` 和 `.dll` 文件
3. 图形驱动（dx dll）与 renderdoc dll 是否也在同级目录中？
4. 是否安装了 Visual C++ Redistributable
5. 平台是否匹配（x64/x86）？
如果仍然无法解决，[可以使用 Dependecies 工具检查依赖项](https://github.com/lucasg/Dependencies)

### Q: 类型存根如何使用？

**A**: See the [RenderDoc Python API development environment documentation](https://renderdoc.org/docs/python_api/dev_environment.html)

1. 用于 IDE 自动补全和类型检查
2. 支持 VS Code、PyCharm 等 IDE
3. 可配合 mypy 等工具进行静态类型检查

### Q: 从源码构建

**A**: 本仓库使用自动化构建脚本（实际上是skill）， 包含了一些对Python高版本的Deprecated的警告处理（C4996）和C++语法降级警告， 不将警告视为报错的处理。

- 位于`.claude/skills/renderdoc-python-builder/` 目录

## 贡献指南

本项目主要作为预编译模块的发布仓库，不接受源码贡献。

如需：

- **报告问题**: 在 RenderDoc 主仓库提交 Issue
- **请求新版本**: 在 Issue 中提出版本需求
- **构建问题**: 查看具体版本的 REPORT.md 了解构建环境

## 许可证

本仓库中的构建产物遵循 RenderDoc 的许可证：

- **RenderDoc**: MIT License
- 详见: <https://github.com/baldurk/renderdoc/blob/master/LICENSE>

## 相关链接

- **RenderDoc 官方仓库**: <https://github.com/baldurk/renderdoc>
- **RenderDoc 文档**: <https://renderdoc.org/>
- **Python API 文档**: <https://renderdoc.org/docs/python_api.html>

## 维护信息

- **主分支**: main 作为集成module。其他分支用于储存编译产物， 因为大量的内存占用故不会上传到GitHub
- **发布分支**: python/v1.43

---

**注意**: 本仓库仅包含预编译的 Python 模块和构建产物，不包含 RenderDoc 源代码。如需源代码，请访问 [RenderDoc 官方仓库](https://github.com/baldurk/renderdoc)。
