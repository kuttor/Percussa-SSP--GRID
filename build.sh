#!/bin/bash
# ── GRID build script ────────────────────────────────────────────────────
# Run from project root. Handles the vst3_helper workaround automatically.
set -e

# Ensure fake vst3 helper exists
echo '#!/bin/bash
exit 0' > GRID_vst3_helper
chmod +x GRID_vst3_helper

# Build
CORES=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
make -j$CORES

echo ""
echo "BUILD OK: GRID_artefacts/Release/VST3/GRID.vst3/Contents/armv7l-linux/GRID.so"
echo ""

# Deploy if SD card is mounted
if [ -d "/Volumes/BOOT" ]; then
    mkdir -p /Volumes/BOOT/plugins/GRID
    cp GRID_artefacts/Release/VST3/GRID.vst3/Contents/armv7l-linux/GRID.so /Volumes/BOOT/plugins/GRID/
    echo "DEPLOYED to /Volumes/BOOT/plugins/GRID/"
else
    echo "SD card not mounted — copy GRID.so to /plugins/GRID/ on SSP's SD card"
fi
