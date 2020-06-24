#!/bin/bash -ex

# Copy documentation
cp license.txt "$REV_NAME"
cp README.md "$REV_NAME"

cp dist/threeSDumper.gm9 "$REV_NAME/dist"

tar $COMPRESSION_FLAGS "$ARCHIVE_NAME" "$REV_NAME"
7z a "$REV_NAME.7z" $REV_NAME

# move the compiled archive into the artifacts directory to be uploaded by travis releases
mv "$ARCHIVE_NAME" artifacts/
mv "$REV_NAME.7z" artifacts/
