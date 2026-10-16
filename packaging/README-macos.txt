ElukkaSynth v@VERSION@ BETA - macOS (Universal VST3: Apple Silicon + Intel)
==========================================================================

ElukkaSynth is an additive / resynthesis software synthesizer (VST3 instrument).
This is a BETA build for testers. Expect rough edges and please report them.

Requirements
------------
- macOS 11 (Big Sur) or newer.
- Apple Silicon (arm64) or Intel (x86_64) Mac; the binary is universal.
- A 64-bit VST3 host (Ableton Live 10.1+, Reaper, Cubase, Studio One, Bitwig,
  FL Studio, ...). Logic Pro and GarageBand do NOT load VST3; no AU is provided.

Installation
------------
1. Unzip this archive.
2. Copy the whole "ElukkaSynth.vst3" bundle to:
       ~/Library/Audio/Plug-Ins/VST3/            (current user)
   or  /Library/Audio/Plug-Ins/VST3/             (all users, needs admin)
   In Finder: Go > Go to Folder... and paste ~/Library/Audio/Plug-Ins/VST3
3. Remove the download quarantine, otherwise macOS refuses to load the plugin
   because this beta is not notarized with Apple. Open Terminal and run:
       xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/ElukkaSynth.vst3
4. Start your host and rescan plugins. It appears as "ElukkaSynth" by "Elukka".

Presets
-------
User presets and sequencer patterns are saved as plain files in
    ~/Documents/ElukkaSynth/Presets/      (*.elukka)
    ~/Documents/ElukkaSynth/Patterns/     (*.elukkaseq)
Deleting a preset from the plugin moves the file to the Trash.

Resynthesis file formats
------------------------
Audio: WAV, AIFF, FLAC, OGG, MP3 (MP3 through macOS' built-in codecs).
Image: PNG, JPG, GIF.

Known limitations (beta)
------------------------
- Not notarized: see step 3 above. Without it the host reports the plugin as
  damaged or from an unidentified developer.
- VST3 only; no AU and no standalone application.
- CPU-heavy at high unison and partial counts; raise the buffer size if needed.

Troubleshooting
---------------
- Plugin not found: confirm ~/Library/Audio/Plug-Ins/VST3/ElukkaSynth.vst3/Contents/MacOS/ElukkaSynth
  exists, then rescan or clear the plugin cache in your host.
- "cannot be opened" / "damaged" / "unidentified developer": run the xattr
  command from step 3 and rescan.
- Crackles or dropouts: raise your audio buffer size. Note your Mac model and
  buffer size when reporting.

Reporting
---------
Please include: macOS version, Mac model (Apple Silicon or Intel), host and
version, buffer size and sample rate, steps to reproduce.
