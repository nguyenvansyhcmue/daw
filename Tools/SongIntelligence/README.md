# StudioForge Song Intelligence bridge

`smart_studio_bridge.py` exposes the existing `smart_studio` analysis code to
StudioForge without starting FastAPI or keeping a crawler alive.

It accepts one JSON request file and writes one JSON response to standard
output.  The two supported operations are:

```json
{"operation":"analyze_file","audio_path":"C:/music/song.wav","input_type":"Full Mix"}
```

```json
{"operation":"lookup_song","title":"Song title","artist":"Artist"}
```

`lookup_song` is the cache-first production route from `smart_studio`: it
retrieves SongBPM, SongData/CDP, and chord-chart information concurrently using
the existing per-source timeouts and caches. `catalog_search` remains available
when the UI needs only a list of candidates before the user chooses one.

Run it with the `smart_studio` virtual environment so its DSP dependencies are
available.  Set `STUDIOFORGE_SMART_STUDIO_ROOT` to the `smart_studio` checkout
during development.  A release build will package this worker and its licensed
dependencies beside the DAW; it will not rely on a source checkout or run a
web server.

Every result deliberately includes `requires_user_confirmation: true`.
StudioForge may cache an approved result in a performance arrangement, but it
must never change a song key, BPM, chord map, or Auto-Tune setup automatically.
