#!/bin/bash -ex

. .ci/common/pre-upload.sh

REV_NAME="threeSD-windows-mingw-${GITNAME}"

mkdir "$REV_NAME"
# get around the permission issues
cp -r package/* "$REV_NAME"

mkdir "$REV_NAME/dist"

. .ci/common/post-upload.sh
