---
name: renderdoc-python-builder
description: >
  Builds pyrenderdoc and qrenderdoc Python extension modules (.pyd) for RenderDoc on Windows
  with Visual Studio 2022. All paths and settings are read from assets/config.json, making
  the build portable across different environments without code changes. Automatically detects
  the project root via marker file, finds MSBuild via vswhere, and uses sys.version_info for
  Python version detection. Generates type stubs, copies to python-releases/, and produces
  a detailed build report with git commit hash.
  Use this skill whenever the user wants to build RenderDoc Python modules, compile
  renderdoc.pyd or qrenderdoc.pyd, build pyrenderdoc_module/qrenderdoc_module, create
  Python bindings for RenderDoc, or mentions Python 3.13+ compatibility issues with
  RenderDoc builds. Also trigger when the user asks to rebuild, recompile, or update
  the RenderDoc Python extensions.
---

# RenderDoc Python Module Builder

Automates building RenderDoc Python extension modules (.pyd) for Windows with Visual Studio 2022.

## Configuration

All configurable values live in `assets/config.json`. Edit this file to adapt the build to your environment without touching any Python code.

Key configuration sections:
- **tools** — vswhere path, Windows Kits root, platform toolset
- **project** — root marker file, vcxproj paths, version header, stubs script
- **dependencies** — required DLLs and file descriptions
- **auto_fixes** — Python 3.13+ compatibility flag and XML namespace
- **supported_*** — valid Python versions, configs, platforms

## Requirements

The host machine must have:
- Windows 10/11
- Visual Studio 2022 with C++ desktop workload and MSBuild
- Python 3.6+ (the version to build modules for)
- RenderDoc source code checkout

## Build Steps

Execute the main build script from the RenderDoc project root:

```bash
python .claude/skills/renderdoc-python-builder/scripts/skill.py [OPTIONS]
```

Options:
- `--python-version VERSION` — e.g. `3.13`. Auto-detected from `sys.version_info` if omitted.
- `--config CONFIG` — `Development` (default) or `Release`.
- `--platform PLATFORM` — `x64` (default) or `x86`.

The script performs these steps in order:

1. **Load config** — reads `assets/config.json` for all paths and settings.
2. **Detect project root** — searches upward from the skill directory for the marker file (`renderdoc/api/replay/version.h`).
3. **Detect environment** — Python version via `sys.version_info`, include/libs dirs, MSBuild via vswhere, VS installation, Windows SDK, RenderDoc version, git commit hash.
4. **Fix project files** — patches `.vcxproj` files with the configured warning suppression flag for Python 3.13+.
5. **Clean artifacts** — removes previous `.pyd` and object files.
6. **Build modules** — runs MSBuild for `pyrenderdoc_module` then `qrenderdoc_module`.
7. **Copy dependencies** — copies required DLLs (from config) and verifies others.
8. **Test imports** — imports `renderdoc` and `qrenderdoc` to verify the modules load.
9. **Copy to releases** — copies all built files to `python-releases/v{RD_VER}_py{PY_VER}_{PLATFORM}/` with README, type stubs, and REPORT.md.
10. **Generate report** — writes a `REPORT.md` at the project root with full build details.

## Output

- **Build output**: `{platform}/{config}/pymodules/` (e.g. `x64/Development/pymodules/`)
- **Release directory**: `python-releases/v{RD_VER}_py{PY_VER}_{PLATFORM}/`
- **Build report**: `REPORT.md` at project root and inside the release directory

## Bundled Files

- `scripts/skill.py` — main builder script (run this)
- `scripts/test_modules.py` — standalone module test utility
- `scripts/test_report_generation.py` — tests report generation with simulated build
- `assets/config.json` — build configuration (edit this to customize)
