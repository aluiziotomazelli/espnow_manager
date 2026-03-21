#!/bin/bash

# Navigate to host_test directory
cd host_test

# Create build directory and navigate into it
mkdir -p build && cd build

# Configure the project with CMake
cmake ..

# Build all tests
make build_all_tests

# Clean previous coverage data
make coverage_clean

# Run tests with output on failure
ctest --output-on-failure

# Generate unified coverage report
make unified_coverage

#Back to host_test directory
cd ..

#Open html report
xdg-open coverage/src/index.html .
