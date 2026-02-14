#!/bin/bash
#
# Build script for nanovna-cli-accessible on macOS
# This script automates the build process using CMake
#

set -e  # Exit on error

# Clear screen for better visibility
clear

echo "=========================================="
echo " NanoVNA CLI Accessible - macOS Build"
echo "=========================================="
echo ""

# Parse command-line arguments
USE_BUNDLED_PORTAUDIO=0
SKIP_PORTAUDIO_PROMPT=0

show_help() {
    echo "Usage: ./build-macos.sh [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --bundled-portaudio    Download and use bundled static PortAudio (recommended for distribution)"
    echo "  --system-portaudio     Use system/Homebrew installed PortAudio"
    echo "  --help                 Show this help message"
    echo ""
    echo "If no option is specified, you will be prompted to choose."
    echo ""
    exit 0
}

for arg in "$@"; do
    case $arg in
        --bundled-portaudio)
            USE_BUNDLED_PORTAUDIO=1
            SKIP_PORTAUDIO_PROMPT=1
            shift
            ;;
        --system-portaudio)
            USE_BUNDLED_PORTAUDIO=0
            SKIP_PORTAUDIO_PROMPT=1
            shift
            ;;
        --help)
            show_help
            ;;
        *)
            ;;
    esac
done

# Check for dependencies
echo "[0/7] Checking dependencies..."

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found!"
    echo "Install using: brew install cmake"
    exit 1
fi
echo "      ✓ CMake found: $(cmake --version | head -1)"

# Check for compiler
if ! command -v clang++ &> /dev/null; then
    echo "ERROR: clang++ not found!"
    echo "Install Xcode Command Line Tools using: xcode-select --install"
    exit 1
fi
echo "      ✓ clang++ found: $(clang++ --version | head -1)"

# Check for PortAudio and ask user for preference if not specified
if [ $SKIP_PORTAUDIO_PROMPT -eq 0 ]; then
    echo "PortAudio Library Options:"
    echo "  1) Use system/Homebrew PortAudio (requires 'brew install portaudio')"
    echo "  2) Download and use bundled static PortAudio (recommended for distribution)"
    echo ""
    read -p "Select option (1 or 2) [default: 2]: " PORTAUDIO_OPTION
    
    if [ -z "$PORTAUDIO_OPTION" ]; then
        PORTAUDIO_OPTION=2
    fi
    
    if [ "$PORTAUDIO_OPTION" == "2" ]; then
        USE_BUNDLED_PORTAUDIO=1
    else
        USE_BUNDLED_PORTAUDIO=0
    fi
fi

if [ $USE_BUNDLED_PORTAUDIO -eq 0 ]; then
    # Check for system/Homebrew PortAudio
    if ! brew list portaudio &> /dev/null && [ ! -f /usr/local/lib/libportaudio.dylib ] && [ ! -f /opt/homebrew/lib/libportaudio.dylib ]; then
        echo "WARNING: PortAudio not found via Homebrew!"
        echo "Audio functionality will be limited."
        echo "Install using: brew install portaudio"
        echo ""
        read -p "Continue without PortAudio? (y/N) " -n 1 -r
        echo ""
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    else
        echo "      ✓ System/Homebrew PortAudio found"
    fi
else
    echo "      ✓ Will use bundled static PortAudio"
fi

echo "      Done."
echo ""

# Check if build directory exists
if [ -d "build" ]; then
    echo "[1/7] Removing existing build directory..."
    rm -rf build
    echo "      Done."
else
    echo "[1/7] No existing build directory found."
fi

# Download and build bundled PortAudio if requested
if [ $USE_BUNDLED_PORTAUDIO -eq 1 ]; then
    echo "[2/7] Setting up bundled PortAudio..."
    
    PORTAUDIO_VERSION="19.7.0"
    PORTAUDIO_DIR="portaudio"
    PORTAUDIO_BUILD_DIR="${PORTAUDIO_DIR}/build"
    PORTAUDIO_INSTALL_DIR="$(pwd)/portaudio_install"
    
    # Check if PortAudio is already built
    if [ -f "${PORTAUDIO_INSTALL_DIR}/lib/libportaudio.a" ]; then
        echo "      ✓ Bundled PortAudio already built"
    else
        echo "      Downloading PortAudio ${PORTAUDIO_VERSION}..."
        
        # Remove old PortAudio directory if exists
        if [ -d "$PORTAUDIO_DIR" ]; then
            rm -rf "$PORTAUDIO_DIR"
        fi
        
        # Try multiple download sources for reliability
        # GitHub is primary, with fallback options
        DOWNLOAD_SUCCESS=0
        
        # List of URLs to try (in order of preference)
        declare -a PORTAUDIO_URLS=(
            "https://github.com/PortAudio/portaudio/archive/refs/tags/v${PORTAUDIO_VERSION}.tar.gz"
            "https://files.portaudio.com/archives/pa_stable_v${PORTAUDIO_VERSION//./_}.tgz"
        )
        
        for PORTAUDIO_URL in "${PORTAUDIO_URLS[@]}"; do
            echo "      Trying: ${PORTAUDIO_URL}"
            
            if curl -L "${PORTAUDIO_URL}" -o portaudio.tgz 2>&1; then
                # Check if download succeeded
                if [ -f "portaudio.tgz" ] && [ -s "portaudio.tgz" ]; then
                    FILE_SIZE=$(wc -c < portaudio.tgz)
                    echo "      Downloaded: ${FILE_SIZE} bytes"
                    
                    # Check if file size is reasonable (at least 1MB)
                    if [ ${FILE_SIZE} -ge 1000000 ]; then
                        DOWNLOAD_SUCCESS=1
                        echo "      ✓ Download successful from this source"
                        break
                    else
                        echo "      ✗ File too small (${FILE_SIZE} bytes), trying next source..."
                        rm -f portaudio.tgz
                    fi
                else
                    echo "      ✗ Download failed, trying next source..."
                fi
            else
                echo "      ✗ Download failed, trying next source..."
            fi
        done
        
        if [ ${DOWNLOAD_SUCCESS} -eq 0 ]; then
            echo "ERROR: Failed to download PortAudio from all sources!"
            echo "Please check your internet connection or try again later."
            echo ""
            echo "Attempted URLs:"
            for url in "${PORTAUDIO_URLS[@]}"; do
                echo "  - ${url}"
            done
            exit 1
        fi
        
        # Verify it's actually a gzip/tar file (not an error page)
        if command -v file &> /dev/null; then
            FILE_TYPE=$(file portaudio.tgz)
            echo "      File type: ${FILE_TYPE}"
            
            if ! echo "${FILE_TYPE}" | grep -qi "gzip\|compressed"; then
                echo "ERROR: Downloaded file is not a gzip archive!"
                echo "This likely means the download redirected to an error page."
                echo ""
                echo "First 500 bytes of downloaded file:"
                head -c 500 portaudio.tgz
                echo ""
                exit 1
            fi
        fi
        
        # Extract
        echo "      Extracting PortAudio..."
        if ! tar -xzf portaudio.tgz; then
            echo "ERROR: Failed to extract PortAudio archive!"
            echo "The file may be corrupted. First 500 bytes:"
            head -c 500 portaudio.tgz
            echo ""
            exit 1
        fi
        
        # Find extracted directory (may be named portaudio, portaudio-19.7.0, etc.)
        EXTRACTED_DIR=$(find . -maxdepth 1 -type d -name "portaudio*" ! -name "$PORTAUDIO_DIR" | head -1)
        
        if [ -z "$EXTRACTED_DIR" ]; then
            echo "ERROR: PortAudio directory not found after extraction!"
            exit 1
        fi
        
        # Rename to standard name if different
        if [ "$EXTRACTED_DIR" != "./$PORTAUDIO_DIR" ]; then
            mv "$EXTRACTED_DIR" "$PORTAUDIO_DIR"
        fi
        
        rm portaudio.tgz
        
        # Build PortAudio
        echo "      Building PortAudio (this may take a few minutes)..."
        mkdir -p "$PORTAUDIO_BUILD_DIR"
        cd "$PORTAUDIO_BUILD_DIR"
        
        # Configure with static library and minimal dependencies
        if ! cmake .. \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_INSTALL_PREFIX="$PORTAUDIO_INSTALL_DIR" \
            -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13 \
            -DPA_BUILD_SHARED=OFF \
            -DPA_BUILD_STATIC=ON \
            -DPA_DISABLE_INSTALL=OFF; then
            echo "ERROR: CMake configuration of PortAudio failed!"
            exit 1
        fi
        
        # Build with all available cores
        if command -v sysctl &> /dev/null; then
            CORES=$(sysctl -n hw.ncpu)
        else
            CORES=4
        fi
        
        if ! make -j${CORES}; then
            echo "ERROR: Building PortAudio failed!"
            exit 1
        fi
        
        # Install to our local directory
        if ! make install; then
            echo "ERROR: Installing PortAudio failed!"
            exit 1
        fi
        
        cd ../..
        
        # Verify installation
        if [ ! -f "${PORTAUDIO_INSTALL_DIR}/lib/libportaudio.a" ]; then
            echo "ERROR: PortAudio static library not found after installation!"
            exit 1
        fi
        
        if [ ! -f "${PORTAUDIO_INSTALL_DIR}/include/portaudio.h" ]; then
            echo "ERROR: PortAudio header file not found after installation!"
            exit 1
        fi
        
        echo "      ✓ Bundled PortAudio built successfully"
    fi
    
    echo "      Done."
else
    echo "[2/7] Skipping bundled PortAudio setup."
fi

# Create fresh build directory
echo "[3/7] Creating fresh build directory..."
mkdir build
cd build
echo "      Done."

# Run CMake
echo "[4/7] Running CMake..."
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=Release"

if [ $USE_BUNDLED_PORTAUDIO -eq 1 ]; then
    CMAKE_ARGS="${CMAKE_ARGS} -DUSE_BUNDLED_PORTAUDIO=ON -DPORTAUDIO_ROOT=$(pwd)/../portaudio_install"
else
    CMAKE_ARGS="${CMAKE_ARGS} -DUSE_BUNDLED_PORTAUDIO=OFF"
fi

if ! cmake .. ${CMAKE_ARGS}; then
    echo "ERROR: CMake configuration failed!"
    exit 1
fi
echo "      Done."

# Run make with parallel builds for faster compilation
echo "[5/7] Building with make..."
# Detect number of CPU cores for parallel builds
if command -v sysctl &> /dev/null; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4  # Default to 4 cores if sysctl not available
fi

if ! make -j${CORES}; then
    echo "ERROR: Build failed!"
    exit 1
fi
echo "      Done."

# Copy necessary files to build directory
echo "[6/7] Copying necessary files to build directory..."

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

# Copy Training directory
echo "      Copying Training directory..."
mkdir -p Training
cp -r ../Training/*.csv Training/ 2>/dev/null || echo "      Warning: No training CSV files found"
cp -r ../Training/README.md Training/ 2>/dev/null || echo "      Warning: No training README found"

# Copy MIDI preset files
echo "      Copying MIDI preset files..."
mkdir -p midi
cp -r ../midi/*.cfg midi/ 2>/dev/null || echo "      Warning: No MIDI preset files found"

echo "      Done."

# Verify important files are present
echo "[7/7] Verifying build..."
if [ ! -f "nanovna-cli" ]; then
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

# Update version stamps in source documentation (before copying to build)
echo ""
echo "[7/7] Updating version stamps in documentation..."
VERSION=$(cat ../VERSION 2>/dev/null || echo "Unknown")
BUILD_DATE=$(date "+%Y-%m-%d %H:%M:%S")

# Function to update version in HTML files
update_html_version() {
    local file="$1"
    local lang="$2"
    if [ -f "$file" ]; then
        if [ "$lang" = "DE" ]; then
            # German version line
            sed -i.bak "s|<p><strong>Version:</strong> Beta <strong>Datum:</strong> [^<]*<strong>|<p><strong>Version:</strong> ${VERSION} <strong>Datum:</strong> ${BUILD_DATE} <strong>|" "$file"
        else
            # English version line
            sed -i.bak "s|<p><strong>Version:</strong> Beta <strong>Date:</strong> [^<]*<strong>|<p><strong>Version:</strong> ${VERSION} <strong>Date:</strong> ${BUILD_DATE} <strong>|" "$file"
        fi
        rm -f "${file}.bak"
        echo "      ✓ Updated version in $(basename $file)"
    fi
}

# Update HTML documentation files in source directory
update_html_version "../doc/manuals/USER_MANUAL_DE.html" "DE"
update_html_version "../doc/manuals/USER_MANUAL_EN.html" "EN"
update_html_version "../doc/beta-testing/BETA_TESTING_DE.html" "DE"
update_html_version "../doc/beta-testing/BETA_TESTING_EN.html" "EN"

# Update README.md in parent directory
if [ -f "../README.md" ]; then
    # Add or update version badge after title
    if grep -q "^<!-- BUILD_VERSION -->" ../README.md; then
        # Update existing version line
        sed -i.bak "s|^<!-- BUILD_VERSION -->.*|<!-- BUILD_VERSION --> **Build Version:** ${VERSION} (${BUILD_DATE})|" ../README.md
    else
        # Add version line after title (line 3, after empty line)
        sed -i.bak "3i\\
<!-- BUILD_VERSION --> **Build Version:** ${VERSION} (${BUILD_DATE})\\
" ../README.md
    fi
    rm -f "../README.md.bak"
    echo "      ✓ Updated version in README.md"
fi

echo "      Done."

# Copy doc directory (now with updated version stamps)
echo "      Copying documentation with updated versions..."
mkdir -p doc
cp -r ../doc/* doc/ 2>/dev/null || echo "      Warning: No documentation files found"
echo "      Done."

# Make executable runnable
echo "[7/7] Setting executable permissions..."
chmod +x nanovna-cli
echo "      Done."

# Create Finder launcher script in build directory
echo "[7/7] Creating Finder launcher script..."
cat > nanovna-cli.command << 'EOF'
#!/bin/bash
#
# NanoVNA CLI Accessible - macOS Finder Launcher
# This script allows launching the application by double-clicking from Finder
#

# Suppress stderr to minimize Terminal noise during startup
exec 2>/dev/null

# Get the directory where this script is located (the build directory)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Change to the script's directory (build directory where all files are)
cd "$SCRIPT_DIR" || {
    # Re-enable stderr for error messages
    exec 2>&1
    clear
    echo "========================================"
    echo " ERROR"
    echo "========================================"
    echo ""
    echo "Failed to change to script directory!"
    echo "Expected: $SCRIPT_DIR"
    echo ""
    read -p "Press Enter to close this window..."
    exit 1
}

# Verify we're in the right directory
BUILD_DIR="$(pwd)"

# Check if the executable exists
if [ ! -f "nanovna-cli" ]; then
    # Re-enable stderr for error messages
    exec 2>&1
    clear
    echo "========================================"
    echo " ERROR"
    echo "========================================"
    echo ""
    echo "Executable not found in build directory!"
    echo "Current directory: $BUILD_DIR"
    echo ""
    echo "Please ensure all files are present."
    echo ""
    read -p "Press Enter to close this window..."
    exit 1
fi

# Re-enable stderr for the application
exec 2>&1

# Clear the screen to hide Terminal login messages
clear

# Add spacing to push any remaining messages off screen
# Use a reduced number since we clear again afterward
i=0
while [ $i -lt 20 ]; do
    echo
    i=$((i+1))
done

# Clear again for clean display
clear

echo "========================================"
echo " NanoVNA CLI Accessible"
echo "========================================"
echo ""
echo "Starting application..."
echo ""

# Launch the application
# Note: Remove the -d flag if you don't want debug output in distribution
./nanovna-cli
EXIT_CODE=$?

# Show exit message
echo ""
echo "========================================"
echo " Application exited"
echo "========================================"
echo ""
read -r -p "Press Enter to close this window..."

exit $EXIT_CODE
EOF

chmod +x nanovna-cli.command
echo "      Done."

echo ""
echo "=========================================="
echo " Build completed successfully!"
echo "=========================================="
if [ $USE_BUNDLED_PORTAUDIO -eq 1 ]; then
    echo "PortAudio: Bundled static library"
else
    echo "PortAudio: System/Homebrew library"
fi
echo "Executable location: build/nanovna-cli"
echo "Finder launcher: build/nanovna-cli.command"
echo "Language files: build/Languages/"
echo "Config files: build/config/"
echo "Band plans: build/bandplans/"
echo "Logs will be saved to: build/logs/"
echo "Exports will be saved to: build/Export/"
echo "Training files: build/Training/"
echo "MIDI presets: build/midi/"
echo "Documentation: build/doc/"
echo ""
echo "To run the application:"
echo ""
echo "  Option 1 - From Terminal:"
echo "    cd build"
echo "    ./nanovna-cli"
echo ""
echo "  Option 2 - From Finder (double-click):"
echo "    In Finder, navigate to the build directory"
echo "    Double-click: nanovna-cli.command"
echo ""
echo "To create a distribution package:"
echo "  Simply zip or package the entire build/ directory"
echo "  All necessary files including the launcher are included"
echo ""
echo "Build options for next time:"
echo "  --bundled-portaudio    Use bundled static PortAudio"
echo "  --system-portaudio     Use system/Homebrew PortAudio"
echo ""
