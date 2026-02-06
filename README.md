# mcicda-stub

A drop-in replacement for Wine's `mcicda.dll` that plays audio files instead of requiring a physical CD drive. Enables CD audio for legacy Windows games running under Wine on macOS and Linux.

## How It Works

Games call the Windows MCI CD Audio API to play music. This DLL intercepts those calls and plays audio files from `C:\music\` using the waveOut API. No external controller or helper program needed -- the DLL handles everything internally.

```
Game (MCI API)  --->  mcicda.dll (this)  --->  waveOut  --->  Audio output
                           |
                      Decodes from:
                      WAV, FLAC, MP3, OGG
                           |
                      C:\music\track02.wav
                      C:\music\track03.flac
                      C:\music\track04.mp3
                      etc.
```

## Supported Formats

| Format | Extension | Decoder |
|--------|-----------|---------|
| WAV | `.wav` | [dr_wav](https://github.com/mackron/dr_libs) |
| FLAC | `.flac` | [dr_flac](https://github.com/mackron/dr_libs) |
| MP3 | `.mp3` | [dr_mp3](https://github.com/mackron/dr_libs) |
| OGG Vorbis | `.ogg` | [stb_vorbis](https://github.com/nothings/stb) |

All decoders are public domain single-header libraries compiled directly into the DLL. No external dependencies.

For each track, the DLL searches for files in priority order: `.wav`, `.flac`, `.mp3`, `.ogg`. You can mix formats -- e.g. `track02.flac` and `track03.mp3` in the same directory.

## Installation

### 1. Get the DLL

Download `mcicda.dll` from the [GitHub Actions](https://github.com/jowtron/mcicda-stub/actions) build artifacts, or build it yourself (see below).

### 2. Place audio files

Put your audio tracks in `C:\music\` inside the Wine prefix:

```
~/.wine/drive_c/music/track02.wav
~/.wine/drive_c/music/track03.flac
~/.wine/drive_c/music/track04.mp3
...
```

Track numbering starts at 02 (track 01 is traditionally the data track on a game CD).

### 3. Install the DLL

Copy to **both** locations in your Wine prefix:

```bash
cp mcicda.dll ~/.wine/drive_c/windows/system32/mcicda.dll
cp mcicda.dll ~/.wine/drive_c/windows/syswow64/mcicda.dll
```

**Important:** Wine loads 32-bit DLLs from `syswow64`, not `system32`. You must install in both locations.

### 4. Run the game

```bash
# Kill any cached wineserver first (required when swapping DLLs)
WINEPREFIX=~/.wine wineserver -k

# Run with native DLL override
WINEDLLOVERRIDES="mcicda=n" wine game.exe
```

The `mcicda=n` override tells Wine to use our native DLL instead of its built-in one.

## Building

### Prerequisites
- CMake 3.10+
- Visual Studio 2022 (or compatible MSVC compiler)

### Build Steps

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

The built DLL will be in `build/Release/mcicda.dll`.

### GitHub Actions

This repo includes a GitHub Actions workflow that builds on every push. Download the artifact from the [Actions tab](https://github.com/jowtron/mcicda-stub/actions).

## Debugging

The DLL logs all commands and playback events to `C:\mcicda_commands.log`. Check this file to diagnose issues:

```
OPEN (17 tracks)
PLAY 2 (C:\music\track02.flac)
Decoded FLAC: 2ch 44100Hz, 7654321 frames
PCM: 2ch 44100Hz 16bit 30617284 bytes
PLAYING
PLAYBACK_DONE
STOP
```

## Tested With

- CivNet (Civilization Network) -- Windows 3.1/95 via otvdm/winevdm
- Wine 9.x on macOS (Homebrew)

## Technical Details

### Exported Function
- `DriverProc` -- Main MCI driver entry point

### Handled MCI Messages
MCI_OPEN, MCI_CLOSE, MCI_PLAY, MCI_STOP, MCI_PAUSE, MCI_RESUME, MCI_SEEK, MCI_STATUS, MCI_SET, MCI_GETDEVCAPS, MCI_INFO

### Audio Pipeline
1. Game sends MCI_PLAY with track number
2. DLL searches `C:\music\trackNN.{wav,flac,mp3,ogg}`
3. Matched file is decoded to 16-bit PCM in memory using the appropriate decoder
4. PCM data is played via waveOut API (dynamically loaded from winmm.dll)

## License

MIT License -- See LICENSE file.

Audio decoder libraries are public domain (dr_libs, stb_vorbis).

## Contributing

Issues and pull requests welcome at https://github.com/jowtron/mcicda-stub
