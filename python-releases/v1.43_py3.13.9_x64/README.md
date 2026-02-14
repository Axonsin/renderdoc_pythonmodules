# RenderDoc Python Modules - v1.43_py3.13.9_x64

## Version Information

- **RenderDoc Version**: 1.43
- **Python Version**: 3.13.9
- **Platform**: x64 (64-bit Windows)
- **Build Configuration**: Development
- **Build Date**: 2025-02-15

## Files

### Python Modules
| File | Size | Description |
|------|------|-------------|
| `renderdoc.pyd` | 7.3 MB | Core RenderDoc Python module |
| `qrenderdoc.pyd` | 8.5 MB | Qt-based UI Python module |

### Dependencies
| File | Size | Description |
|------|------|-------------|
| `renderdoc.dll` | 73 MB | RenderDoc core library (required dependency) |
| `d3dcompiler_47.dll` | 4.6 MB | Direct3D compiler (required dependency) |

### Debug Symbols & Link Libraries
| File | Size | Description |
|------|------|-------------|
| `renderdoc.pdb` | 18 MB | Debug symbols for renderdoc.pyd |
| `qrenderdoc.pdb` | 21 MB | Debug symbols for qrenderdoc.pyd |
| `renderdoc.lib` | 2.0 KB | Import library for renderdoc.pyd |
| `qrenderdoc.lib` | 2.2 KB | Import library for qrenderdoc.pyd |
| `renderdoc.exp` | 980 B | Export file for renderdoc.pyd |
| `qrenderdoc.exp` | 1.1 KB | Export file for qrenderdoc.pyd |

## Usage

All files must be in the same directory when importing:

```python
import sys
sys.path.append(r'path\to\v1.43_py3.13.9_x64')

import renderdoc
import qrenderdoc
```

Or copy the `.pyd` files to your Python project directory.

## Type Stubs

This release includes Python type stubs for IDE autocomplete and type checking:

- **stubs/renderdoc/** (371 files) - Type annotations for renderdoc module
- **stubs/qrenderdoc/** (53 files) - Type annotations for qrenderdoc module

**Total**: 424 stub files, 3.6 MB

To use type stubs in your project:

**Option 1: Configure IDE**
- Add `stubs/` directory to your IDE's Python path
- VS Code: Add to `python.analysis.extraPaths` in `.vscode/settings.json`
- PyCharm: Mark `stubs/` directory as Sources Root

**Option 2: Runtime type checking**
```bash
mypy --custom-typeshed-dir=path/to/stubs your_script.py
```

## Requirements

- Windows 10/11 x64
- Python 3.13.x
- Visual C++ Redistributable (usually pre-installed)

## Build Information

Built from RenderDoc source using MSBuild with Visual Studio 2022.
- **PlatformToolset**: v143
- **MSBuild Version**: 17.11.5

---

For build scripts and configuration, see the parent project's `.claude/skills/renderdoc-python-builder/` directory.
