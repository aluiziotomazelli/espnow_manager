import pytest
import os
import subprocess
import glob
import shutil
import sys

# Define the root directory for host tests
HOST_TEST_DIR = os.path.dirname(os.path.abspath(__file__))
IGNORE_DIRS = ["espnow_facade", "build", "__pycache__", ".pytest_cache"]

def get_test_directories():
    dirs = []
    for entry in os.listdir(HOST_TEST_DIR):
        full_path = os.path.join(HOST_TEST_DIR, entry)
        if os.path.isdir(full_path) and entry not in IGNORE_DIRS:
            # Check for CMakeLists.txt to confirm it's a test project
            if os.path.exists(os.path.join(full_path, "CMakeLists.txt")):
                dirs.append(entry)
    return sorted(dirs)

@pytest.mark.parametrize("test_dir", get_test_directories())
def test_host_component(test_dir):
    work_dir = os.path.join(HOST_TEST_DIR, test_dir)
    print(f"\n\n========== Processing: {test_dir} ==========")

    # 1. Cleaner Build (Optional, can be removed for speed if incremental is trusted)
    if os.environ.get("CLEAN_BUILD") == "1":
        print(f"Cleaning build directory in {work_dir}...")
        shutil.rmtree(os.path.join(work_dir, "build"), ignore_errors=True)
        print("Setting target to linux...")
        try:
             subprocess.check_call(["idf.py", "--preview", "set-target", "linux"], cwd=work_dir)
        except (subprocess.CalledProcessError, FileNotFoundError):
             pytest.fail("Failed to set target to linux. Ensure idf.py is in PATH.")

    print(f"Cleaning old coverage data (.gcda) in {work_dir}...")
    gcda_files = glob.glob(os.path.join(work_dir, "**/*.gcda"), recursive=True)
    for f in gcda_files:
        try:
            os.remove(f)
        except OSError:
            pass

    # 2. Build
    print(f"Building in {work_dir}...")
    try:
        ret = subprocess.call(["idf.py", "build"], cwd=work_dir)
    except FileNotFoundError:
        pytest.fail("idf.py not found. Please ensure ESP-IDF is installed and in your PATH.")
    assert ret == 0, f"Build failed for {test_dir}"

    # 3. Find Executable
    # On Linux host, idf.py build usually creates an executable in build/ likely named after project
    # We will look for any executable file in build/ that isn't a directory or typical cmake artifact
    build_dir = os.path.join(work_dir, "build")
    # Finding the elf/executable.
    # Usually it's <project_name>.elf.
    # Let's find files that are executable.
    executables = []
    for f in os.listdir(build_dir):
        full_path = os.path.join(build_dir, f)
        if os.path.isfile(full_path) and os.access(full_path, os.X_OK) and not f.endswith(".bin") and not f.endswith(".map") and not f.endswith(".cmake"):
             executables.append(full_path)

    # Filter out typical CMake params if any
    executables = [e for e in executables if "cmake" not in os.path.basename(e).lower()]

    if not executables:
        # Fallback: try to read project name from CMakeLists.txt or just find *.elf
        elfs = glob.glob(os.path.join(build_dir, "*.elf"))
        if elfs:
            executables = elfs
        else:
            pytest.fail(f"Could not find executable in {build_dir}")

    # Pick the most likely one (shortest name often best heuristic or matching dir name?)
    # usually project(peer_manager_host_test) -> peer_manager_host_test.elf
    exe_path = executables[0]
    print(f"Running executable: {exe_path}")

    # 4. Execute Test
    # pass stdout/stderr through to capture logs
    ret = subprocess.call([exe_path], cwd=work_dir)
    assert ret == 0, f"Test execution failed for {test_dir}"

    # Test Execution Finished
    # Coverage is now handled by generate_coverage.py

if __name__ == "__main__":
    sys.exit(pytest.main(["-s", __file__]))
