#!/bin/bash

echo "========================================"
echo "Pitch Editor (JUCE) Build Script"
echo "========================================"
echo

cd "$(dirname "$0")"

# Check if JUCE exists
if [ ! -d "JUCE" ]; then
    echo "JUCE not found, cloning..."
    git clone --depth 1 https://github.com/juce-framework/JUCE.git
    if [ $? -ne 0 ]; then
        echo "Failed to clone JUCE"
        exit 1
    fi
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
echo
echo "Configuring with CMake..."
cmake ..
if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

# Build
echo
echo "Building..."
cmake --build . --config Release -j$(nproc)
if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo
echo "========================================"
echo "Build successful!"
echo "========================================"
