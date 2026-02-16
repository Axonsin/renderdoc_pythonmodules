import sys
import os

# Add the module directory to Python path
module_dir = r"c:\Users\13908\Desktop\works\renderdoc_pythonmodules\python-releases\v1.42_py3.13.9_x64"
sys.path.insert(0, module_dir)

print(f"Python version: {sys.version}")
print(f"Module directory: {module_dir}")
print(f"pyd files in directory:")
for f in os.listdir(module_dir):
    if f.endswith('.pyd'):
        print(f"  - {f}")
print()

try:
    import renderdoc
    print("[OK] Successfully imported renderdoc module")
    print()

    # Test getting version string
    version = renderdoc.GetVersionString()
    print(f"[OK] RenderDoc Version: {version}")

    # Test getting commit hash
    commit_hash = renderdoc.GetCommitHash()
    print(f"[OK] Commit Hash: {commit_hash}")

    # Test if it's a release build
    is_release = renderdoc.IsReleaseBuild()
    print(f"[OK] Is Release Build: {is_release}")

    # Test getting log file location
    log_file = renderdoc.GetLogFile()
    print(f"[OK] Log File: {log_file}")

    # Test getting supported device protocols
    protocols = renderdoc.GetSupportedDeviceProtocols()
    print(f"[OK] Supported Device Protocols: {protocols}")

    print()
    print("[SUCCESS] All tests passed! The renderdoc module is working correctly.")

except ImportError as e:
    print(f"[ERROR] Failed to import renderdoc module: {e}")
    sys.exit(1)
except Exception as e:
    print(f"[ERROR] Error during testing: {e}")
    import traceback
    traceback.print_exc()
    sys.exit(1)
