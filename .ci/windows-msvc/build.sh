#!/bin/sh -ex

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -G Ninja -DCMAKE_TOOLCHAIN_FILE="$(pwd)/../CMakeModules/MSVCCache.cmake" -DUSE_CCACHE=ON -DWARNINGS_AS_ERRORS=OFF -DUSE_BUNDLED_QT=1

ninja
# show the caching efficiency
buildcache -s
