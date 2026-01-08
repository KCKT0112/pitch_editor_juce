<div align="center">
  <h1> ♥ 大市修 ♥ <br/> PC-NSF-HifiGAN Pitch Editor </h1>

  <img src="logo.jpg" alt="Logo" width="300" />

  <p>
    <em>大市修于市，音符修于心</em>
  </p>

  <p>
    <small>A lightweight pitch editor inspired by Melodyne, built with the JUCE framework for PC-NSF-HiFiGAN vocoder integration.</small>
  </p>
</div>

## Features

- Piano roll interface for visualizing and editing pitch
- Waveform display
- YIN-based pitch detection
- Note segmentation
- Real-time audio playback
- Pitch offset editing per note
- Import/Export WAV files

## Prerequisites

- CMake 3.22 or later
- C++17 compatible compiler
- JUCE framework (will be cloned as submodule)

### Windows

- Visual Studio 2019 or later with C++ workload
- Windows SDK

### macOS

- Xcode with command line tools

### Linux

- GCC or Clang
- ALSA development libraries

## Building

### 1. Clone JUCE

First, clone the JUCE framework into the project directory:

```bash
cd pitch_editor_juce
git clone https://github.com/juce-framework/JUCE.git
```

### 2. Configure and Build

#### Windows (Visual Studio)

```bash
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

#### macOS / Linux

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### 3. Run

The executable will be in `build/PitchEditor_artefacts/Release/` (or similar path depending on platform).

## Project Structure

```
pitch_editor_juce/
├── CMakeLists.txt              # Build configuration
├── Source/
│   ├── Main.cpp                # Application entry point
│   ├── Audio/
│   │   ├── AudioEngine.h/cpp   # Audio playback engine
│   │   ├── PitchDetector.h/cpp # YIN pitch detection
│   │   └── Vocoder.h/cpp       # Vocoder wrapper (placeholder)
│   ├── Models/
│   │   ├── Note.h/cpp          # Note representation
│   │   └── Project.h/cpp       # Project container
│   ├── UI/
│   │   ├── MainComponent.h/cpp # Main application component
│   │   ├── PianoRollComponent.h/cpp
│   │   ├── WaveformComponent.h/cpp
│   │   ├── ToolbarComponent.h/cpp
│   │   └── ParameterPanel.h/cpp
│   └── Utils/
│       ├── Constants.h         # Audio constants
│       └── MelSpectrogram.h/cpp
└── JUCE/                       # JUCE framework (clone here)
```

## Usage

1. **Open File**: Click "Open" to load a WAV/MP3/FLAC file
2. **View**: Use mouse wheel to scroll, Ctrl+wheel to zoom
3. **Edit**: Click and drag notes to adjust pitch offset
4. **Playback**: Use Play/Pause/Stop buttons
5. **Export**: Click "Export" to save the modified audio

## Keyboard Shortcuts

- `Space`: Play/Pause
- `Ctrl+O`: Open file
- `Ctrl+S`: Export file
- `Ctrl+Mouse Wheel`: Zoom
- `Shift+Mouse Wheel`: Scroll vertically

## License

This project is part of the PC-NSF-HiFiGAN vocoder demo.
