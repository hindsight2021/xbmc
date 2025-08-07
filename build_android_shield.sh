#!/bin/bash
# Build script for Nvidia Shield tuned Kodi on Android
# This script configures a minimal Kodi build with disabled features and link-time optimisation (LTO).
# It is provided as a reference for developers who wish to compile this fork for Android.

set -e

# Create build directory
mkdir -p build && cd build

# Configure CMake with minimal features
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_AIRTUNES=OFF \
  -DENABLE_UPNP=OFF \
  -DENABLE_PVR=OFF \
  -DENABLE_TESTING=OFF \
  -DENABLE_DVDAUDIO=OFF \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
  -DCMAKE_C_FLAGS="${CMAKE_C_FLAGS} -Oz" \
  -DCMAKE_CXX_FLAGS="${CMAKE_CXX_FLAGS} -Oz" \
  -DCORE_PLATFORM_NAME=android \
  -GNinja

# Build Kodi
ninja
