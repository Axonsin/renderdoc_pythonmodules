# RenderDoc Python Modules - v1.44_py3.13.9_x64

## Version Information

- **RenderDoc Version**: 1.44
- **Python Version**: 3.13.9 | packaged by Anaconda, Inc. | (main, Oct 21 2025, 19:09:58) [MSC v.1929 64 bit (AMD64)]
- **Platform**: x64 (Windows)
- **Build Configuration**: Release
- **Build Date**: 2026-05-13

## Files

| File | Size | Description |
|------|------|-------------|
| `d3dcompiler_47.dll` | 4.5 MB | Direct3D compiler (required dependency) |
| `qrenderdoc.exp` | 0.0 MB |  |
| `qrenderdoc.lib` | 0.0 MB |  |
| `qrenderdoc.pdb` | 4.6 MB |  |
| `qrenderdoc.pyd` | 7.2 MB | Qt-based UI Python module |
| `renderdoc.dll` | 23.8 MB | RenderDoc core library (required dependency) |
| `renderdoc.exp` | 0.0 MB |  |
| `renderdoc.lib` | 0.0 MB |  |
| `renderdoc.pdb` | 4.0 MB |  |
| `renderdoc.pyd` | 5.5 MB | Core RenderDoc Python module |

## Usage

All files must be in the same directory when importing:

```python
import sys
sys.path.append(r'path\to\v1.44_py3.13.9_x64')

import renderdoc
import qrenderdoc
```

Or copy the `.pyd` files to your Python project directory.

## Type Stubs

This release includes Python type stubs (`.py` files with type annotations) in the `stubs/` directory for IDE autocomplete and type checking:

- `stubs/renderdoc/` - Type stubs for renderdoc module
- `stubs/qrenderdoc/` - Type stubs for qrenderdoc module

To use type stubs in your project, add the stubs directory to your IDE's Python path or to `MYPYPATH`.

## Requirements

- Windows 10/11 x64
- Python 3.13.x
- Visual C++ Redistributable (usually pre-installed)

## Build Information

Built from RenderDoc source using MSBuild with Visual Studio 2022.
- **PlatformToolset**: v145
- **MSBuild Version**: 18.4.0.7901

---

For build scripts and configuration, see the parent project's `.claude/skills/renderdoc-python-builder/` directory.
