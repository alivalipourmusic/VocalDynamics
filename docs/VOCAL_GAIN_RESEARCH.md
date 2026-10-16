# Vocal Dynamics — Research: Manual Vocal Peak and Event Gain Editing

Research date: 2026-10-16

## Main reference: Arctic Labs Audio IcePick
Official site: https://arcticlabsaudio.com/
IcePick is an event-based de-esser: it detects individual sibilants, hard consonants and breaths, then lets the engineer inspect and edit events one by one. Its interface puts a large segmented waveform at the center, uses event-type tabs, shows detection counts, and offers a focused Tame Peaks action. Additional features include Swap Mode for auditioning/replacing an event with another from the same take and Formant Mode for shaping the harsh components. The product page currently says macOS only, Windows TBD, so it is not a Windows-ready dependency/reference product to copy verbatim.

## Manual gain envelope: iZotope RX
Reference: https://downloads.izotope.com/docs/rx6/11-application-menus/index.html and https://www.izotope.com/en/products/rx/features
RX's Clip Gain envelope allows adding/moving nodes and changing gain over time, including selected sections and fades. Its editing environment prioritizes a large waveform/spectrogram canvas and time/frequency selection. The latest RX product materials also emphasize repair modules, trim silence, history and resizable editing panels.

## Broader vocal/event workflows
- VoiceAssist's region editing presents detected breaths/sibilants as separate editable timeline regions; each region can be adjusted, removed or have boundaries changed. Reference: https://noiseworksaudio.com/blog/voiceassist-automatic-or-manual/
- Steinberg SpectraLayers 13 places waveform overview, spectral display, tool settings, panels, transcript bar, transport, and level metering into a structured editing workspace. Reference: https://www.steinberg.net/spectralayers/ and https://download.steinberg.net/downloads_software/SpectraLayers_12/help/Pro/_user_interface.html
- Waves Vocal Rider is a different approach (automatic level riding, not manual event editing): it uses target, sensitivity, range, rider and output controls, with automation modes. Reference image/source: https://www.waves.com/plugins/vocal-rider

## What this means for Vocal Dynamics
The product should combine the best workflow ideas without copying any vendor's artwork:
1. **Waveform-first interface:** a large central waveform with readable ruler, playhead, zoom and clear time display; avoid a small waveform surrounded by generic cards.
2. **Real event segmentation:** detect and show individual word segments plus separate event categories for sibilance, plosives, breaths and peaks. Never fabricate regions when detection has not run.
3. **Direct manual gain editing:** drag a selected event's gain handle/curve or use a numeric dB field; add and move envelope nodes for phrase-level automation; support undo/redo and reset.
4. **Event inspector:** selected event type, label, start/end, duration, measured peak/RMS/loudness, gain applied, audition/solo, bypass and event navigation.
5. **Detection workflow:** category tabs/filters, event counts, previous/next event, audition in context, accept/reject/edit detection, and an explicit review-before-apply step for automatic peak taming.
6. **Peak control:** scan for transient peaks, propose reductions based on local context, show each proposed gain change visually, allow the engineer to approve/reject individual changes, and preserve musical transients where appropriate.
7. **Breaths and consonants:** independent detectors and gain treatment for breath, sibilance and plosives; don't treat every event as one generic word block.
8. **Useful visualization:** waveform amplitude plus optional spectrogram view; selected event boundaries and gain curve must remain legible at zoom levels.
9. **Professional interaction:** keyboard shortcuts, mouse-wheel zoom, drag-drop, transport sync, clear hover/selection states, responsive/resizable layout, undo/redo, A/B and non-destructive processing.
10. **Honest implementation status:** detection confidence and processing status must be real; don't show fake analysis results. Test on actual Persian and other-language vocals.

## Suggested UI hierarchy
- Header: file/session, undo/redo, transport, current time / total duration, audio device state.
- Main canvas (largest): waveform, time ruler, playhead, gain envelope and event boundaries.
- Event lane: words / sibilants / plosives / breaths / peaks as filterable categories.
- Inspector: selected event's type, start/end, gain dB, peak/RMS, audition, reset and proposed edit controls.
- Analysis strip: BPM/key if analyzed, word transcription status, event counts, confidence and errors.

## Limitations and implementation note
The sources above describe different products and workflows; no single product does all of Vocal Dynamics' proposed functions. IcePick is the closest event-based inspiration for manual vocal artifact control, RX for gain envelopes and precision editing, VoiceAssist for editable detected regions, SpectraLayers for advanced analysis workspace, and Vocal Rider for global level riding. Their features should be treated as design research, not claims that Vocal Dynamics already implements them.
