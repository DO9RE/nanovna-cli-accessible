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
