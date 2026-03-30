#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════
# deploy.sh — Deploy ELAS plugin to Percussa SSP
#
# Usage:
#   ./deploy.sh              # Deploy via mounted SD card
#   ./deploy.sh 192.168.1.X  # Deploy via network (SSH)
# ═══════════════════════════════════════════════════════════════════════════

set -e

PLUGIN_SO="build/ELAS_artefacts/Release/VST3/elas.vst3/Contents/armv7l-linux/elas.so"
SSP_PLUGIN_DIR="/media/sd/plugins"

# Check if plugin exists
if [ ! -f "$PLUGIN_SO" ]; then
    echo "ERROR: Plugin not found at $PLUGIN_SO"
    echo "Build first: mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/xcSSP.cmake .. && cmake --build . -- -j4"
    exit 1
fi

echo "Plugin: $PLUGIN_SO"
echo "Size: $(du -h "$PLUGIN_SO" | cut -f1)"

if [ -n "$1" ]; then
    # ── Network Deploy ────────────────────────────────────────────────────
    SSP_IP="$1"
    echo "Deploying to SSP at $SSP_IP via SSH..."
    scp "$PLUGIN_SO" "root@${SSP_IP}:${SSP_PLUGIN_DIR}/elas.so"
    echo "Done. Reboot SSP or reload plugins."
else
    # ── SD Card Deploy ────────────────────────────────────────────────────
    # Try common SD card mount points
    SD_PATHS=(
        "/Volumes/SSP"           # macOS
        "/media/$USER/SSP"       # Linux auto-mount
        "/mnt/ssp"               # Manual mount
    )

    SD_FOUND=""
    for path in "${SD_PATHS[@]}"; do
        if [ -d "$path" ]; then
            SD_FOUND="$path"
            break
        fi
    done

    if [ -z "$SD_FOUND" ]; then
        echo "ERROR: SSP SD card not found at any of:"
        for path in "${SD_PATHS[@]}"; do
            echo "  $path"
        done
        echo ""
        echo "Mount the SD card, or use: $0 <ssp-ip-address>"
        exit 1
    fi

    DEST_DIR="$SD_FOUND/plugins"
    mkdir -p "$DEST_DIR"

    echo "Copying to $DEST_DIR/elas.so ..."
    cp "$PLUGIN_SO" "$DEST_DIR/elas.so"
    echo "Done. Eject SD card, insert into SSP, and power on."
fi
