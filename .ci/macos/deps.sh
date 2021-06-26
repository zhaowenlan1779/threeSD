#!/bin/sh -ex

brew update
brew unlink python@2 || true
rm '/usr/local/bin/2to3' || true
brew install qt5 sdl2 p7zip ccache llvm ninja || true
pip3 install macpack
