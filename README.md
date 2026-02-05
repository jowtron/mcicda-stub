# mcicda-stub

A stub MCI CD Audio driver for Wine that enables CD audio emulation without physical hardware.

## Overview

This is a replacement for Wine's `mcicda.dll` that intercepts CD audio commands and logs them to a file. An external program can then watch this log file and play audio files in response, enabling CD audio support for legacy Windows games running under Wine on systems without CD drives.

## How It Works

1. The game calls MCI functions to play CD audio (e.g., `mciSendCommand(MCI_PLAY, ...)`)
2. Wine routes these calls to `mcicda.dll`
3. Our stub DLL intercepts the commands and writes them to `C:\mcicda_commands.log`
4. An external controller (e.g., Python script) watches the log and plays corresponding audio files

```
┌─────────────┐     ┌──────────────┐     ┌─────────────────────┐
│  Game       │────▶│  mcicda.dll  │────▶│ mcicda_commands.log │
│  (MCI API)  │     │  (stub)      │     │                     │
└─────────────┘     └──────────────┘     └──────────┬──────────┘
                                                    │
                    ┌──────────────┐                │
                    │  Audio       │◀───────────────┘
                    │  Controller  │     (watches file)
                    └──────┬───────┘
                           │
                    ┌──────▼───────┐
                    │  WAV Files   │
                    │  (track02,   │
                    │   track03...)│
                    └──────────────┘
```

## Log File Format

Commands are written one per line to `C:\mcicda_commands.log`:

| Command | Format | Description |
|---------|--------|-------------|
| OPEN | `OPEN` | CD audio device opened |
| CLOSE | `CLOSE` | CD audio device closed |
| PLAY | `PLAY <from> <to>` | Play tracks from-to (e.g., `PLAY 2 5`) |
| STOP | `STOP` | Stop playback |
| PAUSE | `PAUSE` | Pause playback |
| RESUME | `RESUME` | Resume playback |
| SEEK | `SEEK <track>` | Seek to track |

## Stub Behavior

The stub reports:
- 18 audio tracks available
- ~3 minutes per track
- Media always present and ready
- Device type: CD Audio

## Building

### Prerequisites
- CMake 3.10+
- Visual Studio 2022 (or compatible MSVC compiler)
- Windows SDK

### Build Steps

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A Win32
cmake --build . --config Release
```

The built DLL will be in `build/Release/mcicda.dll`.

### GitHub Actions

This repository includes a GitHub Actions workflow that automatically builds the DLL on push. Download the artifact from the Actions tab.

## Installation

1. Copy `mcicda.dll` to your Wine prefix:
   ```bash
   cp mcicda.dll ~/.wine/drive_c/windows/system32/
   ```

2. Set the DLL override to use native:
   ```bash
   export WINEDLLOVERRIDES="mcicda=n"
   ```

3. Run your game with the override:
   ```bash
   WINEDLLOVERRIDES="mcicda=n" wine game.exe
   ```

## Example Controller (Python)

```python
import time
import subprocess
from pathlib import Path

LOG_FILE = Path.home() / ".wine/drive_c/mcicda_commands.log"
MUSIC_DIR = Path("./music")  # Contains track02.wav, track03.wav, etc.

current_player = None

def play_track(track_num):
    global current_player
    stop_playback()
    track_file = MUSIC_DIR / f"track{track_num:02d}.wav"
    if track_file.exists():
        current_player = subprocess.Popen(["afplay", str(track_file)])

def stop_playback():
    global current_player
    if current_player:
        current_player.terminate()
        current_player = None

def watch_log():
    LOG_FILE.parent.mkdir(parents=True, exist_ok=True)
    LOG_FILE.touch()

    with open(LOG_FILE, 'r') as f:
        f.seek(0, 2)  # Seek to end
        while True:
            line = f.readline()
            if line:
                parts = line.strip().split()
                if parts[0] == "PLAY":
                    play_track(int(parts[1]))
                elif parts[0] == "STOP":
                    stop_playback()
            time.sleep(0.1)

if __name__ == "__main__":
    watch_log()
```

## Use Cases

- **Legacy games**: Run DOS/Win16/Win95 games that use Red Book CD audio
- **Preservation**: Archive and play games without original CDs
- **Wine on macOS/Linux**: Systems without CD drive support

## Tested With

- CivNet (Civilization Network) - Windows 3.1/95
- Wine 9.x on macOS (via Homebrew)
- otvdm/winevdm for 16-bit support

## Technical Details

### Exported Functions

- `DriverProc` - Main MCI driver entry point

### Handled MCI Messages

- `MCI_OPEN` / `MCI_OPEN_DRIVER`
- `MCI_CLOSE` / `MCI_CLOSE_DRIVER`
- `MCI_PLAY`
- `MCI_STOP`
- `MCI_PAUSE`
- `MCI_RESUME`
- `MCI_SEEK`
- `MCI_STATUS`
- `MCI_SET`
- `MCI_GETDEVCAPS`
- `MCI_INFO`

### Time Format Support

Supports both `MCI_FORMAT_TMSF` (Track/Minute/Second/Frame) and raw track numbers.

## License

MIT License - See LICENSE file.

## Contributing

Issues and pull requests welcome at https://github.com/jowtron/mcicda-stub
