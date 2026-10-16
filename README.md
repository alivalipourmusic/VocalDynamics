# Vocal Editor

**Precision Vocal Workstation** by Avlirecordlabel.

Vocal Editor is an in-development JUCE desktop audio editor and VST3 plugin for vocal-focused workflows.

## Current implemented areas
- Import and preview supported audio files in the standalone editor
- Playback controls, timeline zoom, and audio-device settings
- Offline speech-to-text integration through a user-selected Whisper CLI executable and model
- Editable detected regions with per-region gain and WAV export
- Preliminary tempo/key estimates and monophonic pitch-note analysis against a selected root and scale
- VST3 processing controls for gain, leveling, gain riding, de-essing, and bypass

## Important limitations
This project is actively being developed. Pitch detection and tempo/key analysis are preliminary estimates; scale highlighting is not a definitive judgment of musical correctness. Word timing quality depends on the selected speech model and CLI. LUFS/true-peak metering and intelligent leveling still require further validation. The current pitch analysis is not a Melodyne-equivalent pitch-editing engine.

## Build
Use CMake with a C++17-capable compiler and the JUCE 8.0.10 dependency declared in CMakeLists.txt.

The repository/project identifiers and internal C++ class names still use the historical VocalDynamics name to avoid unnecessary build and plugin compatibility changes while the visible product branding transitions to **Vocal Editor**.
