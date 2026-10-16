# Research: Professional Manual Vocal Clip-Gain and Vocal-Event Editing

Research date: 2026-10-16

This research informs the Vocal Dynamics design. References are for workflow study, not for copying another product's visual assets.

## 1. Arctic Labs Audio — IcePick
Official site: https://arcticlabsaudio.com/

IcePick is an event-based vocal de-esser. It detects separate sibilance, hard-consonant/plosive and breath events; supports context-aware Tame Peaks; allows auditioning/replacing matching events with Swap Mode; and includes Formant Mode for targeted formant shaping. The product describes each event as individually reviewable/editable. The site lists VST3/AU/AAX and macOS support; it currently says Windows is TBD.

Design takeaway: an event is a first-class object with type, timing, selected state, proposed action, amount of gain/reduction, audition, and accept/undo. Avoid a generic row of buttons with no visible relationship to the audio.

## 2. Avid Pro Tools — Clip Gain Line
Official edit-window overview: https://www.avid.com/pro-tools/user-guide/edit-window
Reference guide: https://resources.avid.com/SupportFiles/PT/Pro_Tools_Reference_Guide_12.8.2.pdf

Pro Tools supports clip-attached gain, a visible gain line with breakpoint nodes, bypass/clear/render commands, and edits that remain attached to the clip rather than the track automation. Breakpoints allow gain to change inside a clip without splitting the clip into many pieces.

Design takeaway: show an editable gain envelope over the waveform with draggable nodes, numeric dB feedback, clear/reset/bypass actions, and optional render. The user must be able to see the shape of the edit, not only turn a knob.

## 3. iZotope RX — Clip Gain Envelope
Reference: https://downloads.izotope.com/docs/rx6/11-application-menus/index.html

RX's Clip Gain envelope supports fade-in/fade-out, adding nodes, adjusting individual nodes, and adjusting an envelope selection. Nodes can be added by modifier-clicking in the editor or clicking the envelope.

Design takeaway: support free-form local gain envelopes, not just one static gain value per detected word. Add smooth transitions around edits to avoid clicks and abrupt level changes.

## 4. Steinberg SpectraLayers 12 — Editing workspace
Official interface guide: https://download.steinberg.net/downloads_software/SpectraLayers_12/help/Pro/_user_interface.html

The workspace separates tools and tool settings, overview/timeline, waveform and spectral displays, panels, transcript bar, and transport area. It offers time/frequency selection and localized spectral editing.

Design takeaway: keep the waveform/timeline central, put detailed controls in a contextual inspector, and let analysis/transcription panels be shown or hidden instead of squeezing every control into the main lane.

## 5. Waves Vocal Rider — Automatic riding with manual refinement
Official product page: https://www.waves.com/plugins/vocal-rider
User guide: https://www.waves.com/1lib/pdf/plugins/vocal-rider.pdf

Vocal Rider adjusts gain toward a target level in real time and can write its rides to an automation track for fine-tuning. It is an automatic leveling approach rather than manual word-by-word event editing.

Design takeaway: automatic leveling should produce editable gain moves that can be reviewed and refined, rather than silently committing opaque processing.

## Product requirements for Vocal Dynamics
1. A large waveform timeline with a clear time ruler and synchronized playhead.
2. Visible gain envelope/curve over the waveform; draggable breakpoint handles with dB readout and smooth ramps.
3. Real detected word events and separately classified sibilance, plosive/hard-consonant, breath and peak events when corresponding detectors are implemented.
4. Select, audition in context, bypass, reset, undo/redo, and apply/accept proposed edits.
5. Per-event inspector: event type, start/end time, gain/reduction, before/after level, confidence, and navigation to previous/next event.
6. Tame Peaks/automatic leveling as proposals with adjustable strength and explicit approval.
7. Optional advanced tools such as event replacement and formant processing are later-stage features, not decorative buttons.
8. All edits must be audible during playback and match offline WAV export; edits must have non-destructive state and reliable undo.
9. Use original visual design; learn from hierarchy and interaction patterns, not copied branding or assets.
10. Do not claim feature parity with the references until each feature has a working engine and has been tested on real vocals.
