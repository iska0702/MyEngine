#!/bin/bash
set -e

# ==============================================================================
# MyEngine - Apple Silicon (macOS) Build Script
# Targets: Apple Silicon (M1/M2/M3/M4+) natively using Metal, MetalKit & MetalFX
# ==============================================================================

echo "========================================================================"
echo " Building MyEngine for macOS (Apple Silicon arm64)"
echo " Metal Backend + MetalFX + MSL Shaders + Cocoa Windowing"
echo "========================================================================"

# Verify running on macOS
UNAME_S=$(uname -s)
if [ "$UNAME_S" != "Darwin" ]; then
    echo "Warning: Not running on macOS (current OS: $UNAME_S)."
    echo "This script is designed for macOS with Apple Silicon."
fi

# Verify CMake is installed
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake could not be found. Please install CMake via Homebrew: brew install cmake"
    exit 1
fi

# Directory paths
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/build_macos"

echo "Configuration directory: ${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

# Run CMake Configuration
echo ""
echo "--> Configuring CMake (arm64, Metal, C++20)..."
cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Run Compilation
NCPU=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo ""
echo "--> Compiling with ${NCPU} parallel jobs..."
cmake --build "${BUILD_DIR}" --config Release -j "${NCPU}"

echo ""
echo "========================================================================"
echo " Build SUCCESSFUL!"
echo " Output App Bundle: ${BUILD_DIR}/MyEngine.app"
echo " To run the engine:"
echo "   open ${BUILD_DIR}/MyEngine.app"
echo "   or: ${BUILD_DIR}/MyEngine.app/Contents/MacOS/MyEngine"
echo "========================================================================"
