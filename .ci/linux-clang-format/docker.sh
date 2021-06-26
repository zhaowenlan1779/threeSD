#!/bin/bash -ex

if grep -nrI '\s$' src *.yml *.txt *.md Doxyfile .gitignore .gitmodules .ci* dist/*.desktop \
                 dist/*.svg dist/*.xml; then
    echo Trailing whitespace found, aborting
    exit 1
fi

# Default clang-format points to default 3.5 version one
CLANG_FORMAT=clang-format-10
$CLANG_FORMAT --version

# Check everything
files_to_lint="$(find src/ -name '*.cpp' -or -name '*.h')"

# Turn off tracing for this because it's too verbose
set +x

for f in $files_to_lint; do
    d=$(diff -u "$f" <($CLANG_FORMAT "$f") || true)
    if ! [ -z "$d" ]; then
        echo "!!! $f not compliant to coding style, here is the fix:"
        echo "$d"
        fail=1
    fi
done

set -x

if [ "$fail" = 1 ]; then
    exit 1
fi
