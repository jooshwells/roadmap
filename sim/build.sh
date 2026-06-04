#!/bin/bash

# Exit immediately if a command exits with a non-zero status
set -e

# Default variables
BUILD_TYPE="Release"
CLEAN=false
BUILD_DIR="build"
INSTALL_DIR="INSTALL"

# Function to display usage help
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo "Build and install the simulation project."
    echo ""
    echo "Options:"
    echo "  -x          Clean build and install directories before building"
    echo "  -t TYPE     Set the CMake build type (default: Release)"
    echo "  -h, --help  Display this help message"
    exit 1
}

# Parse command-line arguments dynamically
while [[ $# -gt 0 ]]; do
    case $1 in
        -x)
            CLEAN=true
            shift
            ;;
        -t)
            BUILD_TYPE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            echo "Error: Unknown option $1"
            usage
            ;;
    esac
done

# 1. Clean if requested
if [ "$CLEAN" = true ]; then
    echo "Cleaning previous build and install directories..."
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
fi

# 2. Configure
echo "Configuring CMake (Build Type: $BUILD_TYPE)..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# 3. Build
echo "Building project..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

# 4. Install
echo "Installing project..."
cmake --install "$BUILD_DIR" --config "$BUILD_TYPE"

echo "Build and install completed successfully."