#!/bin/bash -ex

# Copy documentation
cp license.txt "$REV_NAME"
cp README.md "$REV_NAME"

cp dist/threeSDumper.gm9 "$REV_NAME/dist"

7z a "$REV_NAME.zip" $REV_NAME

# move the compiled archive into the artifacts directory to be uploaded by gh action releases
mv "$REV_NAME.zip" artifacts/
