#pragma once

#include "../JuceHeader.h"
#include <vector>

/**
 * Represents a single note/pitch segment.
 *
 * Pitch model:
 * - midiNote: The base pitch of the note (can be changed by dragging)
 * - deltaPitch: Per-frame deviation from base pitch (preserved during drag)
 * - f0Values: Original F0 values from detection (for reference)
 *
 * When dragging a note up/down:
 * - midiNote changes
 * - deltaPitch stays the same
 * - Actual pitch = midiNote + deltaPitch[frame]
 */
class Note
{
public:
    Note() = default;
    Note(int startFrame, int endFrame, float midiNote);

    // Frame range
    int getStartFrame() const { return startFrame; }
    int getEndFrame() const { return endFrame; }
    void setStartFrame(int frame) { startFrame = frame; }
    void setEndFrame(int frame) { endFrame = frame; }
    int getDurationFrames() const { return endFrame - startFrame; }

    // Pitch
    float getMidiNote() const { return midiNote; }
    void setMidiNote(float note) { midiNote = note; }
    float getPitchOffset() const { return pitchOffset; }
    void setPitchOffset(float offset) { pitchOffset = offset; }
    float getAdjustedMidiNote() const { return midiNote + pitchOffset; }

    // Delta pitch (per-frame deviation from base pitch in semitones)
    const std::vector<float>& getDeltaPitch() const { return deltaPitch; }
    void setDeltaPitch(std::vector<float> delta) { deltaPitch = std::move(delta); }
    bool hasDeltaPitch() const { return !deltaPitch.empty(); }

    // Vibrato
    bool isVibratoEnabled() const { return vibratoEnabled; }
    void setVibratoEnabled(bool enabled) { vibratoEnabled = enabled; }
    float getVibratoRateHz() const { return vibratoRateHz; }
    void setVibratoRateHz(float hz) { vibratoRateHz = hz; }
    float getVibratoDepthSemitones() const { return vibratoDepthSemitones; }
    void setVibratoDepthSemitones(float semitones) { vibratoDepthSemitones = semitones; }
    float getVibratoPhaseRadians() const { return vibratoPhaseRadians; }
    void setVibratoPhaseRadians(float radians) { vibratoPhaseRadians = radians; }

    // F0 values (original detected values)
    const std::vector<float>& getF0Values() const { return f0Values; }
    void setF0Values(std::vector<float> values) { f0Values = std::move(values); }
    std::vector<float> getAdjustedF0() const;

    // Get F0 values based on current midiNote + deltaPitch
    std::vector<float> computeF0FromDelta() const;

    // Selection
    bool isSelected() const { return selected; }
    void setSelected(bool sel) { selected = sel; }

    // Dirty flag (for incremental synthesis)
    bool isDirty() const { return dirty; }
    void setDirty(bool d) { dirty = d; }
    void markDirty() { dirty = true; }
    void clearDirty() { dirty = false; }

    // Check if frame is within note
    bool containsFrame(int frame) const;

private:
    int startFrame = 0;
    int endFrame = 0;
    float midiNote = 60.0f;
    float pitchOffset = 0.0f;

    std::vector<float> deltaPitch;  // Per-frame deviation from midiNote in semitones

    bool vibratoEnabled = false;
    float vibratoRateHz = 5.0f;
    float vibratoDepthSemitones = 0.0f;
    float vibratoPhaseRadians = 0.0f;

    std::vector<float> f0Values;
    bool selected = false;
    bool dirty = false;  // For incremental synthesis
};
