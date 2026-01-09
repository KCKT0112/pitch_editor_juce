#include "Project.h"
#include "../Utils/Constants.h"
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float twoPi = 6.2831853071795864769f;
}

Project::Project()
{
}

bool Project::saveToFile(const juce::File& file) const
{
    auto xml = toXml();
    if (!xml)
        return false;
    return xml->writeTo(file);
}

std::unique_ptr<juce::XmlElement> Project::toXml() const
{
    auto root = std::make_unique<juce::XmlElement>("PitchEditorProject");
    root->setAttribute("version", 1);
    root->setAttribute("name", name);
    root->setAttribute("audioPath", filePath.getFullPathName());
    root->setAttribute("sampleRate", audioData.sampleRate);
    root->setAttribute("globalPitchOffset", globalPitchOffset);
    root->setAttribute("formantShift", formantShift);
    root->setAttribute("volume", volume);

    // Notes
    auto* notesElem = root->createNewChildElement("Notes");
    for (const auto& note : notes)
    {
        auto* n = notesElem->createNewChildElement("Note");
        n->setAttribute("startFrame", note.getStartFrame());
        n->setAttribute("endFrame", note.getEndFrame());
        n->setAttribute("midiNote", note.getMidiNote());
        n->setAttribute("pitchOffset", note.getPitchOffset());

        n->setAttribute("vibratoEnabled", note.isVibratoEnabled() ? 1 : 0);
        n->setAttribute("vibratoRateHz", note.getVibratoRateHz());
        n->setAttribute("vibratoDepthSemitones", note.getVibratoDepthSemitones());
        n->setAttribute("vibratoPhaseRadians", note.getVibratoPhaseRadians());
    }

    // F0
    auto* f0Elem = root->createNewChildElement("F0");
    {
        juce::StringArray parts;
        parts.ensureStorageAllocated(static_cast<int>(audioData.f0.size()));
        for (float v : audioData.f0)
            parts.add(juce::String(v, 6));
        f0Elem->addTextElement(parts.joinIntoString(" "));
    }

    // VoicedMask
    auto* voicedElem = root->createNewChildElement("VoicedMask");
    {
        juce::String mask;
        mask.preallocateBytes(static_cast<size_t>(audioData.voicedMask.size()));
        for (bool b : audioData.voicedMask)
            mask << (b ? '1' : '0');
        voicedElem->addTextElement(mask);
    }

    return root;
}

bool Project::fromXml(const juce::XmlElement& xml)
{
    if (xml.getTagName() != "PitchEditorProject")
        return false;

    name = xml.getStringAttribute("name", "Untitled");
    filePath = juce::File(xml.getStringAttribute("audioPath"));
    audioData.sampleRate = xml.getIntAttribute("sampleRate", 44100);
    globalPitchOffset = static_cast<float>(xml.getDoubleAttribute("globalPitchOffset", 0.0));
    formantShift = static_cast<float>(xml.getDoubleAttribute("formantShift", 0.0));
    volume = static_cast<float>(xml.getDoubleAttribute("volume", 0.0));

    // Notes
    notes.clear();
    if (auto* notesElem = xml.getChildByName("Notes"))
    {
        for (auto* n = notesElem->getFirstChildElement(); n != nullptr; n = n->getNextElement())
        {
            if (n->getTagName() == "Note")
            {
                Note note;
                note.setStartFrame(n->getIntAttribute("startFrame", 0));
                note.setEndFrame(n->getIntAttribute("endFrame", 0));
                note.setMidiNote(static_cast<float>(n->getDoubleAttribute("midiNote", 60.0)));
                note.setPitchOffset(static_cast<float>(n->getDoubleAttribute("pitchOffset", 0.0)));
                note.setVibratoEnabled(n->getIntAttribute("vibratoEnabled", 0) != 0);
                note.setVibratoRateHz(static_cast<float>(n->getDoubleAttribute("vibratoRateHz", 5.0)));
                note.setVibratoDepthSemitones(static_cast<float>(n->getDoubleAttribute("vibratoDepthSemitones", 0.0)));
                note.setVibratoPhaseRadians(static_cast<float>(n->getDoubleAttribute("vibratoPhaseRadians", 0.0)));
                notes.push_back(std::move(note));
            }
        }
    }

    // F0
    audioData.f0.clear();
    if (auto* f0Elem = xml.getChildByName("F0"))
    {
        juce::String f0Text = f0Elem->getAllSubText();
        juce::StringArray parts;
        parts.addTokens(f0Text, " ", "");
        audioData.f0.reserve(static_cast<size_t>(parts.size()));
        for (const auto& p : parts)
        {
            if (p.isNotEmpty())
                audioData.f0.push_back(p.getFloatValue());
        }
    }

    // VoicedMask
    audioData.voicedMask.clear();
    if (auto* voicedElem = xml.getChildByName("VoicedMask"))
    {
        juce::String mask = voicedElem->getAllSubText();
        audioData.voicedMask.reserve(static_cast<size_t>(mask.length()));
        for (int i = 0; i < mask.length(); ++i)
            audioData.voicedMask.push_back(mask[i] == '1');
    }

    modified = false;
    return true;
}

Note* Project::getNoteAtFrame(int frame)
{
    for (auto& note : notes)
    {
        if (note.containsFrame(frame))
            return &note;
    }
    return nullptr;
}

std::vector<Note*> Project::getNotesInRange(int startFrame, int endFrame)
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.getStartFrame() < endFrame && note.getEndFrame() > startFrame)
            result.push_back(&note);
    }
    return result;
}

std::vector<Note*> Project::getSelectedNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isSelected())
            result.push_back(&note);
    }
    return result;
}

void Project::deselectAllNotes()
{
    for (auto& note : notes)
        note.setSelected(false);
}

std::vector<Note*> Project::getDirtyNotes()
{
    std::vector<Note*> result;
    for (auto& note : notes)
    {
        if (note.isDirty())
            result.push_back(&note);
    }
    return result;
}

void Project::clearAllDirty()
{
    for (auto& note : notes)
        note.clearDirty();
    // Also clear F0 dirty range
    f0DirtyStart = -1;
    f0DirtyEnd = -1;
}

bool Project::hasDirtyNotes() const
{
    for (const auto& note : notes)
    {
        if (note.isDirty())
            return true;
    }
    return false;
}

void Project::setF0DirtyRange(int startFrame, int endFrame)
{
    if (f0DirtyStart < 0 || startFrame < f0DirtyStart)
        f0DirtyStart = startFrame;
    if (f0DirtyEnd < 0 || endFrame > f0DirtyEnd)
        f0DirtyEnd = endFrame;
}

void Project::clearF0DirtyRange()
{
    f0DirtyStart = -1;
    f0DirtyEnd = -1;
}

bool Project::hasF0DirtyRange() const
{
    return f0DirtyStart >= 0 && f0DirtyEnd >= 0;
}

std::pair<int, int> Project::getF0DirtyRange() const
{
    return {f0DirtyStart, f0DirtyEnd};
}

std::pair<int, int> Project::getDirtyFrameRange() const
{
    int minStart = -1;
    int maxEnd = -1;
    
    // Check dirty notes
    for (const auto& note : notes)
    {
        if (note.isDirty())
        {
            if (minStart < 0 || note.getStartFrame() < minStart)
                minStart = note.getStartFrame();
            if (maxEnd < 0 || note.getEndFrame() > maxEnd)
                maxEnd = note.getEndFrame();
        }
    }
    
    // Also include F0 dirty range from Draw mode edits
    if (f0DirtyStart >= 0)
    {
        if (minStart < 0 || f0DirtyStart < minStart)
            minStart = f0DirtyStart;
    }
    if (f0DirtyEnd >= 0)
    {
        if (maxEnd < 0 || f0DirtyEnd > maxEnd)
            maxEnd = f0DirtyEnd;
    }
    
    return {minStart, maxEnd};
}

std::vector<float> Project::getAdjustedF0() const
{
    if (audioData.f0.empty())
        return {};
    
    // Start with copy of original F0
    std::vector<float> adjustedF0 = audioData.f0;
    
    // Apply global pitch offset
    if (globalPitchOffset != 0.0f)
    {
        float globalRatio = std::pow(2.0f, globalPitchOffset / 12.0f);
        for (auto& f : adjustedF0)
        {
            if (f > 0.0f)
                f *= globalRatio;
        }
    }
    
    // Calculate per-frame ratios from notes
    std::vector<float> frameRatios(adjustedF0.size(), 1.0f);
    
    for (const auto& note : notes)
    {
        const bool hasPitchOffset = std::abs(note.getPitchOffset()) > 0.0001f;
        const bool hasVibrato = note.isVibratoEnabled() && note.getVibratoDepthSemitones() > 0.0001f && note.getVibratoRateHz() > 0.0001f;

        if (hasPitchOffset || hasVibrato)
        {
            int start = note.getStartFrame();
            int end = std::min(note.getEndFrame(), static_cast<int>(adjustedF0.size()));

            for (int i = start; i < end; ++i)
            {
                float ratio = 1.0f;
                if (hasPitchOffset)
                    ratio *= std::pow(2.0f, note.getPitchOffset() / 12.0f);

                if (hasVibrato)
                {
                    const float t = framesToSeconds(i - start);
                    const float vib = note.getVibratoDepthSemitones() * std::sin(twoPi * note.getVibratoRateHz() * t + note.getVibratoPhaseRadians());
                    ratio *= std::pow(2.0f, vib / 12.0f);
                }

                frameRatios[i] = ratio;
            }
        }
    }
    
    // Apply smoothing at transitions
    const int smoothFrames = 5;
    
    // Find transition points
    for (size_t i = 1; i < frameRatios.size(); ++i)
    {
        if (std::abs(frameRatios[i] - frameRatios[i-1]) > 0.001f)
        {
            // Smooth transition
            int startIdx = std::max(0, static_cast<int>(i) - smoothFrames / 2);
            int endIdx = std::min(static_cast<int>(frameRatios.size()), 
                                  static_cast<int>(i) + smoothFrames / 2 + 2);
            
            if (endIdx - startIdx > 1)
            {
                float valBefore = frameRatios[startIdx];
                float valAfter = frameRatios[endIdx - 1];
                
                for (int j = startIdx; j < endIdx; ++j)
                {
                    float t = static_cast<float>(j - startIdx) / (endIdx - startIdx - 1);
                    frameRatios[j] = valBefore + t * (valAfter - valBefore);
                }
            }
        }
    }
    
    // Apply ratios only to voiced regions
    for (size_t i = 0; i < adjustedF0.size(); ++i)
    {
        if (i < audioData.voicedMask.size() && audioData.voicedMask[i])
        {
            adjustedF0[i] *= frameRatios[i];
        }
    }
    
    return adjustedF0;
}

std::vector<float> Project::getAdjustedF0ForRange(int startFrame, int endFrame) const
{
    if (audioData.f0.empty())
        return {};
    
    // Clamp range
    startFrame = std::max(0, startFrame);
    endFrame = std::min(endFrame, static_cast<int>(audioData.f0.size()));
    
    if (startFrame >= endFrame)
        return {};
    
    int rangeSize = endFrame - startFrame;
    
    // Get slice of original F0
    std::vector<float> adjustedF0(audioData.f0.begin() + startFrame, 
                                   audioData.f0.begin() + endFrame);
    
    // Apply global pitch offset
    if (globalPitchOffset != 0.0f)
    {
        float globalRatio = std::pow(2.0f, globalPitchOffset / 12.0f);
        for (auto& f : adjustedF0)
        {
            if (f > 0.0f)
                f *= globalRatio;
        }
    }
    
    // Calculate per-frame ratios from notes (for the range)
    std::vector<float> frameRatios(rangeSize, 1.0f);
    
    for (const auto& note : notes)
    {
        const bool hasPitchOffset = std::abs(note.getPitchOffset()) > 0.0001f;
        const bool hasVibrato = note.isVibratoEnabled() && note.getVibratoDepthSemitones() > 0.0001f && note.getVibratoRateHz() > 0.0001f;

        if (hasPitchOffset || hasVibrato)
        {
            int noteStart = note.getStartFrame();
            int noteEnd = note.getEndFrame();
            
            // Calculate overlap with our range
            int overlapStart = std::max(noteStart, startFrame) - startFrame;
            int overlapEnd = std::min(noteEnd, endFrame) - startFrame;
            
            for (int i = overlapStart; i < overlapEnd; ++i)
            {
                if (i >= 0 && i < rangeSize)
                {
                    const int globalFrame = startFrame + i;
                    float ratio = 1.0f;

                    if (hasPitchOffset)
                        ratio *= std::pow(2.0f, note.getPitchOffset() / 12.0f);

                    if (hasVibrato)
                    {
                        const float t = framesToSeconds(globalFrame - noteStart);
                        const float vib = note.getVibratoDepthSemitones() * std::sin(twoPi * note.getVibratoRateHz() * t + note.getVibratoPhaseRadians());
                        ratio *= std::pow(2.0f, vib / 12.0f);
                    }

                    frameRatios[i] = ratio;
                }
            }
        }
    }
    
    // Apply smoothing at transitions between different pitch offsets
    const int smoothFrames = 20;  // ~50ms at 400fps for smooth transitions

    // Find transition points and smooth them
    for (int i = 1; i < rangeSize; ++i)
    {
        float diff = std::abs(frameRatios[i] - frameRatios[i-1]);
        if (diff > 0.001f)
        {
            // Found a transition point, apply smoothing around it
            int smoothStart = std::max(0, i - smoothFrames);
            int smoothEnd = std::min(rangeSize, i + smoothFrames);

            float valBefore = frameRatios[smoothStart];
            float valAfter = frameRatios[smoothEnd - 1];

            // Use cosine interpolation for smoother transitions
            for (int j = smoothStart; j < smoothEnd; ++j)
            {
                float t = static_cast<float>(j - smoothStart) / (smoothEnd - smoothStart - 1);
                // Cosine interpolation: smoother than linear
                float smoothT = (1.0f - std::cos(t * 3.14159f)) * 0.5f;
                frameRatios[j] = valBefore + smoothT * (valAfter - valBefore);
            }

            // Skip past the smoothed region
            i = smoothEnd - 1;
        }
    }
    
    // Apply ratios only to voiced regions
    for (int i = 0; i < rangeSize; ++i)
    {
        size_t globalIdx = static_cast<size_t>(startFrame + i);
        if (globalIdx < audioData.voicedMask.size() && audioData.voicedMask[globalIdx])
        {
            adjustedF0[i] *= frameRatios[i];
        }
    }
    
    return adjustedF0;
}
