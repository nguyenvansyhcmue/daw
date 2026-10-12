#!/bin/zsh
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
xcrun swiftc -O "$script_dir/SystemAudioCapture.swift" -o "$script_dir/system_audio_capture"
