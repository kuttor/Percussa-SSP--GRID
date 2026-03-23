#!/bin/bash
# ── GRID first-time configure ────────────────────────────────────────────
# Run once after cloning. Sets up cmake with cross-compile toolchain.
# Adjust paths below to match your system.
set -e

# ── Defaults (override with env vars) ────────────────────────────────────
JUCE_DIR="${JUCE_DIR:-$HOME/Code/JUCE}"
VSTSDK="${VSTSDK:-$HOME/Code/VST_SDK}"
BUILDROOT="${BUILDROOT:-$HOME/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot}"

echo "GRID — Configuring for SSP cross-compile"
echo ""
echo "  JUCE_DIR:  $JUCE_DIR"
echo "  VSTSDK:    $VSTSDK"
echo "  BUILDROOT: $BUILDROOT"
echo ""

# Verify paths
for dir in "$JUCE_DIR" "$VSTSDK" "$BUILDROOT"; do
    if [ ! -d "$dir" ]; then
        echo "ERROR: $dir not found"
        echo "Set environment variables or edit this script."
        exit 1
    fi
done

# Init submodules
git submodule update --init --recursive

# Run cmake
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR="$JUCE_DIR" \
      -DVSTSDK="$VSTSDK" \
      -DBUILDROOT="$BUILDROOT" \
      .

# Fake the vst3 helper
echo '#!/bin/bash
exit 0' > GRID_vst3_helper
chmod +x GRID_vst3_helper

echo ""
echo "CONFIGURED. Now run: ./build.sh"
