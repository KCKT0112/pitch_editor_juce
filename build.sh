#!/bin/bash

echo "========================================"
echo "Pitch Editor (JUCE) Build Script"
echo "========================================"
echo

cd "$(dirname "$0")"

# Initialize git submodules (JUCE and ARA_SDK)
if [ ! -f "third_party/JUCE/CMakeLists.txt" ]; then
    echo "Initializing git submodules..."
    git submodule update --init --recursive
    if [ $? -ne 0 ]; then
        echo "Failed to initialize submodules"
        exit 1
    fi
fi

# Detect CPU count (cross-platform)
if [ "$(uname)" = "Darwin" ]; then
    NPROC=$(sysctl -n hw.ncpu)
else
    NPROC=$(nproc)
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

# Build all targets
echo
echo "Building..."
cmake --build . --config Release -j${NPROC}
if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

echo
echo "========================================"
echo "Build successful!"
echo "========================================"
echo "Outputs:"
echo "  Standalone: build/PitchEditor_artefacts/Release/"
echo "  VST3:       build/PitchEditorPlugin_artefacts/Release/VST3/"
echo "  AU:         build/PitchEditorPlugin_artefacts/Release/AU/"
echo "========================================"
