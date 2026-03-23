# xcSSP.cmake — Cross-compilation toolchain for Percussa SSP (ARM Cortex A17)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv7l)

# ── Buildroot SDK location ────────────────────────────────────────────────
if(NOT DEFINED BUILDROOT)
    if(DEFINED ENV{BUILDROOT})
        set(BUILDROOT $ENV{BUILDROOT})
    else()
        set(BUILDROOT "$ENV{HOME}/Code/buildroot/arm-rockchip-linux-gnueabihf_sdk-buildroot")
    endif()
endif()

# Block pkg-config from finding macOS host libraries.
# We manually provide buildroot freetype/fontconfig paths in CMakeLists.txt.
set(ENV{PKG_CONFIG_LIBDIR} "/nonexistent")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
set(ENV{PKG_CONFIG_PATH} "")

message(STATUS "SSP Buildroot: ${BUILDROOT}")

# ── Key paths inside the buildroot ────────────────────────────────────────
set(GCC_TRIPLE "arm-rockchip-linux-gnueabihf")
set(GCC_VERSION "8.4.0")
set(GCC_LIB_DIR "${BUILDROOT}/lib/gcc/${GCC_TRIPLE}/${GCC_VERSION}")
set(CXX_INC_BASE "${BUILDROOT}/${GCC_TRIPLE}/include/c++/${GCC_VERSION}")

# THE KEY FIX: glibc headers (features.h, stdint.h, etc.) live in a
# nested sysroot, NOT at the top-level buildroot.
set(REAL_SYSROOT "${BUILDROOT}/${GCC_TRIPLE}/sysroot")

# ── Sysroot (points to nested sysroot with glibc) ────────────────────────
set(CMAKE_SYSROOT "${REAL_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH "${REAL_SYSROOT}" "${BUILDROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Skip link test during configure
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ── Compiler Setup ────────────────────────────────────────────────────────
if(EXISTS "/usr/bin/clang")
    set(CMAKE_C_COMPILER   "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")

    set(CMAKE_C_COMPILER_TARGET   "arm-linux-gnueabihf")
    set(CMAKE_CXX_COMPILER_TARGET "arm-linux-gnueabihf")

    # C++ standard library headers from the buildroot's GCC 8.4.0
    # Also include freetype2 headers so JUCE modules can find ft2build.h
    set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
        "${CXX_INC_BASE}"
        "${CXX_INC_BASE}/${GCC_TRIPLE}"
        "${CXX_INC_BASE}/backward"
        "${REAL_SYSROOT}/usr/include/freetype2"
    )
    set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES
        "${REAL_SYSROOT}/usr/include/freetype2"
    )
    message(STATUS "C++ headers: ${CXX_INC_BASE}")
    message(STATUS "Sysroot (glibc): ${REAL_SYSROOT}")

    # Compiler flags — freetype2 include is handled via STANDARD_INCLUDE_DIRECTORIES above
    set(COMMON_FLAGS "--gcc-toolchain=${BUILDROOT} -mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm -B${GCC_LIB_DIR}")

    set(CMAKE_C_FLAGS   "${COMMON_FLAGS}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS "${COMMON_FLAGS}" CACHE STRING "" FORCE)

    # Linker
    find_program(ARM_LINKER "arm-linux-gnueabihf-ld" HINTS /opt/homebrew/bin)
    find_program(ARM_AR     "arm-linux-gnueabihf-ar" HINTS /opt/homebrew/bin)
    find_program(ARM_RANLIB "arm-linux-gnueabihf-ranlib" HINTS /opt/homebrew/bin)

    if(ARM_AR)
        set(CMAKE_AR "${ARM_AR}" CACHE FILEPATH "" FORCE)
    endif()
    if(ARM_RANLIB)
        set(CMAKE_RANLIB "${ARM_RANLIB}" CACHE FILEPATH "" FORCE)
    endif()

    set(LINK_FLAGS "--gcc-toolchain=${BUILDROOT}")
    if(ARM_LINKER)
        set(LINK_FLAGS "${LINK_FLAGS} -fuse-ld=${ARM_LINKER}")
        message(STATUS "ARM linker: ${ARM_LINKER}")
    endif()
    # Library search paths: nested sysroot + buildroot-level
    set(LINK_FLAGS "${LINK_FLAGS} -L${REAL_SYSROOT}/usr/lib -L${REAL_SYSROOT}/lib -L${GCC_LIB_DIR}")

    set(CMAKE_EXE_LINKER_FLAGS    "${LINK_FLAGS}" CACHE STRING "" FORCE)
    set(CMAKE_SHARED_LINKER_FLAGS "${LINK_FLAGS}" CACHE STRING "" FORCE)
    set(CMAKE_MODULE_LINKER_FLAGS "${LINK_FLAGS}" CACHE STRING "" FORCE)

    message(STATUS "GCC lib dir: ${GCC_LIB_DIR}")
    message(STATUS "Using clang cross-compilation (target: arm-linux-gnueabihf)")

else()
    # Linux: buildroot's own GCC
    set(CMAKE_C_COMPILER   "${BUILDROOT}/bin/arm-linux-gcc")
    set(CMAKE_CXX_COMPILER "${BUILDROOT}/bin/arm-linux-g++")

    set(CMAKE_C_FLAGS   "-mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm" CACHE STRING "" FORCE)
    set(CMAKE_CXX_FLAGS "-mcpu=cortex-a17 -mfpu=neon-vfpv4 -mfloat-abi=hard -marm" CACHE STRING "" FORCE)

    message(STATUS "Using buildroot GCC")
endif()

# ── Build type flags ──────────────────────────────────────────────────────
set(CMAKE_C_FLAGS_RELEASE   "-O2 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
