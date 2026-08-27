#!/usr/bin/env bash
set -e

# Change to project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT_DIR}"

echo "==> Running Doxygen..."
mkdir -p build/docs/doxygen
doxygen Doxyfile

echo "==> Generating consolidated API.md..."
python3 scripts/generate_api_md.py

echo "==> Done! API.md has been successfully updated."
