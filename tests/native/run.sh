#!/bin/sh
# Configures, builds and runs host-side native C++ unit tests.
# Usage (WSL): sh tests/native/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
build="$here/../../build/native-tests"
cmake -S "$here" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build"
ctest --test-dir "$build" --output-on-failure
