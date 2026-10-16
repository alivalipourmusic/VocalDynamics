# Vocal Dynamics — Music Analysis Requirements

These features are part of the Vocal Dynamics product scope for the standalone Windows editor. They must use the loaded audio file and report confidence/limitations instead of displaying fabricated results.

## Transport and import
- Accept supported audio files dropped from Windows Explorer onto the editor.
- Show current position and total duration as `MM:SS.mmm`.
- Keep the playhead, waveform time mapping, seeking, and audible transport position synchronized.

## Offline music analysis
- **Tempo (BPM):** analyze the loaded audio offline, estimate BPM and confidence, and show likely alternatives (for example half-time/double-time) where ambiguous. Provide a manual BPM override and an optional beat grid on the timeline.
- **Musical key:** estimate the likely major/minor key from the loaded audio, display a confidence indicator and ranked alternatives, and allow manual override. Do not present a low-confidence estimate as certain.
- **Vocal pitch view (later stage):** estimate voiced-pitch contours and note names for vocal passages, clearly separating pitch analysis from automatic pitch correction.

## Vocal editing
- Offline multilingual transcription with word timestamps; users can select individual detected words and adjust gain.
- Gain changes must affect both playback and rendered WAV export.
- Future analysis should identify silence, breaths, plosive/sibilant transients, and peaks, with proposed edits requiring user confirmation.

## Implementation and acceptance criteria
- Analysis runs locally on Windows and does not require uploading audio to a cloud service.
- BPM/key analysis must run against the actual loaded audio, not demo values.
- Display the analysis state (not analyzed / analyzing / completed / error) and provide actionable error messages.
- Verify with several real audio files, including vocal-only and full-mix examples, before claiming the analysis is accurate.
- A successful GitHub commit is not evidence of a successful build or runtime test; report these separately.

## Current status
This file records the requested product scope. The tempo/key analysis engine and UI are not yet implemented by this specification alone.
