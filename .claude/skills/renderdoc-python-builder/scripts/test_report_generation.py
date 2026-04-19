#!/usr/bin/env python3
"""
Test script to verify report generation functionality.
"""

import sys
from pathlib import Path

# Add scripts directory to path
sys.path.insert(0, str(Path(__file__).parent))

from skill import RenderDocPyBuilder


def test_report_generation():
    """Test the report generation with minimal build."""

    print("[*] Testing Report Generation")
    print("=" * 60)

    # Create builder instance (this collects all environment info)
    builder = RenderDocPyBuilder(config="Development", platform="x64")

    # Simulate build results
    builder.build_results = {
        "modules": {
            "pyrenderdoc_module": {
                "project": "qrenderdoc/Code/pyrenderdoc/pyrenderdoc_module.vcxproj",
                "success": True,
                "duration": 123.5,
                "stdout": "Build output...",
                "stderr": "",
            },
            "qrenderdoc_module": {
                "project": "qrenderdoc/Code/pyrenderdoc/qrenderdoc_module.vcxproj",
                "success": True,
                "duration": 45.2,
                "stdout": "Build output...",
                "stderr": "",
            },
        },
        "dependencies": {
            "renderdoc.dll": {
                "path": "x64/Development/pymodules/renderdoc.dll",
                "size_mb": 72.5,
                "copied": True,
            },
            "d3dcompiler_47.dll": {
                "path": "x64/Development/pymodules/d3dcompiler_47.dll",
                "size_mb": 4.5,
                "copied": False,
            },
        },
        "tests": {
            "renderdoc": {"status": "Pass", "error": None},
            "qrenderdoc": {"status": "Pass", "error": None},
        },
    }

    # Generate report
    print("\nGenerating report...")
    report_path = builder.generate_report()

    print(f"\n[+] Report generated successfully!")
    print(f"    Location: {report_path}")
    print(f"    Size: {report_path.stat().st_size / 1024:.1f} KB")

    # Display first few lines
    print("\n" + "=" * 60)
    print("Report Preview (first 30 lines):")
    print("=" * 60)
    report_content = report_path.read_text(encoding="utf-8")
    lines = report_content.split("\n")
    for i, line in enumerate(lines[:30], 1):
        print(f"{i:3d}: {line}")

    print("\n...")
    print(f"\n[*] Total lines: {len(lines)}")


if __name__ == "__main__":
    try:
        test_report_generation()
    except Exception as e:
        print(f"\n[-] Error: {e}")
        import traceback

        traceback.print_exc()
        sys.exit(1)