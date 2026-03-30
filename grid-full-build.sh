cd ~/Code/ssp_projects/ssp-elastic-audio
rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake Makefile juce GRID_artefacts
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=cmake/xcSSP.cmake \
      -DJUCE_DIR=$HOME/Code/JUCE/ \
      -DVSTSDK=$HOME/Code/VST_SDK/ \
      -DBUILDROOT=$HOME/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot \
      .
grid-build
