#!/bin/bash -ex

. .travis/common/pre-upload.sh

# Find out what release we are building
if [ -z $TRAVIS_TAG ]; then
    REV_NAME="threeSD-linux-${GITDATE}-${GITREV}"
else
    REV_NAME="threeSD-linux-${TRAVIS_TAG}"
fi

ARCHIVE_NAME="${REV_NAME}.tar.xz"
COMPRESSION_FLAGS="-cJvf"

mkdir "$REV_NAME"

cp build/bin/threeSD "$REV_NAME"

mkdir "$REV_NAME/dist"

. .travis/common/post-upload.sh
