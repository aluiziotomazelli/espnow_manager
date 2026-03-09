# generate_coverage.py
import os
import subprocess
import glob
import sys
import shutil

# Define paths
HOST_TEST_DIR = os.path.dirname(os.path.abspath(__file__))
COVERAGE_DIR = os.path.join(HOST_TEST_DIR, "coverage")
HTML_REPORT_DIR = os.path.join(COVERAGE_DIR, "html")
UNIFIED_INFO = os.path.join(COVERAGE_DIR, "unified_coverage.info")
IGNORE_DIRS = ["espnow_facade", "build", "__pycache__", ".pytest_cache", "coverage"]

def get_coverage_ignore_patterns():
    ignore_file = os.path.join(HOST_TEST_DIR, "coverage_ignore.txt")
    patterns = []
    if os.path.exists(ignore_file):
        with open(ignore_file, "r") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#"):
                    patterns.append(line)
    return patterns

def get_test_directories():
    dirs = []
    for entry in os.listdir(HOST_TEST_DIR):
        full_path = os.path.join(HOST_TEST_DIR, entry)
        if os.path.isdir(full_path) and entry not in IGNORE_DIRS:
            if os.path.exists(os.path.join(full_path, "CMakeLists.txt")):
                dirs.append(entry)
    return sorted(dirs)

# Absolute paths are maintained for better local HTML report integration
def print_simple_summary(info_file, label, summary_file=None):
    """Print a clean summary from lcov --summary output and optionally write to file."""
    try:
        # Added --ignore-errors gcov to be more permissive
        output = subprocess.check_output(["lcov", "--summary", info_file, "--ignore-errors", "gcov"], stderr=subprocess.STDOUT, text=True)
        header = f"{label}:"
        print(header)
        if summary_file:
            summary_file.write(header + "\n")

        for line in output.splitlines():
            # Match lines that look like: "  lines......: 88.2% (15 of 17 lines)"
            # or "  functions..: 83.3% (5 of 6 functions)"
            if ":" in line and ("lines" in line or "functions" in line):
                clean_line = line.strip()
                print(f"  {clean_line}")
                if summary_file:
                    summary_file.write(f"  {clean_line}\n")

        if summary_file:
            summary_file.write("\n")
    except Exception:
        msg = f"  Summary not available for {label}"
        print(msg)
        if summary_file:
            summary_file.write(msg + "\n\n")

def main():
    if not os.path.exists(COVERAGE_DIR):
        os.makedirs(COVERAGE_DIR)

    if os.path.exists(UNIFIED_INFO):
        os.remove(UNIFIED_INFO)

    summary_path = os.path.join(COVERAGE_DIR, "summary.txt")

    collected_info_files = []
    ignore_patterns = get_coverage_ignore_patterns()

    print("========== Generating Coverage Reports ==========\n")

    with open(summary_path, "w") as summary_file:
        summary_file.write("========== Host Test Coverage Summary ==========\n\n")

        for test_dir in get_test_directories():
            work_dir = os.path.join(HOST_TEST_DIR, test_dir)
            build_dir = os.path.join(work_dir, "build")

            if not os.path.exists(build_dir):
                continue

            coverage_info = os.path.join(build_dir, "coverage.info")
            coverage_filtered = os.path.join(build_dir, "coverage_filtered.info")

            # Capture coverage
            cmd_capture = ["lcov", "--capture", "--directory", build_dir, "--output-file", coverage_info, "--quiet"]
            try:
                subprocess.check_call(cmd_capture, cwd=work_dir)
            except subprocess.CalledProcessError:
                cmd_capture_fallback = ["lcov", "--capture", "--directory", build_dir, "--output-file", coverage_info, "--ignore-errors", "gcov", "--quiet"]
                try:
                    subprocess.check_call(cmd_capture_fallback, cwd=work_dir)
                except subprocess.CalledProcessError:
                     continue

            # Filter coverage
            source_info = coverage_info
            if ignore_patterns:
                cmd_remove = ["lcov", "--remove", coverage_info] + ignore_patterns + ["--output-file", coverage_filtered, "--quiet"]
                try:
                    subprocess.check_call(cmd_remove, cwd=work_dir)
                    source_info = coverage_filtered
                except subprocess.CalledProcessError:
                    pass

            # Collected for merging
            collected_info_files.append(source_info)

            # Print simple summary and log to file
            print_simple_summary(source_info, test_dir, summary_file)

        if not collected_info_files:
            print("No coverage data found. Please run tests first.")
            sys.exit(1)

        # Merge all coverage data
        print("\nMerging coverage data...")
        cmd_merge = ["lcov"]
        for f in collected_info_files:
            cmd_merge.extend(["-a", f])
        cmd_merge.extend(["-o", UNIFIED_INFO, "--quiet"])

        try:
            subprocess.check_call(cmd_merge, cwd=HOST_TEST_DIR)
        except subprocess.CalledProcessError:
            print("Failed to merge coverage data.")
            sys.exit(1)

        print("")
        print_simple_summary(UNIFIED_INFO, "UNIFIED REPORT", summary_file)

    print(f"\nGenerating HTML report in {HTML_REPORT_DIR}...")
    cmd_genhtml = ["genhtml", UNIFIED_INFO, "--output-directory", HTML_REPORT_DIR, "--quiet", "--title", "Host Test Coverage"]
    try:
        subprocess.check_call(cmd_genhtml, cwd=HOST_TEST_DIR)
        print(f"\nSuccess! Full report available at: file://{HTML_REPORT_DIR}/index.html")
        print(f"Text summary available at: {summary_path}")
    except subprocess.CalledProcessError:
        print("Failed to generate HTML report. Tracefile might contain invalid paths.")
        sys.exit(1)

if __name__ == "__main__":
    main()
