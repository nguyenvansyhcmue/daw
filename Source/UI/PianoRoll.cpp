#include "PianoRoll.h"

#include <algorithm>

namespace
{
constexpr float noteResizeHandleWidth = 7.0f;
}

PianoRoll::PianoRoll(TrackDataModel* model) : trackModel(model)
{
    setWantsKeyboardFocus(true);
}

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
    const auto editorHeight = noteEditorHeight();
    const auto visibleKeys = juce::jmax(1, juce::roundToInt(editorHeight / keyHeight));
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
        g.drawVerticalLine(juce::roundToInt(x), 0.0f, editorHeight);
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
            const auto previewing = draggingNote && note.id == selectedNote;
            const auto startSample = previewing ? noteDragPreviewStartSample : note.startSample;
            const auto durationSamples = previewing ? noteDragPreviewDurationSamples : note.durationSamples;
            const auto pitch = previewing ? noteDragPreviewPitch : note.pitch;
            const auto x = 42.0f + static_cast<float>(startSample * 80.0 / trackModel->getSampleRate());
            const auto width = juce::jmax(4.0f, static_cast<float>(durationSamples * 80.0 / trackModel->getSampleRate()));
            const auto y = static_cast<float>((topPitch - pitch) * keyHeight);
            if (y < -keyHeight || y >= editorHeight) continue;
            const auto previewingVelocity = (draggingNote || editingVelocity) && note.id == selectedNote;
            const auto velocity = previewingVelocity ? velocityPreview : note.velocity;
            g.setColour(note.id == selectedNote ? juce::Colour(0xffffd166) : juce::Colour(0xff00c8ff).withAlpha(0.35f + velocity * 0.65f));
            g.fillRoundedRectangle(x, y + 1.0f, width, 10.0f, 2.0f);
            if (note.id == selectedNote) { g.setColour(juce::Colours::white); g.drawRoundedRectangle(x, y + 1.0f, width, 10.0f, 2.0f, 1.0f); }
        }

    const auto velocityBounds = juce::Rectangle<float>(42.0f, editorHeight,
                                                        juce::jmax(0.0f, static_cast<float>(getWidth()) - 42.0f),
                                                        velocityLaneHeight);
    g.setColour(juce::Colour(0xff111820));
    g.fillRect(velocityBounds);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawHorizontalLine(juce::roundToInt(editorHeight), 0.0f, static_cast<float>(getWidth()));
    g.setColour(juce::Colours::white.withAlpha(0.55f));
    g.setFont(9.0f);
    g.drawText("VELOCITY", 4, juce::roundToInt(editorHeight + 5.0f), 34, 14, juce::Justification::centredLeft, false);
    for (const auto& clip : trackModel->getMidiClips())
    {
        if (clip.id != id)
            continue;
        for (const auto& note : clip.notes)
        {
            const auto velocity = editingVelocity && note.id == selectedNote ? velocityPreview : note.velocity;
            const auto x = 42.0f + static_cast<float>(note.startSample * 80.0 / trackModel->getSampleRate());
            const auto width = juce::jmax(3.0f, static_cast<float>(note.durationSamples * 80.0 / trackModel->getSampleRate()));
            const auto height = velocity * (velocityLaneHeight - 18.0f);
            const auto bar = juce::Rectangle<float>(x, velocityBounds.getBottom() - 4.0f - height, width, height);
            g.setColour(note.id == selectedNote ? juce::Colour(0xffffd166) : juce::Colour(0xff00c8ff).withAlpha(0.72f));
            g.fillRect(bar);
        }
    }
}

void PianoRoll::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    const auto clipId = ensureActiveClip(); if (! clipId.isValid() || trackModel == nullptr) return;
    if (event.position.x < 42.0f) return;
    if (event.position.y >= noteEditorHeight())
    {
        for (const auto& clip : trackModel->getMidiClips())
        {
            if (clip.id != clipId)
                continue;
            for (const auto& note : clip.notes)
            {
                const auto x = 42.0f + static_cast<float>(note.startSample * 80.0 / trackModel->getSampleRate());
                const auto width = juce::jmax(3.0f, static_cast<float>(note.durationSamples * 80.0 / trackModel->getSampleRate()));
                if (event.position.x < x || event.position.x > x + width)
                    continue;
                selectedNote = note.id;
                editingVelocity = true;
                velocityOriginal = note.velocity;
                velocityPreview = juce::jlimit(0.0f, 1.0f,
                                                (noteEditorHeight() + velocityLaneHeight - 4.0f - event.position.y)
                                                / (velocityLaneHeight - 18.0f));
                repaint();
                return;
            }
        }
        return;
    }
    const auto pitch = pitchForY(event.position.y); const auto sample = trackModel->getSnappedSamplePosition(sampleForX(event.position.x), 0.25);
    for (const auto& clip : trackModel->getMidiClips()) if (clip.id == clipId)
        for (const auto& note : clip.notes)
            if (note.pitch == pitch && sample >= note.startSample && sample <= note.startSample + note.durationSamples)
            {
                selectedNote = note.id;
                if (event.mods.isRightButtonDown())
                {
                    trackModel->deleteMidiNote(clipId, note.id);
                    selectedNote = {};
                }
                else
                {
                    draggingNote = true;
                    const auto noteEndX = 42.0f + static_cast<float>((note.startSample + note.durationSamples)
                                                                     * 80.0 / trackModel->getSampleRate());
                    resizingNote = event.mods.isShiftDown()
                        || std::abs(event.position.x - noteEndX) <= noteResizeHandleWidth;
                    noteDragOriginalPitch = note.pitch;
                    noteDragPreviewPitch = note.pitch;
                    noteDragOriginalStartSample = note.startSample;
                    noteDragPreviewStartSample = note.startSample;
                    noteDragOriginalDurationSamples = note.durationSamples;
                    noteDragPreviewDurationSamples = note.durationSamples;
                    noteDragGrabOffsetSamples = juce::jmax(0.0, sample - note.startSample);
                }
                repaint();
                return;
            }
    if (! event.mods.isRightButtonDown()) selectedNote = trackModel->addMidiNote(clipId, pitch, 0.8f, sample, trackModel->getSampleRate() * 0.25, 1);
    repaint();
}

void PianoRoll::mouseMove(const juce::MouseEvent& event)
{
    if (trackModel == nullptr || activeClip.isValid() == false || event.position.x < 42.0f)
        return;

    for (const auto& clip : trackModel->getMidiClips())
    {
        if (clip.id != activeClip)
            continue;
        for (const auto& note : clip.notes)
        {
            const auto noteY = static_cast<float>((topPitch - note.pitch) * keyHeight);
            const auto noteEndX = 42.0f + static_cast<float>((note.startSample + note.durationSamples)
                                                             * 80.0 / trackModel->getSampleRate());
            if (event.position.y >= noteY && event.position.y <= noteY + keyHeight
                && std::abs(event.position.x - noteEndX) <= noteResizeHandleWidth)
            {
                setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDrag(const juce::MouseEvent& event)
{
    if (editingVelocity)
    {
        velocityPreview = juce::jlimit(0.0f, 1.0f,
                                        (noteEditorHeight() + velocityLaneHeight - 4.0f - event.position.y)
                                        / (velocityLaneHeight - 18.0f));
        repaint();
        return;
    }
    if (! draggingNote || ! selectedNote.isValid() || trackModel == nullptr || ! activeClip.isValid()) return;

    const auto snappedSample = trackModel->getSnappedSamplePosition(sampleForX(event.position.x), 0.25);
    if (resizingNote)
        noteDragPreviewDurationSamples = juce::jmax(1.0, snappedSample - noteDragOriginalStartSample);
    else
    {
        noteDragPreviewPitch = pitchForY(event.position.y);
        noteDragPreviewStartSample = trackModel->getSnappedSamplePosition(
            juce::jmax(0.0, snappedSample - noteDragGrabOffsetSamples), 0.25);
    }
    repaint();
}

void PianoRoll::mouseUp(const juce::MouseEvent&)
{
    if (editingVelocity && trackModel != nullptr && activeClip.isValid() && selectedNote.isValid()
        && velocityPreview != velocityOriginal)
        trackModel->setMidiNoteVelocity(activeClip, selectedNote, velocityPreview);
    if (draggingNote && trackModel != nullptr && activeClip.isValid() && selectedNote.isValid())
    {
        if (resizingNote && noteDragPreviewDurationSamples != noteDragOriginalDurationSamples)
            trackModel->resizeMidiNote(activeClip, selectedNote, noteDragPreviewDurationSamples);
        else if (! resizingNote && (noteDragPreviewPitch != noteDragOriginalPitch
                                    || noteDragPreviewStartSample != noteDragOriginalStartSample))
            trackModel->moveMidiNote(activeClip, selectedNote, noteDragPreviewPitch, noteDragPreviewStartSample);
    }
    draggingNote = false;
    resizingNote = false;
    editingVelocity = false;
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

bool PianoRoll::keyPressed(const juce::KeyPress& key)
{
    if (trackModel == nullptr || ! ensureActiveClip().isValid())
        return false;

    const auto hasPrimaryShortcutModifier = key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown();
    if (hasPrimaryShortcutModifier && (key.getKeyCode() == 'v' || key.getKeyCode() == 'V') && hasCopiedNote)
    {
        const auto pasteSample = trackModel->getSnappedSamplePosition(trackModel->getPlayheadPosition(), 0.25);
        selectedNote = trackModel->addMidiNote(activeClip, copiedNote.pitch, copiedNote.velocity, pasteSample,
                                               copiedNote.durationSamples, copiedNote.channel);
        repaint();
        return selectedNote.isValid();
    }

    if (key.getTextCharacter() == 'q' || key.getTextCharacter() == 'Q')
    {
        const auto gridSamples = trackModel->getSampleRate() * 60.0 / trackModel->getBpm() * 0.25;
        return trackModel->quantizeMidiClip(activeClip, gridSamples);
    }
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && selectedNote.isValid())
    {
        const auto deleted = trackModel->deleteMidiNote(activeClip, selectedNote);
        if (deleted)
            selectedNote = {};
        repaint();
        return deleted;
    }

    if (selectedNote.isValid())
    {
        for (const auto& clip : trackModel->getMidiClips())
        {
            if (clip.id != activeClip)
                continue;
            const auto note = std::find_if(clip.notes.begin(), clip.notes.end(), [this] (const MidiNoteEvent& candidate)
            {
                return candidate.id == selectedNote;
            });
            if (note == clip.notes.end())
                return false;

            if (hasPrimaryShortcutModifier && (key.getKeyCode() == 'c' || key.getKeyCode() == 'C'))
            {
                copiedNote = *note;
                hasCopiedNote = true;
                return true;
            }
            if (hasPrimaryShortcutModifier && (key.getKeyCode() == 'd' || key.getKeyCode() == 'D'))
            {
                selectedNote = trackModel->addMidiNote(activeClip, note->pitch, note->velocity,
                                                       note->startSample + note->durationSamples,
                                                       note->durationSamples, note->channel);
                repaint();
                return selectedNote.isValid();
            }

            if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
            {
                const auto pitchOffset = key == juce::KeyPress::upKey ? 1 : -1;
                const auto moved = trackModel->moveMidiNote(activeClip, selectedNote, note->pitch + pitchOffset,
                                                            note->startSample);
                repaint();
                return moved;
            }
            if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            {
                const auto samplesPerBeat = trackModel->getSampleRate() * 60.0 / trackModel->getBpm();
                const auto step = samplesPerBeat * (key.getModifiers().isShiftDown() ? 1.0 / 960.0 : 1.0 / 16.0);
                const auto offset = key == juce::KeyPress::leftKey ? -step : step;
                const auto moved = trackModel->moveMidiNote(activeClip, selectedNote, note->pitch,
                                                            juce::jmax(0.0, note->startSample + offset));
                repaint();
                return moved;
            }
            break;
        }
    }
    if (key == juce::KeyPress::upKey)
        return trackModel->transposeMidiClip(activeClip, 1);
    if (key == juce::KeyPress::downKey)
        return trackModel->transposeMidiClip(activeClip, -1);
    return false;
}
