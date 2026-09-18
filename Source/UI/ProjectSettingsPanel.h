#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TrackDataModel;

class ProjectSettingsPanel final : public juce::Component
{
public:
    explicit ProjectSettingsPanel(TrackDataModel& model);

    void resized() override;

private:
    void applyAndClose();
    void close();

    TrackDataModel& trackDataModel;
    juce::Label tempoLabel { {}, "Tempo" };
    juce::Label signatureLabel { {}, "Beats per Bar" };
    juce::Slider tempo;
    juce::ComboBox beatsPerBar;
    juce::TextButton cancel { "Cancel" };
    juce::TextButton apply { "Apply" };
};
