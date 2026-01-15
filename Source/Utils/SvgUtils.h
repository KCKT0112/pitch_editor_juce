#pragma once

#include "../JuceHeader.h"

namespace SvgUtils
{
    // Load SVG from binary data and optionally tint it to a specific color
    std::unique_ptr<juce::Drawable> loadSvg(const char* data, int size);
    std::unique_ptr<juce::Drawable> loadSvg(const char* data, int size, juce::Colour tintColour);

    // Create drawable from SVG string
    std::unique_ptr<juce::Drawable> createDrawableFromSvg(const juce::String& svgString);
    std::unique_ptr<juce::Drawable> createDrawableFromSvg(const juce::String& svgString, juce::Colour tintColour);

    // Tint an existing drawable to a specific color
    void tintDrawable(juce::Drawable* drawable, juce::Colour colour);
}
