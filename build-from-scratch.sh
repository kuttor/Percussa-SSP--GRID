#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════
# build-from-scratch.sh — Complete setup & build for ELAS on Apple Silicon
#
# Prerequisites (you already have these):
#   ✅ ARM cross-compiler (arm-linux-gnueabihf-gcc)
#   ✅ CMake
#
# This script installs the remaining dependencies and builds the plugin.
# Run from anywhere — it will create directories as needed.
# ═══════════════════════════════════════════════════════════════════════════

set -e

echo "═══════════════════════════════════════════════════════════════"
echo " ELAS — Elastic Audio for Percussa SSP"
echo " Build setup for macOS Apple Silicon"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# ── Step 1: Verify prerequisites ──────────────────────────────────────────
echo "▸ Step 1: Checking prerequisites..."

if ! command -v cmake &> /dev/null; then
    echo "  ✗ cmake not found. Install with: brew install cmake"
    exit 1
fi
echo "  ✓ cmake: $(cmake --version | head -1)"

if ! command -v arm-linux-gnueabihf-gcc &> /dev/null; then
    echo "  ✗ ARM cross-compiler not found on PATH."
    echo "    Make sure arm-linux-gnueabihf-gcc is accessible."
    exit 1
fi
echo "  ✓ ARM compiler: $(arm-linux-gnueabihf-gcc --version | head -1)"

if ! command -v git &> /dev/null; then
    echo "  ✗ git not found. Install with: brew install git"
    exit 1
fi
echo "  ✓ git: $(git --version)"
echo ""

# ── Step 2: SSP Buildroot SDK ─────────────────────────────────────────────
echo "▸ Step 2: SSP Buildroot SDK..."

BUILDROOT_DIR="$HOME/buildroot"
BUILDROOT_INNER="$BUILDROOT_DIR/arm-rockchip-linux-gnueabihf_sdk-buildroot"

if [ -d "$BUILDROOT_INNER/libexec" ]; then
    echo "  ✓ Already installed at $BUILDROOT_INNER"
else
    echo "  Downloading SSP Buildroot SDK (~500MB)..."
    mkdir -p "$BUILDROOT_DIR"
    cd "$BUILDROOT_DIR"

    if [ ! -f "arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz" ]; then
        curl -O https://sw13072022.s3.us-west-1.amazonaws.com/arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz
    fi

    echo "  Extracting (this takes a minute)..."
    tar xzf arm-rockchip-linux-gnueabihf_sdk-buildroot.tar.gz

    if [ -d "$BUILDROOT_INNER/libexec" ]; then
        echo "  ✓ Installed successfully"
    else
        echo "  ✗ Extraction failed — check $BUILDROOT_DIR"
        exit 1
    fi
fi
echo ""

# ── Step 3: VST3 SDK ─────────────────────────────────────────────────────
echo "▸ Step 3: Steinberg VST3 SDK..."

SDKS_DIR="$HOME/SDKs"
VSTSDK_DIR="$SDKS_DIR/vst3-sdk"

if [ -f "$VSTSDK_DIR/CMakeLists.txt" ]; then
    echo "  ✓ Already installed at $VSTSDK_DIR"
else
    mkdir -p "$SDKS_DIR"
    cd "$SDKS_DIR"

    echo "  ╔══════════════════════════════════════════════════════════╗"
    echo "  ║  The VST3 SDK requires manual download from Steinberg.  ║"
    echo "  ║                                                          ║"
    echo "  ║  1. Go to: https://www.steinberg.net/developers/         ║"
    echo "  ║  2. Download the VST3 SDK (zip file)                     ║"
    echo "  ║  3. Move it to: ~/SDKs/                                  ║"
    echo "  ║  4. Then run these commands:                             ║"
    echo "  ║                                                          ║"
    echo "  ║     cd ~/SDKs                                            ║"
    echo "  ║     unzip VST_SDK.zip                                    ║"
    echo "  ║     mv VST_SDK/vst3sdk vst3-sdk                         ║"
    echo "  ║                                                          ║"
    echo "  ║  Then re-run this script.                                ║"
    echo "  ╚══════════════════════════════════════════════════════════╝"
    echo ""
    echo "  ✗ VST3 SDK not found. Complete the step above first."
    exit 1
fi
echo ""

# ── Step 4: JUCE ─────────────────────────────────────────────────────────
echo "▸ Step 4: JUCE framework..."

JUCE_DIR="$SDKS_DIR/JUCE"

if [ -f "$JUCE_DIR/CMakeLists.txt" ]; then
    echo "  ✓ Already installed at $JUCE_DIR"
else
    echo "  Cloning JUCE (this may take a moment)..."
    cd "$SDKS_DIR"
    git clone --depth 1 https://github.com/juce-framework/JUCE.git
    echo "  ✓ JUCE cloned"
fi
echo ""

# ── Step 5: Clone / update ELAS project ──────────────────────────────────
echo "▸ Step 5: ELAS project..."

PROJECTS_DIR="$HOME/projects"
ELAS_DIR="$PROJECTS_DIR/ssp-elastic-audio"

mkdir -p "$PROJECTS_DIR"

if [ -d "$ELAS_DIR" ]; then
    echo "  ✓ Project exists at $ELAS_DIR"
else
    echo "  ╔══════════════════════════════════════════════════════════╗"
    echo "  ║  Copy the ssp-elastic-audio project folder to:          ║"
    echo "  ║  ~/projects/ssp-elastic-audio                            ║"
    echo "  ║                                                          ║"
    echo "  ║  Then init the submodules:                               ║"
    echo "  ║     cd ~/projects/ssp-elastic-audio                      ║"
    echo "  ║     git init                                             ║"
    echo "  ║     git submodule add https://github.com/                ║"
    echo "  ║       Signalsmith-Audio/signalsmith-stretch.git          ║"
    echo "  ║       libs/signalsmith-stretch                           ║"
    echo "  ║     git submodule add https://github.com/                ║"
    echo "  ║       percussa/ssp-sdk.git libs/ssp-sdk                  ║"
    echo "  ║     git submodule update --init --recursive              ║"
    echo "  ║                                                          ║"
    echo "  ║  Then re-run this script.                                ║"
    echo "  ╚══════════════════════════════════════════════════════════╝"
    exit 1
fi

# Make sure submodules are present
if [ ! -f "$ELAS_DIR/libs/signalsmith-stretch/signalsmith-stretch.h" ] || \
   grep -q "#error" "$ELAS_DIR/libs/signalsmith-stretch/signalsmith-stretch.h" 2>/dev/null; then
    echo "  Signalsmith Stretch submodule missing or is stub. Initializing..."
    cd "$ELAS_DIR"
    # Remove stub files if present
    rm -rf libs/signalsmith-stretch libs/ssp-sdk
    git submodule add -f https://github.com/Signalsmith-Audio/signalsmith-stretch.git libs/signalsmith-stretch 2>/dev/null || true
    git submodule add -f https://github.com/percussa/ssp-sdk.git libs/ssp-sdk 2>/dev/null || true
    git submodule update --init --recursive
    echo "  ✓ Submodules initialized"
else
    echo "  ✓ Submodules present"
fi
echo ""

# ── Step 6: Build for SSP (ARM cross-compile) ────────────────────────────
echo "▸ Step 6: Building for SSP (ARM cross-compile)..."

cd "$ELAS_DIR"
mkdir -p build
cd build

echo "  Running cmake..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=../cmake/xcSSP.cmake \
      -DJUCE_DIR="$JUCE_DIR" \
      -DVSTSDK="$VSTSDK_DIR" \
      -DBUILDROOT="$BUILDROOT_INNER" \
      ..

echo "  Compiling (using $(sysctl -n hw.ncpu) cores)..."
cmake --build . -- -j$(sysctl -n hw.ncpu)

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo " ✓ BUILD COMPLETE"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo " Plugin: $ELAS_DIR/build/ELAS_artefacts/Release/VST3/elas.vst3/Contents/armv7l-linux/elas.so"
echo ""
echo " To deploy to SSP:"
echo "   1. Copy elas.so to /plugins/ on the SSP's SD card"
echo "   2. Or use: ../deploy.sh <ssp-ip-address>"
echo ""
