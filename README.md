# Vocal Editor

**A Product by Avlirecordlabel**  
Professional vocal editing, intelligent analysis, and precision control.

> **Project status:** Early development / prototype. This repository does not yet implement every feature in this specification. Features marked as planned must not be presented as working until implemented and tested.

## Product vision

Vocal Editor is planned as a professional Windows standalone application and VST3 plug-in for Cubase. Its central purpose is precise vocal editing at phrase, word, syllable, phoneme, and custom-region level. It combines direct manual control with AI-assisted analysis while keeping the user in control of every destructive or audible change.

The product has three pillars:

- **Precision:** editable gain, fades, envelopes, boundaries, and reliable undo/redo.
- **Intelligence:** audio analysis, word-boundary proposals, vocal leveling suggestions, breath/silence detection, and later pitch analysis.
- **Professional workflow:** a refined interface, stable audio processing, project persistence, standalone file handling, and VST3 host integration.

## Current implementation snapshot

The current codebase is an early JUCE prototype. The visible editor currently includes basic audio-file selection and waveform thumbnail display, a global Gain parameter, and a simple sample-threshold gate parameter. The Analyze button does **not** run an AI model, the Play button does **not** implement editor playback, and word/phoneme event editing, per-event gain, fade processing, recording, pitch correction, and the full project workflow are not implemented yet.

This snapshot is intentionally explicit so that a UI control or roadmap item is not mistaken for a completed feature.

## Target platforms and technology

- **Operating system:** Windows.
- **Formats:** standalone application and VST3 plug-in.
- **DAW target:** Cubase, with compatibility to be validated on actual supported host versions.
- **Primary implementation:** C++ and JUCE.
- **Audio processing:** shared DSP components where appropriate; host real-time processing kept separate from file analysis and UI work.
- **AI inference:** evaluate local inference with compatible runtimes such as ONNX Runtime, selecting models only after quality, license, memory, CPU/GPU, and distribution checks.

The standalone app and plug-in share core editing and DSP concepts, but their I/O capabilities differ. Recording and file management belong to the standalone workflow; plug-in capabilities must respect the VST3 host and DAW.

## Core editing requirements

### Event-based vocal editing

Users should be able to select or create editable regions at these levels:

- Phrase
- Word
- Syllable
- Phoneme, where detection quality permits
- Any manually selected time range

Automatic boundaries are suggestions, not immutable truth. Users must be able to move boundaries, create splits, merge adjacent regions, and work manually when analysis is unavailable or inaccurate.

### Independent gain

Each editable event should support:

- Gain adjustment in dB with numeric entry and draggable controls.
- Envelope points and smooth transitions.
- Multi-selection and batch edits.
- Per-event reset and preview.
- Non-destructive editing and reliable undo/redo.
- Protection against accidentally changing neighboring events.

The DSP engine must apply these edits to the actual audio. Displaying a gain value without changing the rendered signal is not sufficient. Transitions must avoid clicks and discontinuities.

### Fade In / Fade Out

Every selected event or custom region should have independent Fade In and Fade Out controls:

- Duration in milliseconds.
- Draggable waveform handles and numeric entry.
- Linear and smooth curves; custom curves may be added later.
- Preview, bypass, and reset.
- Safe handling of short regions and overlapping fades.
- Click/pop prevention and smooth boundary behavior.

Gain and fades are independent parameters that must combine correctly in the audio engine.

### Editing workflow

- Split and merge regions.
- Select one or multiple regions.
- Edit region boundaries and timing.
- Preview changes before applying them.
- Undo and redo supported operations.
- Save and restore the editing state.
- Preserve the original source and avoid irreversible changes by default.

## AI-assisted analysis

AI is a core product direction, but each capability requires a real, validated analysis implementation. The first release should prioritize word detection and intelligent leveling.

### Planned analysis modules

1. **Word/phrase detection:** propose word and phrase boundaries; allow manual correction.
2. **Phoneme/syllable alignment:** investigate model support and language coverage; show uncertainty where boundaries are unreliable.
3. **Smart Leveling:** analyze local loudness and vocal context, propose event gain in dB, and let the user preview, accept, reject, or edit suggestions.
4. **Silence detection:** find low-level regions using configurable threshold and minimum duration.
5. **Breath detection:** identify probable breaths and propose reduction while preserving natural performance.
6. **Sibilance detection:** flag potentially excessive sibilants and propose local de-essing or gain reduction.
7. **Audio quality checks:** identify clipping, unusual peaks, and other detectable issues.
8. **Pitch analysis:** estimate notes and key as a foundation for a later pitch-editing engine.
9. **Mix assistant:** later suggest compressor, de-esser, EQ, and level settings based on analysis.

### AI user experience

The planned assistant may include actions such as Analyze Vocal, Detect Words, Smart Leveling, Detect Breaths, Detect Silence, Detect Sibilance, Pitch Analysis, and Review Suggestions.

The workflow should be:

1. Analyze the selected audio or file.
2. Show detected regions, suggestions, and confidence/uncertainty.
3. Let the user audition before/after.
4. Allow accept, reject, edit, or batch-apply.
5. Preserve undo and restore the original state.

The assistant must not silently apply irreversible edits. Manual editing must remain useful when AI is disabled or unavailable.

### Local processing and privacy

Prefer local processing for supported models so that vocal recordings can remain on the user's machine. Cloud inference, if ever added, must be optional, clearly disclosed, and explicitly authorized. Model choice must consider language accuracy (including Persian where supported), latency, RAM/CPU/GPU needs, licensing, package size, and redistribution rights.

## Silence, breaths, sibilance, and clicks

Silence operations must be distinct:

- **Mute:** silence a selected region.
- **Remove:** remove a region and handle the resulting timeline gap according to a clear user-selected behavior.
- **Shorten:** reduce a region to a target duration.

Controls should include threshold, minimum duration, target duration, boundary fades, preview, and confirmation. Breath reduction should preserve natural breaths by default. Sibilance, breaths, clicks, and silence are different detection problems and should not be treated as one generic detector.

## Dynamics and metering

Planned processing tools:

- Gain riding and event automation.
- Compressor: threshold, ratio, attack, release, and makeup gain.
- De-esser with frequency and amount controls.
- Peak and true-peak metering.
- LUFS measurement for perceptual loudness context.
- A/B comparison and bypass.
- Smooth parameter transitions to avoid clicks.

These tools complement event-level manual gain editing; they must not replace it. Loudness meters alone do not determine whether individual words sound balanced.

## Pitch, timing, and formants (advanced roadmap)

A later stage may include:

- Pitch and note visualization.
- User-selectable root note and scale (major, minor, and additional supported scales).
- Visual indication of in-scale/out-of-scale notes.
- Manual and assisted pitch correction.
- Note timing and duration edits.
- Formant processing where a suitable engine is available.

A note display alone is not pitch correction. This stage requires a real pitch-detection and pitch-processing engine, validated for sound quality and latency.

## Recording, files, and project management

### Standalone

Planned capabilities:

- Import audio via file chooser and drag-and-drop.
- Audio device selection and input metering.
- Record, stop, save, and edit recorded audio.
- Playback and accurate timeline navigation.
- Export processed audio.
- Reopen projects with edits and settings preserved.
- Clear recovery when a source file has moved or is missing.

Supported formats and sample rates must be verified in implementation and documented.

### VST3

The plug-in should process the host's audio and persist its supported state in Cubase. File import, recording, and timeline editing inside the plug-in must be designed around host constraints rather than assumed to work exactly like the standalone application. ARA may be investigated separately but is not assumed to be available through ordinary VST3.

## UI and visual design

The target is a refined, modern, dark interface with graphite panels and restrained cyan/teal and blue accents. Typography, spacing, alignment, and interaction accuracy are product requirements—not decoration.

### Planned layout

- **Top toolbar:** Vocal Editor branding, Auto/Manual/Edit modes, undo/redo, save, settings, language, and processing status.
- **Main timeline:** waveform, time ruler, playhead, zoom, scroll, editable events, gain envelopes, and fade handles.
- **Selected Event inspector:** event type, start/end, duration, gain, fades, curve, and relevant analysis suggestions.
- **Event list:** event labels, timing, gain, fade state, and analysis status.
- **Transport:** playback controls and accurate navigation.
- **AI Assistant:** analysis controls, findings, confidence, preview, and approval workflow.
- **Manual controls:** precise direct access to supported editing parameters.

### UX quality bar

- No overlapping text, clipped labels, or controls outside their panels.
- Resizable window and robust layout at supported sizes.
- Correct mouse hit-testing, selection, playback position, zoom, and scroll.
- Clear selected, hover, disabled, processing, and error states.
- Useful tooltips and keyboard shortcuts.
- Consistent typography, icons, spacing, and control behavior.
- Persian and English UI; Persian requires right-to-left layout and correct glyph shaping.
- Avoid decorative controls that do not perform their advertised action.

## Branding and localization

Primary product name: **Vocal Editor**  
Brand credit: **A Product by Avlirecordlabel**

Initial languages: English and Persian. German, French, Spanish, Turkish, and Arabic may be considered later. All user-facing text, errors, settings, and dialogs should use a consistent localization system.

## Architecture and engineering requirements

Separate the system into maintainable components:

- UI and interaction layer.
- Project/event data model.
- Audio DSP engine.
- File I/O and recording layer.
- AI/model inference and analysis layer.
- Project persistence and migration.
- Host integration and plug-in parameter/state handling.

Important engineering constraints:

- Do not run heavy analysis on the real-time audio thread.
- Avoid unnecessary memory allocation, blocking locks, and file I/O in the audio callback.
- Make parameter changes thread-safe and smooth.
- Save and restore plug-in state correctly.
- Handle different sample rates, channel layouts, and block sizes.
- Prevent analysis failures from crashing the UI or audio engine.
- Version project data and maintain backward-compatibility where practical.
- Track dependencies and licenses, especially for AI models.

## Roadmap

### Phase 1 — Stable foundation
- Review and stabilize the existing JUCE project.
- Refine responsive UI and typography.
- Implement reliable waveform display, timeline, selection, zoom, scroll, and playback behavior.

### Phase 2 — Core manual editing
- Build the event data model.
- Implement per-event gain and envelopes.
- Implement real per-event Fade In/Fade Out.
- Add split, merge, multi-selection, undo/redo, preview, and project persistence.

### Phase 3 — Standalone and VST3 workflow
- Implement standalone import/export and recording.
- Validate plug-in audio processing and state persistence in Cubase.
- Add automated build and regression checks.

### Phase 4 — First useful AI release
- Integrate and evaluate word/phrase detection.
- Implement analysis results and confidence/uncertainty.
- Add Smart Leveling suggestions with preview and user approval.
- Add silence detection and manual correction.

### Phase 5 — Advanced analysis and dynamics
- Breath and sibilance detection.
- Compressor, de-esser, and metering.
- Improve local inference performance and language coverage.

### Phase 6 — Pitch editing and release polish
- Evaluate and integrate a suitable pitch-processing engine.
- Add note editing and timing/formant capabilities where feasible.
- Complete localization, packaging, host compatibility testing, documentation, and release QA.

## Definition of done / acceptance tests

Before a feature is called complete, verify that:

- Gain changes the actual selected audio region and not neighboring regions.
- Fades affect the rendered audio and avoid audible clicks.
- Playback starts at the intended timeline position.
- Zoom and scrolling behave correctly.
- Undo/redo restore edits accurately.
- Project and plug-in state survive save/reopen.
- AI analysis does not freeze the UI or interrupt real-time audio.
- Suggestions can be previewed, rejected, edited, and undone.
- UI remains readable in English and Persian at supported window sizes.
- Standalone and VST3 pass tests in their actual target environments.
- CPU, memory, latency, and audio quality are measured on representative projects.
- Every visible control performs its advertised action; unfinished capabilities are clearly marked.

## Immediate next steps

1. Audit the current source and build before large refactors.
2. Fix existing compiler errors and UI/layout issues.
3. Confirm current audio processing behavior and parameter state.
4. Implement a reliable event model and per-region gain/fades.
5. Add real playback/timeline interaction.
6. Integrate AI only after the editing model can represent and apply its suggestions safely.

The goal is a genuinely useful vocal editor—not merely a large binary, an attractive mock-up, or a collection of inactive buttons.
