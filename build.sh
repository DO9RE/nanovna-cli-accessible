#!/bin/bash
#
# Build script for nanovna-cli-accessible
# This script automates the build process using CMake and MinGW
#

set -e  # Exit on error

# Clear screen for better visibility
clear

echo "=========================================="
echo " NanoVNA CLI Accessible - Build Script"
echo "=========================================="
echo ""

# Check if build directory exists
if [ -d "build" ]; then
    echo "[1/6] Removing existing build directory..."
    rm -rf build
    echo "      Done."
else
    echo "[1/6] No existing build directory found."
fi

# Create fresh build directory
echo "[2/6] Creating fresh build directory..."
mkdir build
cd build
echo "      Done."

# Run CMake
echo "[3/6] Running CMake..."
if ! cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release; then
    echo "ERROR: CMake configuration failed!"
    exit 1
fi
echo "      Done."

# Run make with parallel builds for faster compilation
echo "[4/6] Building with MinGW make..."
# Detect number of CPU cores for parallel builds
if command -v nproc &> /dev/null; then
    CORES=$(nproc)
else
    CORES=4  # Default to 4 cores if nproc not available
fi

if ! mingw32-make -j${CORES}; then
    echo "ERROR: Build failed!"
    exit 1
fi
echo "      Done."

# Copy necessary files to build directory
echo "[5/6] Copying necessary files to build directory..."

# Copy language files
echo "      Copying language files..."
mkdir -p Languages
cp -r ../Languages/*.lng Languages/ 2>/dev/null || echo "      Warning: No language files found"

# Copy config files
echo "      Copying config files..."
mkdir -p config
cp -r ../config/*.cfg config/ 2>/dev/null || echo "      Warning: No config files found"

# Copy band plan files
echo "      Copying band plan files..."
mkdir -p bandplans
cp -r ../bandplans/*.ini bandplans/ 2>/dev/null || echo "      Warning: No band plan files found"

# Create logs directory
echo "      Creating logs directory..."
mkdir -p logs

# Create Export directory
echo "      Creating Export directory..."
mkdir -p Export

echo "      Done."

# Verify important files are present
echo "[6/6] Verifying build..."
if [ ! -f "nanovna-cli.exe" ]; then
    echo "ERROR: Executable not found!"
    exit 1
fi

if [ ! -d "Languages" ] || [ -z "$(ls -A Languages 2>/dev/null)" ]; then
    echo "WARNING: Language files not found in build directory!"
fi

if [ ! -d "config" ] || [ -z "$(ls -A config 2>/dev/null)" ]; then
    echo "WARNING: Config files not found in build directory!"
fi

if [ ! -d "bandplans" ] || [ -z "$(ls -A bandplans 2>/dev/null)" ]; then
    echo "WARNING: Band plan files not found in build directory!"
fi

echo "      Done."

echo ""
echo "=========================================="
echo " Build completed successfully!"
echo "=========================================="
echo "Executable location: build/nanovna-cli.exe"
echo "Language files: build/Languages/"
echo "Config files: build/config/"
echo "Band plans: build/bandplans/"
echo "Logs will be saved to: build/logs/"
echo "Exports will be saved to: build/Export/"
echo ""
