# RenderDoc Python Modules

[English](README.md) | [简体中文](README.zh-CN.md)

Pre-built RenderDoc Python modules repository containing builds for different RenderDoc versions, Python versions, and platforms. Built from RenderDoc's develop branch for development, testing, and debugging. **Built on Windows Platform**

## Overview

This repository is dedicated to storing and managing RenderDoc Python module build artifacts, including:

- **renderdoc.pyd** - Core RenderDoc Python module
- **qrenderdoc.pyd** - Qt UI Python module
- **renderdoc.dll** - RenderDoc core library
- **Dependencies** - Runtime dependencies like D3D compiler
- **Debug Symbols** - .pdb files for debugging
- **Type Stubs** - Complete Python type hints (.py files)
- **Build Reports** - Detailed build environment information and version records

## Directory Structure

```text
python-releases/
├── v1.43_py3.13.9_x64/
│   ├── pymodules/           # Python modules and dependencies
│   │   ├── renderdoc.pyd
│   │   ├── qrenderdoc.pyd
│   │   ├── renderdoc.dll
│   │   ├── d3dcompiler_47.dll
│   │   ├── renderdoc.pdb    # Debug symbols
│   │   ├── qrenderdoc.pdb
│   │   ├── renderdoc.lib    # Import libraries
│   │   └── qrenderdoc.lib
│   ├── stubs/               # Python Stubs
│   └── REPORT.md            # Build environment
└── v{version}_py{python}_{platform}/
    └── ...
```

## Version Convention

Release directories follow this naming format:

```text
v{RenderDoc version}_py{Python full version}_{platform}

Examples:
v1.43_py3.13.9_x64    → RenderDoc 1.43, Python 3.13.9, 64-bit Windows
v1.43_py3.12.8_x86    → RenderDoc 1.43, Python 3.12.8, 32-bit Windows
v1.44_py3.13.9_x64    → RenderDoc 1.44, Python 3.13.9, 64-bit Windows
```

## Quick Start

### 1. Choose the Right Version

Select the appropriate release directory based on your RenderDoc version, Python version, and platform.

**List available versions**:

```bash
ls python-releases/
```

### 2. Using the Modules

#### Method A: Add to Python Path

```python
import sys
sys.path.append(r'path\to\v1.43_py3.13.9_x64\pymodules')

import renderdoc
import qrenderdoc

# Open a RenderDoc capture file
cap = renderdoc.OpenCapture("capture.rdc")
```

#### Method B: Copy to Project Directory

Copy the `.pyd` and `.dll` files from the `pymodules/` directory to your Python project.

### 3. Configure Type Hints

Each release includes complete Python type stubs for IDE autocomplete and type checking.

> The stubs are modified, using `regenerate-stubs.py` with modules from the Development build branch, not the Release branch.

**VS Code Configuration** (`.vscode/settings.json`):

```json
{
  "python.analysis.extraPaths": [
    "path/to/v1.43_py3.13.9_x64/stubs"
  ]
}
```

**PyCharm Configuration**:

1. Right-click on the `stubs/` directory
2. Select `Mark Directory as` → `Sources Root`

**For more information on using stubs for syntax highlighting, see the [RenderDoc development environment documentation](https://renderdoc.org/docs/python_api/dev_environment.html)**

## System Requirements

### General Requirements

- **Operating System**: Windows 10/11 (x64 or x86).
- **Python**: The Python version specified in the directory. Typically 3.13.9
- **Visual C++ Redistributable**: Usually pre-installed

### Version Example

**v1.43_py3.13.9_x64**:

- Windows 10/11 x64
- Python 3.13.9
- MSBuild 17.11.5
- Platform Toolset v143
- Windows SDK 10.0.26100.0

## Build Information

Each release includes a detailed build report (`REPORT.md`) documenting:

- **Python Environment**: Version, include directory, import libraries
- **Build Tools**: MSBuild, Platform Toolset, Windows SDK versions
- **Visual Studio**: Version and installation path
- **System Information**: OS version, architecture
- **Build Parameters**: Complete MSBuild command lines
- **Test Results**: Module import test status

### Build Environment Example

```text
MSBuild: 17.11.5
Platform Toolset: v143
Windows SDK: 10.0.26100.0
Visual Studio: 2022
Python: 3.13.9
```

**Detailed Documentation**: [v1.43_py3.13.9_x64/README.md](python-releases/v1.43_py3.13.9_x64/README.md)

## FAQ

### Q: How do I choose the correct version?

**A**: Prioritize in this order:

1. **RenderDoc Version** - Must match your RenderDoc installation, especially for remote debugging (e.g., Android pre-installed RenderDoc cmd, version must match exactly)
2. **Python Version** - Must match your Python environment

### Q: Can I use versions across different releases?

**A**: **Not recommended**.

- Different RenderDoc versions may have incompatible APIs
- Different Python versions are incompatible (ABI changes)
- x86 and x64 cannot be mixed

### Q: What if import fails?

**A**: Check the following:

1. Python version matches
2. Both `.pyd` and `.dll` files are copied together
3. Graphics drivers (dx dll) and renderdoc.dll are in the same directory
4. Visual C++ Redistributable is installed
5. Platform matches (x64/x86)?

If the issue persists, [you can use the Dependencies tool to check dependencies](https://github.com/lucasg/Dependencies)

### Q: How do I use type stubs?

**A**: See the [RenderDoc Python API development environment documentation](https://renderdoc.org/docs/python_api/dev_environment.html)

1. For IDE autocomplete and type checking
2. Supports VS Code, PyCharm, and other IDEs
3. Works with mypy for static type checking

### Q: Build from source

**A**: This repository uses automated build scripts (actually a skill), including handling of deprecation warnings (C4996) and C++ syntax downgrade warnings for newer Python versions, without treating warnings as errors.

- Located in `.claude/skills/renderdoc-python-builder/` directory

## Contributing

This repository primarily serves as a distribution channel for pre-built modules. Source code contributions are not accepted.

For:

- **Report Issues**: Submit issues in the main RenderDoc repository
- **Request New Versions**: Open an issue with version requirements
- **Build Issues**: Check the version-specific REPORT.md for build environment details

## License

Build artifacts in this repository follow the RenderDoc license:

- **RenderDoc**: MIT License
- See: <https://github.com/baldurk/renderdoc/blob/master/LICENSE>

## Related Links

- **RenderDoc Official Repository**: <https://github.com/baldurk/renderdoc>
- **RenderDoc Documentation**: <https://renderdoc.org/>
- **Python API Documentation**: <https://renderdoc.org/docs/python_api.html>

## Maintenance Information

- **Main Branch**: `main` for integrating modules. Other branches store build artifacts and are not uploaded to GitHub due to large size
- **Release Branches**: python/v1.43

---

**Note**: This repository contains only pre-built Python modules and build artifacts. It does not include RenderDoc source code. For source code, visit the [RenderDoc Official Repository](https://github.com/baldurk/renderdoc).
