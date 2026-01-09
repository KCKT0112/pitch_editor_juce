#pragma once

// Audio constants
constexpr int SAMPLE_RATE = 44100;
constexpr int HOP_SIZE = 512;
constexpr int WIN_SIZE = 2048;
constexpr int N_FFT = 2048;
constexpr int NUM_MELS = 128;
constexpr float FMIN = 40.0f;
constexpr float FMAX = 16000.0f;

// MIDI constants
constexpr int MIN_MIDI_NOTE = 24; // C1
constexpr int MAX_MIDI_NOTE = 96; // C7
constexpr int MIDI_A4 = 69;
constexpr float FREQ_A4 = 440.0f;

// UI constants
constexpr float DEFAULT_PIXELS_PER_SECOND = 100.0f;
constexpr float DEFAULT_PIXELS_PER_SEMITONE = 45.0f;
constexpr float MIN_PIXELS_PER_SECOND = 20.0f;
constexpr float MAX_PIXELS_PER_SECOND = 500.0f;
constexpr float MIN_PIXELS_PER_SEMITONE = 8.0f;
constexpr float MAX_PIXELS_PER_SEMITONE = 120.0f;

// Colors (ARGB format) - Modern dark theme
constexpr juce::uint32 COLOR_BACKGROUND = 0xFF2A2A35;  // Dark gray-blue
constexpr juce::uint32 COLOR_GRID = 0xFF3A3A45;        // Subtle grid
constexpr juce::uint32 COLOR_GRID_BAR = 0xFF4A4A55;    // Bar lines
constexpr juce::uint32 COLOR_PITCH_CURVE = 0xFFFFFFFF; // White pitch curve
constexpr juce::uint32 COLOR_NOTE_NORMAL =
    0xFF6B5BFF; // Blue-purple (like reference)
constexpr juce::uint32 COLOR_NOTE_SELECTED =
    0xFF8B7BFF; // Lighter purple when selected
constexpr juce::uint32 COLOR_NOTE_HOVER = 0xFF7B6BFF; // Hover state
constexpr juce::uint32 COLOR_PRIMARY = 0xFF6B5BFF;    // Primary accent
constexpr juce::uint32 COLOR_WAVEFORM =
    0xFF353540; // Background waveform (very subtle)

// Note names - use inline function to avoid global construction issues
inline const juce::StringArray &getNoteNames() {
  static const juce::StringArray names = {"C",  "C#", "D",  "D#", "E",  "F",
                                          "F#", "G",  "G#", "A",  "A#", "B"};
  return names;
}

// Utility functions
inline float midiToFreq(float midi) {
  return FREQ_A4 * std::pow(2.0f, (midi - MIDI_A4) / 12.0f);
}

inline float freqToMidi(float freq) {
  if (freq <= 0.0f)
    return 0.0f;
  return 12.0f * std::log2(freq / FREQ_A4) + MIDI_A4;
}

inline int secondsToFrames(float seconds) {
  return static_cast<int>(seconds * SAMPLE_RATE / HOP_SIZE);
}

inline float framesToSeconds(int frames) {
  return static_cast<float>(frames) * HOP_SIZE / SAMPLE_RATE;
}
