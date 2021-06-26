#!/bin/bash -ex

. .ci/common/pre-upload.sh

REV_NAME="threeSD-linux-${GITNAME}"

mkdir "$REV_NAME"
cp build/bin/threeSD "$REV_NAME"

mkdir "$REV_NAME/dist"

. .ci/common/post-upload.sh
