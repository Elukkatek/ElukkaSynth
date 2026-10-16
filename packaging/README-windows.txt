ElukkaSynth v@VERSION@ BETA - Windows (64-bit VST3)
====================================================

ElukkaSynth is an additive / resynthesis software synthesizer (VST3 instrument).
This is a BETA build for testers. Expect rough edges and please report them.

Requirements
------------
- Windows 10 or 11, 64-bit (x64). No ARM64 build yet.
- A 64-bit VST3 host (Reaper, Ableton Live 11+, Cubase, Studio One, Bitwig, FL Studio, ...).
- No separate runtime installation is needed; the plugin is self-contained.

Installation
------------
1. Unzip this archive.
2. Copy the whole "ElukkaSynth.vst3" FOLDER (it is a bundle, not a single file) to:
       C:\Program Files\Common Files\VST3\
   You may need to confirm an administrator prompt.
3. Start your host and rescan plugins if it does not pick the synth up automatically.
   It appears as "ElukkaSynth" by "Elukka" under Instruments.

Presets
-------
User presets and sequencer patterns are saved as plain files in
    Documents\ElukkaSynth\Presets\      (*.elukka)
    Documents\ElukkaSynth\Patterns\     (*.elukkaseq)
Deleting a preset from the plugin moves the file to the Recycle Bin.

Resynthesis file formats
------------------------
Audio: WAV, AIFF, FLAC, OGG, MP3 (MP3 through Windows' built-in codecs).
Image: PNG, JPG, GIF.

Known limitations (beta)
------------------------
- Windows builds were compiled and unit-tested on a build server, but this
  release has NOT yet been exercised by hand inside a Windows DAW. UI, MIDI,
  automation, presets and audio are expected to behave as on macOS; please report
  anything that does not.
- Only x64 is provided (no 32-bit, no ARM64).
- VST3 only; no standalone application.

Troubleshooting
---------------
- Plugin not found: make sure you copied the ElukkaSynth.vst3 folder itself into
  the VST3 folder (so that C:\Program Files\Common Files\VST3\ElukkaSynth.vst3\Contents\
  exists), then rescan or clear the plugin cache in your host.
- Host says the plugin failed to load / is blocklisted: remove it from the host's
  blocklist and rescan. If it keeps failing, tell us your Windows version and host.
- Crackles or dropouts: raise your audio buffer size; ElukkaSynth is CPU-heavy at
  high unison and partial counts. Note your CPU and buffer size when reporting.
- Presets missing: check that Documents\ElukkaSynth\Presets exists and is writable.

Reporting
---------
Please include: Windows version, host and version, buffer size and sample rate,
steps to reproduce, and whether the problem is repeatable.
