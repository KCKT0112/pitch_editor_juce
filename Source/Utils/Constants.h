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
constexpr int MIN_MIDI_NOTE = 24;   // C1
constexpr int MAX_MIDI_NOTE = 96;   // C7
constexpr int MIDI_A4 = 69;
constexpr float FREQ_A4 = 440.0f;

// UI constants
constexpr float DEFAULT_PIXELS_PER_SECOND = 100.0f;
constexpr float DEFAULT_PIXELS_PER_SEMITONE = 20.0f;
constexpr float MIN_PIXELS_PER_SECOND = 20.0f;
constexpr float MAX_PIXELS_PER_SECOND = 500.0f;
constexpr float MIN_PIXELS_PER_SEMITONE = 8.0f;
constexpr float MAX_PIXELS_PER_SEMITONE = 60.0f;

// Colors (ARGB format)
constexpr juce::uint32 COLOR_BACKGROUND = 0xFF1E1E28;
constexpr juce::uint32 COLOR_GRID = 0xFF2D2D37;
constexpr juce::uint32 COLOR_GRID_BAR = 0xFF3D3D47;
constexpr juce::uint32 COLOR_PITCH_CURVE = 0xFFFFD700;
constexpr juce::uint32 COLOR_NOTE_NORMAL = 0xFF9B59B6;
constexpr juce::uint32 COLOR_NOTE_SELECTED = 0xFFE74C3C;
constexpr juce::uint32 COLOR_NOTE_HOVER = 0xFFBB8FCE;
constexpr juce::uint32 COLOR_PRIMARY = 0xFF3498DB;
constexpr juce::uint32 COLOR_WAVEFORM = 0xFF2ECC71;

// Note names
const juce::StringArray NOTE_NAMES = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// Note colors by pitch class
inline juce::Colour getNoteColor(int midiNote)
{
    static const juce::Colour colors[] = {
        juce::Colour(0xFF9B59B6),  // C - Purple
        juce::Colour(0xFF8E44AD),  // C#
        juce::Colour(0xFF3498DB),  // D - Blue
        juce::Colour(0xFF2980B9),  // D#
        juce::Colour(0xFF1ABC9C),  // E - Teal
        juce::Colour(0xFF2ECC71),  // F - Green
        juce::Colour(0xFF27AE60),  // F#
        juce::Colour(0xFFF1C40F),  // G - Yellow
        juce::Colour(0xFFF39C12),  // G#
        juce::Colour(0xFFE67E22),  // A - Orange
        juce::Colour(0xFFD35400),  // A#
        juce::Colour(0xFFE74C3C),  // B - Red
    };
    return colors[midiNote % 12];
}

// Utility functions
inline float midiToFreq(float midi)
{
    return FREQ_A4 * std::pow(2.0f, (midi - MIDI_A4) / 12.0f);
}

inline float freqToMidi(float freq)
{
    if (freq <= 0.0f) return 0.0f;
    return 12.0f * std::log2(freq / FREQ_A4) + MIDI_A4;
}

inline int secondsToFrames(float seconds)
{
    return static_cast<int>(seconds * SAMPLE_RATE / HOP_SIZE);
}

inline float framesToSeconds(int frames)
{
    return static_cast<float>(frames) * HOP_SIZE / SAMPLE_RATE;
}
