# RenderDoc Python Modules - v1.43_py3.13.9_x64

     2→
     3. ## Version Information
     4
     5. - **RenderDoc Version**: 1.43
     6. - **Python Version**: 3.13.9
     7. - **Platform**: x64 (64-bit Windows)
     8. - **Build Configuration**: Development
     9. - **Build Date**: 2025-03-16
    10-    11. ## Files
    12-    13-### Python Modules
    14   | File | Size | Description |
    15-    |------|------|-------------|
    16   | `renderdoc.pyd` | 6.4 MB | Core RenderDoc Python module |
    17   | `qrenderdoc.pyd`    8.5 MB | Qt-based UI Python module |
    18   |
    19   ### Dependencies
    20   | File | Size | Description |
    21   |------|------|-------------|
    22   | `renderdoc.dll` | 24.9 MB | RenderDoc core library (required dependency) |
    23   | `d3dcompiler_47.dll` | 4.6 MB | Direct3D compiler (required dependency) |
    24   |
    25   ### Debug Symbols & Link Libraries
    26   | File | Size | Description |
    27   |------|------|-------------|
    28   | `renderdoc.pdb` | 18 MB | Debug symbols for renderdoc.pyd |
    29   | `qrenderdoc.pdb` | 21 MB | Debug symbols for qrenderdoc.pyd |
    30   | `renderdoc.lib` | 2.0 KB | Import library for renderdoc.pyd |
    31   | `qrenderdoc.lib`    2.2 KB | Import library for qrenderdoc.pyd |
    32   | `renderdoc.exp`    980 B | Export file for renderdoc.pyd |
    33   | `qrenderdoc.exp`    1.1 KB | Export file for qrenderdoc.pyd |

    34   | `renderdoc.pyd`        6372864 3月 16 17:56
    35
## Usage

    36All files must be in the same directory when importing:
    37
    38
    sys.path.append(r'path\to\v1.43_py3.13.9_x64')
    39
    40
    import renderdoc
    41
    import qrenderdoc
    42
    ```
    43   import qrenderdoc
    44   ```
    45   Or copy the `.pyd` files to your Python project directory.

    46   **Type Stubs** This release includes Python type stubs ( IDE autocomplete and type checking:
    52   - **stubs/renderdoc/** (371 files) - Type annotations for renderdoc module
    53   - **stubs/qrenderdoc/** (53 files) - Type annotations for qrenderdoc module

    54
            **Total**: 424 stub files, 3.6 MB

To use type stubs in your project:
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
            - Visual C++ Redistributable (usually pre-installed)
            - **Build Date**: 2025-03-16
