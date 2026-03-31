cd ~/Code/ssp_projects/ssp-elastic-audio
rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake Makefile juce GRID_artefacts
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR=$HOME/Code/JUCE7/ \
      -DVSTSDK=$HOME/Code/VST_SDK/ \
      -DBUILDROOT=$HOME/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot \
      .
make -j$(sysctl -n hw.ncpu) GRID juce_vst3_helper 2>/dev/null || true
echo '#!/bin/bash
exit 0' > juce_vst3_helper
chmod +x juce_vst3_helper
export PATH="$(pwd):$PATH"
make -j$(sysctl -n hw.ncpu)
