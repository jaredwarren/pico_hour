#!/bin/sh
# Run inside the container: configure and compile.
#
# Uses build-docker/ (not build/) so CMake's absolute paths in CMakeCache.txt never clash with a
# native host build: macOS might cache /Users/you/.../build while this tree is mounted as /project.
set -eu
cd /project
mkdir -p build-docker
cd build-docker
cmake -G "Unix Makefiles" ..
cmake --build . --parallel "$(nproc)"
