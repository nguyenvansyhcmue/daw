# Phase 7: Audio Recording Foundation

`RecordingSession` uses a preallocated capture buffer and JUCE `AudioFormatWriter::ThreadedWriter`. The device callback copies input to the writer FIFO only; WAV creation, flushing, decoding and clip creation execute on the control thread.

Recording targets a supplied track and file path, arms the target while active, starts at the current playhead and creates a decoded, playable clip when successfully stopped. FIFO pressure increments a dropped-block counter. A zero-sample recording finalizes safely without adding a clip; writer/decode failures return an error without adding invalid project state.

The current UI routes the Record button to track 0 and a temporary WAV path, matching the repository's minimal track-routing UI. There are no take lanes, punch workflows or input monitoring.
