#!/bin/bash -ex

# override CI ccache size
mkdir -p "$HOME/.ccache/"
echo 'max_size = 3.0G' > "$HOME/.ccache/ccache.conf"

mkdir build && cd build
cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE="$(pwd)/../CMakeModules/MinGWCross.cmake" -DUSE_CCACHE=ON -DCMAKE_BUILD_TYPE=Release -DCOMPILE_WITH_DWARF=OFF
ninja

ccache -s

echo 'Prepare binaries...'
cd ..
mkdir package

QT_PLATFORM_DLL_PATH='/usr/x86_64-w64-mingw32/lib/qt5/plugins/platforms/'
find build/ -name "threeSD.exe" -exec cp {} 'package' \;

# copy Qt plugins
mkdir package/platforms
cp "${QT_PLATFORM_DLL_PATH}/qwindows.dll" package/platforms/
cp -rv "${QT_PLATFORM_DLL_PATH}/../imageformats/" package/

python3 .ci/linux-mingw/scan_dll.py package/*.exe package/imageformats/*.dll "package/"
