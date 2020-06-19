#!/bin/bash -ex

. .travis/common/pre-upload.sh

REV_NAME="threeSD-macos-${GITDATE}-${GITREV}"
ARCHIVE_NAME="${REV_NAME}.tar.gz"
COMPRESSION_FLAGS="-czvf"

mkdir "$REV_NAME"

cp -r build/bin/threeSD.app "$REV_NAME"

# move libs into folder for deployment
macpack "${REV_NAME}/threeSD.app/Contents/MacOS/threeSD" -d "../Frameworks"
# move qt frameworks into app bundle for deployment
$(brew --prefix)/opt/qt5/bin/macdeployqt "${REV_NAME}/threeSD.app" -executable="${REV_NAME}/threeSD.app/Contents/MacOS/threeSD"

# Make the launching script executable
chmod +x ${REV_NAME}/threeSD.app/Contents/MacOS/threeSD

# Verify loader instructions
find "$REV_NAME" -exec otool -L {} \;

. .travis/common/post-upload.sh
