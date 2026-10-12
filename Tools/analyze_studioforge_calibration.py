#!/usr/bin/env python3
"""Create latency-aligned F0 and transition reports for StudioForge bounces."""

import argparse
import csv
import math
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "autotune" / "tools"))
from compare_pitch_correction_bounces import align, pitch_track, read_mono
from measure_artist_vibe_latency_aligned import envelope_delay


def midi(hz):
    return 69.0 + 12.0 * np.log2(hz / 440.0)


def load_cases(report):
    with open(report, newline="") as source:
        return list(csv.DictReader(source))


def key(row):
    return tuple(row[field] for field in ("retune_ms", "flex_tune", "humanize", "natural_vibrato"))


def trace_and_transitions(signal, rate, label, output):
    # 64-sample hops at 48 kHz give a 1.33 ms F0 trace, sufficiently dense to
    # inspect correction transitions without inventing interpolated frames.
    trace = pitch_track(signal, rate)
    valid = np.isfinite(trace[:, 1]) & (trace[:, 2] >= 0.55)
    times = trace[valid, 0] / rate
    notes = midi(trace[valid, 1])
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["time_seconds", "f0_hz", "midi_note", "confidence"])
        for index in np.flatnonzero(valid):
            writer.writerow([trace[index, 0] / rate, trace[index, 1], midi(trace[index, 1]), trace[index, 2]])

    # A transition is a sustained change of >= 25 cents. Contiguous slope
    # frames are one event; duration avoids counting vibrato as note changes.
    events = []
    if len(times) > 1:
        changes = np.abs(np.diff(notes)) * 100.0
        active = changes >= 25.0
        start = None
        for index, changed in enumerate(np.r_[active, False]):
            if changed and start is None:
                start = index
            elif not changed and start is not None:
                end = index
                duration = times[end] - times[start]
                cents = abs(notes[end] - notes[start]) * 100.0
                if duration >= 0.004 and cents >= 25.0:
                    events.append((times[start], times[end], duration, cents))
                start = None
    return trace, valid, events


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("raw", type=Path)
    parser.add_argument("vibe_report", type=Path)
    parser.add_argument("artist_report", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rate, raw = read_mono(args.raw)
    vibe = {key(row): row for row in load_cases(args.vibe_report)}
    artist = {key(row): row for row in load_cases(args.artist_report)}
    common = sorted(vibe.keys() & artist.keys())
    if not common:
        raise RuntimeError("Vibe and Artist reports have no matching parameter rows")

    rows, markdown = [], ["# StudioForge Artist-vs-Vibe calibration", "",
                           "| Case | Artist latency | Vibe latency | F0 median Δ | Artist transitions | Vibe transitions |",
                           "|---|---:|---:|---:|---:|---:|"]
    for number, case in enumerate(common):
        artist_rate, artist_signal = read_mono(Path(artist[case]["output"]))
        vibe_rate, vibe_signal = read_mono(Path(vibe[case]["output"]))
        if artist_rate != rate or vibe_rate != rate:
            raise RuntimeError("All bounces must use the raw input sample rate")
        # Waveform correlation is invalid for PSOLA A/B: matching pitch may
        # have different phase and grain placement. Align the RMS envelope.
        artist_lag = envelope_delay(raw, artist_signal, rate)
        vibe_lag = envelope_delay(raw, vibe_signal, rate)
        _, artist_aligned = align(raw, artist_signal, artist_lag)
        _, vibe_aligned = align(raw, vibe_signal, vibe_lag)
        length = min(len(artist_aligned), len(vibe_aligned))
        artist_trace, artist_valid, artist_events = trace_and_transitions(
            artist_aligned[:length], rate, "artist", args.output / "f0-traces" / f"case-{number}-artist.csv")
        vibe_trace, vibe_valid, vibe_events = trace_and_transitions(
            vibe_aligned[:length], rate, "vibe", args.output / "f0-traces" / f"case-{number}-vibe.csv")
        usable = (np.isfinite(artist_trace[:, 1]) & np.isfinite(vibe_trace[:, 1])
                  & (artist_trace[:, 2] >= 0.55) & (vibe_trace[:, 2] >= 0.55))
        delta = 1200.0 * np.log2(vibe_trace[usable, 1] / artist_trace[usable, 1])
        median = float(np.median(np.abs(delta))) if len(delta) else math.nan
        row = [number, *case, artist_lag, vibe_lag, median, len(artist_events), len(vibe_events)]
        rows.append(row)
        markdown.append(f"| {number} | {artist_lag} samples | {vibe_lag} samples | {median:.2f} cents | {len(artist_events)} | {len(vibe_events)} |")

    header = ["case", "retune_ms", "flex_tune", "humanize", "natural_vibrato",
              "artist_latency_samples_vs_raw", "vibe_latency_samples_vs_raw",
              "vibe_minus_artist_f0_median_absolute_cents", "artist_transition_count", "vibe_transition_count"]
    with (args.output / "calibration-report.csv").open("w", newline="") as handle:
        writer = csv.writer(handle); writer.writerow(header); writer.writerows(rows)
    (args.output / "calibration-report.md").write_text("\n".join(markdown) + "\n")


if __name__ == "__main__":
    main()
