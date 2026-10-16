ElukkaSynth v@VERSION@ BETA - Linux (x86-64 VST3)
=================================================

ElukkaSynth is an additive / resynthesis software synthesizer (VST3 instrument).
This is a BETA build for testers. Expect rough edges and please report them.

Requirements
------------
- x86-64 Linux with glibc 2.35 or newer (Ubuntu 22.04+, Debian 12+, Fedora 36+,
  Arch, etc.). The build was made on Ubuntu 22.04.
- A VST3 host: Reaper, Bitwig Studio, Ardour 7+, Carla, Qtractor, ...
- Runtime libraries (normally already present on any desktop system):
    libasound2  libfreetype6  libfontconfig1  libx11-6  libxext6  libxrandr2
    libxinerama1  libxcursor1  libgl1
  Debian/Ubuntu:  sudo apt install libasound2 libfreetype6 libfontconfig1 \
                      libx11-6 libxext6 libxrandr2 libxinerama1 libxcursor1 libgl1
  Fedora:         sudo dnf install alsa-lib freetype fontconfig libX11 libXext \
                      libXrandr libXinerama libXcursor mesa-libGL

Installation
------------
1. Unzip this archive.
2. Copy the whole "ElukkaSynth.vst3" DIRECTORY (it is a bundle) to one of:
       ~/.vst3/                 (current user only, no root needed)
       /usr/lib/vst3/           (all users)
       /usr/local/lib/vst3/
   e.g.   mkdir -p ~/.vst3 && cp -r ElukkaSynth.vst3 ~/.vst3/
3. Rescan plugins in your host. It appears as "ElukkaSynth" by "Elukka".

Presets
-------
User presets and sequencer patterns are saved as plain files in
    ~/Documents/ElukkaSynth/Presets/      (*.elukka)
    ~/Documents/ElukkaSynth/Patterns/     (*.elukkaseq)
(If your Documents folder lives elsewhere per XDG settings, that folder is used.)
Deleting a preset from the plugin moves the file to the freedesktop Trash.

Resynthesis file formats
------------------------
Audio: WAV, AIFF, FLAC, OGG, MP3 (MP3 via JUCE's built-in decoder).
Image: PNG, JPG, GIF.

Known limitations (beta)
------------------------
- Linux builds were compiled and unit-tested on a build server, but this release
  has NOT yet been exercised by hand inside a Linux DAW. Please report anything
  that misbehaves.
- X11 only; under Wayland your host runs the plugin through XWayland, which is
  normally fine.
- Only x86-64 is provided (no aarch64).
- VST3 only; no standalone application.

Troubleshooting
---------------
- Plugin not found: confirm ~/.vst3/ElukkaSynth.vst3/Contents/x86_64-linux/ElukkaSynth.so
  exists, then rescan / clear the plugin cache in your host.
- "cannot open shared object file": a runtime library above is missing. Run
      ldd ~/.vst3/ElukkaSynth.vst3/Contents/x86_64-linux/ElukkaSynth.so | grep "not found"
  and install the package that provides it.
- "version `GLIBC_2.xx' not found": your distribution is older than the build
  target. Use a newer distro or build from source (see the project's BUILDING.md).
- Crackles or dropouts: raise the host buffer size; ElukkaSynth is CPU-heavy at
  high unison and partial counts.
- Blank or tiny UI: check your host's plugin-window scaling options (HiDPI).

Reporting
---------
Please include: distribution and version, host and version, buffer size and
sample rate, steps to reproduce, and the output of the ldd command above if the
plugin fails to load.
