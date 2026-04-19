---
name: renderdoc-python-builder
description: >
  Builds pyrenderdoc and qrenderdoc Python extension modules (.pyd) for RenderDoc on Windows
  with Visual Studio 2022. Automatically detects Python version and build environment,
  fixes Python 3.13+ compatibility, generates type stubs, copies to python-releases/,
  and produces a detailed build report with git commit hash.
  Use this skill whenever the user wants to build RenderDoc Python modules, compile
  renderdoc.pyd or qrenderdoc.pyd, build pyrenderdoc_module/qrenderdoc_module, create
  Python bindings for RenderDoc, or mentions Python 3.13+ compatibility issues with
  RenderDoc builds. Also trigger when the user asks to rebuild, recompile, or update
  the RenderDoc Python extensions.
---

# RenderDoc Python Module Builder

Automates building RenderDoc Python extension modules (.pyd) for Windows with Visual Studio 2022.

## Requirements

The host machine must have:
- Windows 10/11
- Visual Studio 2022 with C++ desktop workload and MSBuild
- Python 3.6+ (the version to build modules for)
- RenderDoc source code checkout (the project root is the parent of `.claude/`)

## Build Steps

Execute the main build script from the RenderDoc project root:

```bash
python .claude/skills/renderdoc-python-builder/scripts/skill.py [OPTIONS]
```

Options:
- `--python-version VERSION` — e.g. `3.13`. Auto-detected from current interpreter if omitted.
- `--config CONFIG` — `Development` (default) or `Release`.
- `--platform PLATFORM` — `x64` (default) or `x86`.

The script performs these steps in order:

1. **Detect environment** — Python version, include dirs, import libs, MSBuild path, VS installation, Windows SDK, RenderDoc version, and git commit hash.
2. **Fix project files** — patches `.vcxproj` files with `/wd4996` to suppress Python 3.13+ deprecation warnings.
3. **Clean artifacts** — removes previous `.pyd` and object files.
4. **Build modules** — runs MSBuild for `pyrenderdoc_module` then `qrenderdoc_module`.
5. **Copy dependencies** — copies `renderdoc.dll` and verifies `d3dcompiler_47.dll`.
6. **Test imports** — imports `renderdoc` and `qrenderdoc` to verify the modules load.
7. **Copy to releases** — copies all built files to `python-releases/v{RD_VER}_py{PY_VER}_{PLATFORM}/` with README, type stubs, and a per-release REPORT.md.
8. **Generate report** — writes a `REPORT.md` at the project root with full build details including git commit hash.

## Output

- **Build output**: `{platform}/{config}/pymodules/` (e.g. `x64/Development/pymodules/`)
- **Release directory**: `python-releases/v{RD_VER}_py{PY_VER}_{PLATFORM}/`
- **Build report**: `REPORT.md` at project root and inside the release directory

The report includes: environment info, build tool versions, git commit hash, module build status and timing, output file manifest, and test results.

## Bundled Files

- `scripts/skill.py` — main builder script (run this)
- `scripts/test_modules.py` — standalone module test utility
- `scripts/test_report_generation.py` — tests report generation with simulated build
- `assets/config.example.json` — example configuration schema
