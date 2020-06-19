#!/bin/bash -ex

. .travis/common/pre-upload.sh

REV_NAME="threeSD-linux-${GITDATE}-${GITREV}"
ARCHIVE_NAME="${REV_NAME}.tar.xz"
COMPRESSION_FLAGS="-cJvf"

mkdir "$REV_NAME"

cp build/bin/threeSD "$REV_NAME"

mkdir "$REV_NAME/dist"

. .travis/common/post-upload.sh
