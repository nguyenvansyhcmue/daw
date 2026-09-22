#include "PerformanceRoomIconLibrary.h"

#include <array>
#include <memory>
#include <string_view>

#include "StudioForgeIconSvg.h"

namespace
{
using IconArray = std::array<std::unique_ptr<juce::Drawable>, PerformanceRoomModel::memberCount>;

std::unique_ptr<juce::Drawable> createDrawable(std::string_view svg)
{
    auto xml = juce::parseXML(juce::String::fromUTF8(svg.data(), static_cast<int>(svg.size())));
    return xml != nullptr ? juce::Drawable::createFromSVG(*xml) : nullptr;
}

IconArray createIcons()
{
    return {
        createDrawable(StudioForgeIconSvg::microphone),
        createDrawable(StudioForgeIconSvg::guitar),
        createDrawable(StudioForgeIconSvg::guitar),
        createDrawable(StudioForgeIconSvg::keyboard),
        createDrawable(StudioForgeIconSvg::drum),
        createDrawable(StudioForgeIconSvg::beat)
    };
}

juce::Colour colourForRole(PerformanceRole role)
{
    switch (role)
    {
        case PerformanceRole::vocal:    return juce::Colour(0xff3c718f);
        case PerformanceRole::guitar:   return juce::Colour(0xffb75c68);
        case PerformanceRole::bass:     return juce::Colour(0xff7864a7);
        case PerformanceRole::keyboard: return juce::Colour(0xff5c7890);
        case PerformanceRole::drums:    return juce::Colour(0xffb87567);
        case PerformanceRole::beat:     return juce::Colour(0xff4d8897);
    }

    return juce::Colours::black;
}
}

void PerformanceRoomIconLibrary::draw(juce::Graphics& graphics,
                                      juce::Rectangle<float> bounds,
                                      PerformanceRole role)
{
    static auto icons = createIcons();
    auto* icon = icons[static_cast<size_t>(role)].get();
    if (icon == nullptr)
        return;

    icon->replaceColour(juce::Colours::black, colourForRole(role));
    icon->drawWithin(graphics, bounds, juce::RectanglePlacement::centred, 1.0f);
}
