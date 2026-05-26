# obs-midi-viz

> Real-time MIDI visualizer plugin for OBS Studio — piano roll, drum pads, and CC lanes as independent, composable OBS sources.

[![Build](https://github.com/YOUR_USER/obs-midi-viz/actions/workflows/build.yml/badge.svg)](https://github.com/YOUR_USER/obs-midi-viz/actions/workflows/build.yml)

## Features

| OBS Source | What it shows |
|---|---|
| **MIDI Piano Roll** | Full keyboard with lit keys; configurable range; optional Synthesia-style falling-note waterfall |
| **MIDI Drum Pads** | Configurable grid (4×4, 4×8, 8×8) with velocity-scaled flash; GM drum labels |
| **MIDI CC Lanes** | Per-CC vertical bars with smoothing (mod wheel, expression, sustain, filter, etc.) |

All three sources are independent — place, resize, and show/hide them per scene.

## Installation

Download the latest release for your platform from [Releases](https://github.com/YOUR_USER/obs-midi-viz/releases):

| Platform | File | Install path |
|---|---|---|
| Windows | `obs-midi-viz-windows-x64.zip` | Extract to `%ProgramFiles%\obs-studio\` |
| macOS | `obs-midi-viz-macos-universal.tar.gz` | Extract to `/Applications/OBS.app/Contents/` |
| Linux | `obs-midi-viz-linux-x86_64.tar.gz` | Extract to `/usr/lib/` |

Restart OBS, then add sources via **+ → MIDI Piano Roll / MIDI Drum Pads / MIDI CC Lanes**.

## Building from source

### Prerequisites

**Windows**
```
Visual Studio 2022 (Desktop C++ workload)
CMake 3.24+ (bundled with VS or standalone)
Qt6 (installed via the Qt Online Installer or winget)
```

**macOS**
```
brew install cmake ninja qt@6
Xcode Command Line Tools  (xcode-select --install)
OBS Studio installed to /Applications/
```

**Linux (Ubuntu 22.04)**
```
sudo apt install cmake ninja-build qt6-base-dev libasound2-dev
```

### Build

```bash
git clone https://github.com/YOUR_USER/obs-midi-viz.git
cd obs-midi-viz

cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
cmake --install build --prefix dist
```

## Project structure

```
obs-midi-viz/
├── .github/
│   └── workflows/
│       └── build.yml          # CI matrix: Windows / macOS / Linux
├── cmake/                     # Custom CMake finders
├── src/
│   ├── plugin-main.cpp        # obs_module_load — registers all sources
│   ├── plugin-support.h       # Logging macros
│   ├── midi-engine.hpp/.cpp   # Thread-safe RtMidi wrapper + event queue
│   └── sources/
│       ├── piano-source.*     # MIDI Piano Roll
│       ├── drum-source.*      # MIDI Drum Pads
│       └── cc-source.*        # MIDI CC Lanes
└── data/
    └── locale/
        └── en-US.ini          # OBS Properties panel strings
```

## Development roadmap

- [x] Phase 1 — Scaffolding, CI, hello-world sources
- [x] Phase 2 — MIDI engine (RtMidi, thread-safe queue, device selection)
- [ ] Phase 3 — Piano renderer (white/black keys, velocity colour, waterfall)
- [ ] Phase 4 — Drum pad renderer (grid, flash decay, velocity brightness)
- [ ] Phase 5 — CC bar renderer (lanes, labels, smoothing)
- [ ] Phase 6 — Polish (config presets, CPack installers)

## License

MIT
