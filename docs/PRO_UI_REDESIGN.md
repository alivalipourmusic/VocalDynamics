# Vocal Dynamics — Professional UI Redesign Brief

## Why redesign
The current JUCE editor is an early functional prototype, not a shippable professional audio product. Do not polish the current layout incrementally; redesign the visual system and interaction hierarchy as a coherent product.

## Reference research
- FabFilter Pro-Q 4: the main interactive analysis canvas dominates the window; controls appear contextually around the selected item; clear bottom-level/global controls; resizable/scalable interface. Reference: https://prod.fabfilter.com/help/pro-q/using/overview and https://prod.fabfilter.com/help/pro-q/using/fullscreenandresize
- Oeksound soothe2: specialized spectral processing UI with a clear main visual analysis area, carefully grouped macro controls, and explicit quality/settings section. Reference: https://oeksound.com/manuals/soothe2/
- These are workflow references, not assets to copy. Build a distinctive Vocal Dynamics identity.

## Art direction
- Premium, modern audio workstation aesthetic; deep near-black / graphite surfaces, subtle layered panels, restrained cyan/teal accent, limited secondary violet only for analysis overlays.
- Avoid generic rounded-card dashboards, oversized headings, bright outlines around every panel, and rows of same-looking default JUCE buttons.
- Use a compact, professional top bar for app identity, file/session, undo/redo, settings and transport state.
- Use consistent spacing, typography scale, alignment, icon family, subtle dividers, hover/focus/pressed/disabled states, and clear contrast.
- Build custom-drawn JUCE controls and waveform/timeline components rather than relying on default widget appearance. Ensure readable at normal Windows DPI and support resize/scaling.

## Main workspace
1. Top transport strip: file name, play/stop, loop, current time / total duration, tempo and key analysis status.
2. Main timeline (largest area): high-quality waveform, ruler in minutes/seconds/milliseconds, grid/beat overlay when tempo is analyzed, visible playhead synchronized to the actual audio transport, zoom and horizontal navigation.
3. Vocal event lane: real detected word segments only; word labels and boundaries; selection highlight; clear selected-word state; no fake placeholder regions.
4. Inspector panel: selected word, start/end time, gain in dB, input/output level, bypass/reset, and proposed automatic gain correction requiring explicit user approval.
5. Analysis panel or collapsible side section: transcription language/model, BPM estimate and confidence, key estimate and confidence, analysis progress/errors, silence/breath/peak findings.
6. Bottom status area: audio device, sample rate, buffer size, processing state and file duration.

## Interaction and usability
- Drag and drop supported audio files onto the waveform/workspace with an obvious drag-hover state.
- The waveform, ruler, playhead, seeking, event boundaries and audio transport must share one authoritative time mapping.
- Space toggles playback; plus/minus zoom; keyboard focus should not break text editing or slider operation.
- Word selection seeks exactly to the word start and makes the selected word's controls available.
- Gain edits must be audible during playback and match offline WAV export.
- Display genuine analysis results only. Show confidence and clear error states rather than demo numbers.
- Add responsive layouts and resize support; do not hard-code controls to positions that overlap at smaller widths.

## Implementation sequence
1. Create a custom theme and reusable drawing helpers (backgrounds, typography, buttons, meters, knob, focus states).
2. Rebuild the main layout around the waveform/timeline and event lane; make it resize-safe.
3. Fix and validate audio/time synchronization before adding decorative animation.
4. Add functional meters, selected-event inspector and transport display.
5. Add BPM/key analysis views only when backed by working local analysis engines.
6. Compare screenshots at multiple window sizes and DPI settings; build and test on Windows before calling it production-ready.

## Acceptance criteria
- Looks intentionally designed, not like a generic form or a stack of rounded boxes.
- Main waveform and word timeline are the visual focus.
- No overlapping controls at supported window sizes.
- Every visible control has real behavior or is clearly disabled/not yet available; no decorative fake controls.
- Build and runtime validation must be reported separately from code commits.
