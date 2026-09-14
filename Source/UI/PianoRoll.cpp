#include "PianoRoll.h"

MidiClipId PianoRoll::ensureActiveClip()
{
    if (trackModel == nullptr) return {};
    for (const auto& clip : trackModel->getMidiClips()) if (clip.id == activeClip) return activeClip;
    if (! trackModel->getMidiClips().empty()) return activeClip = trackModel->getMidiClips().front().id;
    if (trackModel->getTrackCount() == 0) return {};
    return activeClip = trackModel->addMidiClip(trackModel->getTrackId(0), trackModel->getPlayheadPosition());
}

int PianoRoll::pitchForY(float y) const noexcept { return juce::jlimit(0, 127, topPitch - static_cast<int>(y / keyHeight)); }
double PianoRoll::sampleForX(float x) const noexcept { return juce::jmax(0.0, static_cast<double>(x - 42.0f) * (trackModel != nullptr ? trackModel->getSampleRate() / 80.0 : 1.0)); }

void PianoRoll::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff161b22));
    const auto visibleKeys = juce::jmax(1, juce::roundToInt(getHeight() / keyHeight));
    for (int row = 0; row < visibleKeys; ++row)
    {
        const auto key = topPitch - row;
        if (key < 0) break;
        const auto y = static_cast<int>(row * keyHeight);
        g.setColour((key % 12 == 1 || key % 12 == 3 || key % 12 == 6 || key % 12 == 8 || key % 12 == 10)
                        ? juce::Colour(0xff101419) : juce::Colour(0xff202833));
        g.fillRect(42, y, getWidth() - 42, juce::roundToInt(keyHeight));
        g.setColour(key % 12 == 0 ? juce::Colour(0xffd8d8d8) : juce::Colour(0xff8f8f8f));
        g.fillRect(0, y, 40, juce::roundToInt(keyHeight));
        if (key % 12 == 0) { g.setColour(juce::Colours::black); g.setFont(9.0f); g.drawText("C" + juce::String(key / 12 - 1), 3, y, 34, juce::roundToInt(keyHeight), juce::Justification::centredLeft, false); }
        g.setColour(juce::Colours::white.withAlpha(0.08f)); g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }
    if (trackModel == nullptr) return;
    const auto gridSamples = trackModel->getSampleRate() * 60.0 / trackModel->getBpm() * 0.25;
    const auto gridWidth = static_cast<float>(gridSamples * 80.0 / trackModel->getSampleRate());
    for (auto x = 42.0f; gridWidth > 1.0f && x < getWidth(); x += gridWidth)
    {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, static_cast<float>(getHeight()));
    }
    // Painting must remain read-only. A new clip is created only in response to
    // an edit gesture in mouseDown().
    auto id = activeClip;
    bool foundActiveClip = false;
    for (const auto& clip : trackModel->getMidiClips())
        foundActiveClip = foundActiveClip || clip.id == id;
    if (! foundActiveClip && ! trackModel->getMidiClips().empty())
        id = trackModel->getMidiClips().front().id;
    if (! id.isValid()) return;
    for (const auto& clip : trackModel->getMidiClips()) if (clip.id == id)
        for (const auto& note : clip.notes)
        {
            const auto x = 42.0f + static_cast<float>(note.startSample * 80.0 / trackModel->getSampleRate());
            const auto width = juce::jmax(4.0f, static_cast<float>(note.durationSamples * 80.0 / trackModel->getSampleRate()));
            const auto y = static_cast<float>((topPitch - note.pitch) * keyHeight);
            if (y < -keyHeight || y >= getHeight()) continue;
            g.setColour(note.id == selectedNote ? juce::Colour(0xffffd166) : juce::Colour(0xff00c8ff).withAlpha(0.35f + note.velocity * 0.65f));
            g.fillRoundedRectangle(x, y + 1.0f, width, 10.0f, 2.0f);
            if (note.id == selectedNote) { g.setColour(juce::Colours::white); g.drawRoundedRectangle(x, y + 1.0f, width, 10.0f, 2.0f, 1.0f); }
        }
}

void PianoRoll::mouseDown(const juce::MouseEvent& event)
{
    const auto clipId = ensureActiveClip(); if (! clipId.isValid() || trackModel == nullptr) return;
    if (event.position.x < 42.0f) return;
    const auto pitch = pitchForY(event.position.y); const auto sample = trackModel->getSnappedSamplePosition(sampleForX(event.position.x), 0.25);
    for (const auto& clip : trackModel->getMidiClips()) if (clip.id == clipId)
        for (const auto& note : clip.notes)
            if (note.pitch == pitch && sample >= note.startSample && sample <= note.startSample + note.durationSamples)
            { selectedNote = note.id; if (event.mods.isRightButtonDown()) trackModel->deleteMidiNote(clipId, note.id); repaint(); return; }
    if (! event.mods.isRightButtonDown()) selectedNote = trackModel->addMidiNote(clipId, pitch, 0.8f, sample, trackModel->getSampleRate() * 0.25, 1);
    repaint();
}

void PianoRoll::mouseDrag(const juce::MouseEvent& event)
{
    if (! selectedNote.isValid() || trackModel == nullptr) return;
    if (event.mods.isShiftDown())
        for (const auto& clip : trackModel->getMidiClips()) if (clip.id == ensureActiveClip()) for (const auto& note : clip.notes) if (note.id == selectedNote)
            trackModel->resizeMidiNote(activeClip, selectedNote, trackModel->getSnappedSamplePosition(sampleForX(event.position.x), 0.25) - note.startSample);
    else
        trackModel->moveMidiNote(ensureActiveClip(), selectedNote, pitchForY(event.position.y), trackModel->getSnappedSamplePosition(sampleForX(event.position.x), 0.25));
    repaint();
}

void PianoRoll::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (trackModel == nullptr) return;
    if (! selectedNote.isValid())
    {
        topPitch = juce::jlimit(20, 127, topPitch + (wheel.deltaY > 0.0f ? 4 : -4));
        repaint();
        return;
    }
    for (const auto& clip : trackModel->getMidiClips()) if (clip.id == ensureActiveClip()) for (const auto& note : clip.notes) if (note.id == selectedNote)
        trackModel->setMidiNoteVelocity(activeClip, selectedNote, note.velocity + wheel.deltaY * 0.1f);
    repaint();
}
