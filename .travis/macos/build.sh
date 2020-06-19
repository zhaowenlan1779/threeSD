#!/bin/bash -ex

set -o pipefail

export MACOSX_DEPLOYMENT_TARGET=10.13
export Qt5_DIR=$(brew --prefix)/opt/qt5
export PATH="/usr/local/opt/ccache/libexec:$PATH"

mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
