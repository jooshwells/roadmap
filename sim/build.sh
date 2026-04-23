#!/bin/bash

# These commands build and install the sim project.

set -e

if [[ $1 == "-x" ]]; then
    rm -rf build
fi

cmake -S . -B build -DCMAKE_INSTALL_PREFIX="INSTALL"
cmake --build build --config Release -j$(nproc)
cmake --install build --config Release
