#!/bin/zsh
set -euo pipefail

if (( $# < 2 || $# > 4 )); then
  print -u2 "usage: $0 INPUT.wav OUTPUT_DIRECTORY [VIBE_QUERY] [ARTIST_QUERY]"
  exit 64
fi

input=${1:A}
output=$2
vibe_query=${3:-VibeAutotune}
artist_query=${4:-Auto-Tune Artist}
root=${0:A:h:h}
runner=${STUDIOFORGE_CALIBRATION_RUNNER:-$root/build-macos-arm64/StudioForgeCalibrationRunner}

[[ -x $runner ]] || { print -u2 "missing runner: $runner"; exit 1; }
if [[ ! -f $input ]]; then
  print -u2 "input WAV does not exist: $input"
  print -u2 "pass the path to an unprocessed vocal WAV; for a bundled smoke fixture use:"
  print -u2 "  autotune/benchmark/autotune-artist-colour-probe/fsharp-minor/vowel_fs4_flat30_minus12dbfs.wav"
  exit 66
fi
mkdir -p "$output"
"$runner" "$input" "$output/vibe" "$vibe_query"
"$runner" "$input" "$output/artist" "$artist_query"
python3 "$root/Tools/analyze_studioforge_calibration.py" "$input" \
  "$output/vibe/calibration-report.csv" "$output/artist/calibration-report.csv" "$output/comparison"
print "Complete: $output/comparison/calibration-report.md"
