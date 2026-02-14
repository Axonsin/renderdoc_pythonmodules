# RenderDoc Python Module Builder - Claude Skill

Automated build system for RenderDoc Python extension modules on Windows.

## What This Skill Does

This skill automates the entire process of building RenderDoc Python modules:
1. Detects Python environment (version, include dirs, libraries)
2. Locates MSBuild and Visual Studio
3. Fixes Python 3.13+ compatibility issues
4. Builds pyrenderdoc.pyd and qrenderdoc.pyd
5. Copies DLL dependencies
6. Tests module imports
7. Generates detailed build report

## When to Use This Skill

Use this skill when you need to:
- Build RenderDoc Python extension modules
- Compile pyrenderdoc_module or qrenderdoc_module
- Create Python bindings for RenderDoc on Windows
- Build RenderDoc modules for Python 3.13+

## How Claude Will Use This Skill

Claude will automatically invoke this skill when:
- You ask to build RenderDoc Python modules
- You mention compiling pyrenderdoc or qrenderdoc
- You need to create .pyd files for RenderDoc
- You want to build RenderDoc with specific Python version

## Usage

### In Claude Chat

Simply ask Claude to build the modules:
```
"Build the RenderDoc Python modules for Python 3.13"
"Compile pyrenderdoc and qrenderdoc modules"
"Create .pyd files for RenderDoc"
```

Claude will:
1. Invoke this skill
2. Run the build script
3. Show you the results
4. Display the generated REPORT.md

### Command Line

```bash
# Build with current Python
python skill.py

# Build for specific Python version
python skill.py --python-version 3.13

# Build Release configuration
python skill.py --config Release

# Build for x86
python skill.py --platform x86

# Combine options
python skill.py --python-version 3.13 --config Release --platform x64
```

## Output

After building, you'll find:
- `x64/Development/pymodules/renderdoc.pyd` - Core module (~7 MB)
- `x64/Development/pymodules/qrenderdoc.pyd` - Qt UI module (~8 MB)
- `x64/Development/pymodules/renderdoc.dll` - Core library (~70 MB)
- `REPORT.md` - Detailed build report

## Build Report

The generated `REPORT.md` contains:
- Complete environment information (Python, MSBuild, VS versions)
- Build configuration details
- Module build status and timing
- Output file manifest
- Test results
- Usage instructions

Example report section:
```markdown
## Build Environment

### Python Configuration
| **Python Version** | 3.13 |
| **Full Version** | 3.13.9 | packaged by Anaconda, Inc. |
| **Include Directory** | C:\ProgramData\miniconda3\include |

### Build Tools
| **MSBuild** | 17.14.23.42201 |
| **Platform Toolset** | v143 |
```

## Requirements

- Windows 10/11
- Visual Studio 2022 (Community/Enterprise/Professional)
- Python 3.6 or later
- RenderDoc source code
- MSBuild (included with VS2022)

## Features

### Auto-Detection
- Python version and installation
- MSBuild location
- Visual Studio installation
- System architecture

### Python 3.13+ Support
Automatically fixes deprecated API warnings by adding `/wd4996` compiler flag.

### Multi-Version Support
Build for different Python versions:
- Python 3.6 (legacy)
- Python 3.11 (stable)
- Python 3.12 (latest)
- Python 3.13 (current)

### Detailed Reporting
Every build generates a comprehensive report documenting the exact environment and configuration used.

## Testing

Test the built modules:
```bash
python test_modules.py
```

## Troubleshooting

### "Cannot find Python import library"
Ensure Python is properly installed with development files.

### "Cannot find MSBuild.exe"
Install Visual Studio 2022 with "Desktop development with C++" workload.

### "ImportError: DLL load failed"
Check that renderdoc.dll is in the same directory as the .pyd files.

### Build succeeds but import fails
Verify Python version matches the build target.

## Example Workflow

```bash
# 1. Activate desired Python environment
conda activate base

# 2. Build modules (Claude will do this)
python skill.py

# 3. Check the report
cat REPORT.md

# 4. Test the modules
python test_modules.py

# 5. Use the modules
python -c "import sys; sys.path.insert(0, 'x64/Development/pymodules'); import renderdoc"
```

## Technical Details

### Modified Project Files
The skill automatically modifies:
- `qrenderdoc/Code/pyrenderdoc/pyrenderdoc_module.vcxproj`
- `qrenderdoc/Code/pyrenderdoc/qrenderdoc_module.vcxproj`

Adds compiler flag: `/wd4996` (ignore Python 3.13+ deprecation warnings)

### MSBuild Parameters
```
-p:Configuration=Development
-p:Platform=x64
-p:PlatformToolset=v143
-p:PythonIncludeDir=<python include dir>
-p:PythonImportLib=<python lib>
```

## Files in This Skill

- `SKILL.md` - Skill metadata and description
- `skill.py` - Main build script (700+ lines)
- `test_modules.py` - Module testing utility
- `test_report_generation.py` - Report testing
- `README.md` - This file

## Version History

### v1.1.0 (2026-02-15)
- Added automatic report generation (REPORT.md)
- Enhanced environment information collection
- Added build timing statistics
- Improved test result reporting

### v1.0.0 (2026-02-15)
- Initial release
- Auto-detection of build environment
- Python 3.13+ support
- Multi-version Python support

## License

Follows RenderDoc project license.

## Support

For issues or questions:
1. Check the generated REPORT.md
2. Review build logs
3. Verify environment requirements
