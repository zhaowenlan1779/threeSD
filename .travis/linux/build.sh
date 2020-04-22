#!/bin/bash -ex
mkdir -p "$HOME/.ccache"
docker run -v $(pwd):/threeSD -v "$HOME/.ccache":/root/.ccache citraemu/build-environments:linux-fresh /bin/bash -ex /threeSD/.travis/linux/docker.sh
