# RenderDoc Python Module Builder

Automates building RenderDoc Python extension modules (.pyd files) for Windows with Visual Studio.

## Description

Builds pyrenderdoc and qrenderdoc Python extension modules for RenderDoc graphics debugging tool. Supports multiple Python versions (3.6-3.13), automatically detects build environment, configures MSBuild parameters, fixes Python 3.13+ compatibility issues, generates detailed build reports, and automatically copies built modules to the python-releases directory for version tracking.

Use this skill when:
- User needs to build RenderDoc Python modules (renderdoc.pyd, qrenderdoc.pyd)
- User asks to compile Python extensions for RenderDoc
- User wants to build pyrenderdoc_module or qrenderdoc_module
- User needs to create Python bindings for RenderDoc on Windows
- User mentions building RenderDoc with Python 3.13+ (needs /wd4996 fix)

## Capabilities

- Auto-detects Python environment (version, include dirs, import libraries)
- Finds MSBuild and Visual Studio installation
- Configures correct PlatformToolset (v143 for VS2022)
- Fixes Python 3.13+ deprecation warnings automatically
- Builds both pyrenderdoc and qrenderdoc modules
- Copies DLL dependencies (renderdoc.dll, d3dcompiler_47.dll)
- Tests module imports
- **Automatically copies built modules to python-releases/ directory**
  - Organized by Python version and platform (e.g., py3.13-x64/)
  - Includes README.md with build information
  - Ready for git version control
- Generates detailed build report (REPORT.md) with:
  - Complete environment information
  - Build tool versions
  - Module build status and timing
  - Output file manifest
  - Test results

## Requirements

- Windows 10/11
- Visual Studio 2022 with C++ tools
- Python 3.6+ (any version)
- RenderDoc source code
- MSBuild

## Usage

```bash
# Basic usage
python skill.py

# With options
python skill.py --python-version 3.13 --config Release --platform x64
```

## Author

RenderDoc Python Modules Team

## Version

1.1.0

## Files

- `skill.py` - Main builder script
- `test_modules.py` - Module testing utility
- `SKILL.md` - This file
