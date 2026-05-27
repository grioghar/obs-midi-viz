# obs-midi-viz

> Real-time MIDI visualizer plugin for OBS Studio — piano roll, drum pads, and CC lanes as independent, composable OBS sources.

[![Build](https://github.com/grioghar/obs-midi-viz/actions/workflows/build.yml/badge.svg)](https://github.com/grioghar/obs-midi-viz/actions/workflows/build.yml)

---

## Sources

| OBS Source | What it shows |
|---|---|
| **Keys (MIDI)** | Full 128-key piano roll with velocity-coloured keys; up to 4 independent MIDI controllers, each with its own colour, blending onto a single roll; Synthesia-style falling-note waterfall that fills the canvas with a configurable keyboard height strip at the bottom |
| **Drums (MIDI)** | Pad grid (any size up to 8×8) with velocity-scaled flash decay; 25 device presets (TR-808, MPC 3000, Maschine, Launchpad, and more); three pad styles (Square, Rounded, Circle); GM drum name labels drawn with a GPU-side 3×5 bitmap font |
| **CC Lanes (MIDI)** | Per-CC vertical bars with configurable smoothing for mod wheel, expression, sustain, filter, and any other CC number |

All three sources are independent — add, resize, reorder, and show/hide them per scene.

---

## Installation

Download the latest build artifacts from the [Actions tab](https://github.com/grioghar/obs-midi-viz/actions) (click the most recent green run → **Artifacts**).

| Platform | File | Install path |
|---|---|---|
| **Windows** | `obs-midi-viz-windows-x64.zip` | Extract into `%ProgramFiles%\obs-studio\` (so `obs-plugins\64bit\obs-midi-viz.dll` lands in the right place) |
| **macOS (Apple Silicon)** | `obs-midi-viz-macos-arm64.tar.gz` | Extract, then copy `obs-midi-viz.plugin` → `~/Library/Application Support/obs-studio/plugins/` |
| **Linux (x86-64)** | `obs-midi-viz-linux-x86_64.tar.gz` | Extract, then copy `obs-midi-viz/` → `~/.config/obs-studio/plugins/` |

Restart OBS, then add sources via **+** → **Keys (MIDI)** / **Drums (MIDI)** / **CC Lanes (MIDI)**.

---

## Drum Machine Device Presets

Pick a preset from the **Device Preset** dropdown in the source properties.
It auto-fills the grid size, base note, colours, and pad style — you can then
fine-tune any individual field.

### Roland

| Preset | Grid | Pad Style | Notable use |
|---|---|---|---|
| Roland TR-808 | 4×4 | Square | Trap, hip-hop, 808 bass |
| Roland TR-909 | 4×4 | Square | House, techno |
| Roland TR-707 | 4×4 | Square | Pop, early electronic |
| Roland TR-606 | 2×3 | Square | Acid, TB-303 companion |
| Roland SP-404 | 4×4 | Square | Lo-fi, beat tapes |

### Akai MPC

| Preset | Grid | Pad Style | Notable use |
|---|---|---|---|
| Akai MPC 60 | 4×4 | Rounded | Golden-era hip-hop (Roger Linn design) |
| Akai MPC 3000 | 4×4 | Rounded | Dre, Premier, J Dilla |
| Akai MPC Live | 4×4 | Rounded | Modern standalone production |

### Native Instruments Maschine

| Preset | Grid | Pad Style | Notes |
|---|---|---|---|
| NI Maschine Studio | 4×4 | Rounded | Flagship; dual-display body |
| NI Maschine Mk3 | 4×4 | Rounded | Current standard |
| NI Maschine Mikro | 4×4 | Rounded | Compact |

### Hip-Hop Heritage

| Preset | Grid | Pad Style | Notable use |
|---|---|---|---|
| E-mu SP-1200 | 4×2 | Square | Golden-age NY hip-hop; punchy filtered drums |
| Oberheim DMX | 4×2 | Square | Run-DMC, LL Cool J |
| LinnDrum | 4×2 | Square | Prince, Human League |

### Elektron

| Preset | Grid | Pad Style | Notes |
|---|---|---|---|
| Elektron Digitakt | 4×4 | Square | Digital sampler/sequencer |
| Elektron Analog Rytm | 4×3 | Square | Analog + acoustic, 12 voices |

### Novation Launchpad

| Preset | Grid | Pad Style | Notes |
|---|---|---|---|
| Novation Launchpad | 8×8 | Square | Original; green pads; live clip launching |
| Novation Launchpad Mini | 8×8 | Square | Compact |
| Novation Launchpad X | 8×8 | Rounded | RGB velocity-sensitive |
| Novation Launchpad Pro | 8×8 | Rounded | RGB + aftertouch |

### Arturia & Korg

| Preset | Grid | Pad Style | Notes |
|---|---|---|---|
| Arturia BeatStep Pro | 8×2 | Circle | 16-step sequencer rows |
| Arturia DrumBrute | 4×4 | Circle | Analog drum synth |
| Korg Volca Beats | 4×4 | Circle | Lo-fi analog; yellow accent |

---

## Multi-Controller Piano Roll

The **Keys (MIDI)** source supports up to **4 simultaneous MIDI controllers**, each
assigned its own colour. Notes from different controllers blend on the same
keyboard and waterfall — useful for showing two players on separate keyboards,
or layering a keyboard with a pad controller.

Properties: **Slot 0–3 Port** and **Slot 0–3 Colour**.

---

## Building from source

### Prerequisites

**All platforms**
- CMake 3.24+
- A C++17 compiler (MSVC 2022 / Clang / GCC)
- OBS Studio (for headers and `libobs`)
- Internet access for the first build (RtMidi is fetched via `FetchContent`)

**Windows**
```
Visual Studio 2022 with "Desktop development with C++" workload
```

**macOS**
```
brew install cmake ninja
Xcode Command Line Tools  (xcode-select --install)
OBS Studio installed to /Applications/
```

**Linux (Ubuntu 22.04)**
```
sudo add-apt-repository ppa:obsproject/obs-studio
sudo apt update
sudo apt install cmake ninja-build libobs-dev libasound2-dev
```

### Build

```bash
git clone https://github.com/grioghar/obs-midi-viz.git
cd obs-midi-viz

cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
cmake --install build --prefix dist
```

The `dist/` directory will contain the platform-appropriate layout ready to copy into OBS.

---

## Project structure

```
obs-midi-viz/
├── .github/workflows/build.yml     # CI matrix: Linux / macOS / Windows
├── src/
│   ├── plugin-main.cpp             # obs_module_load — registers all sources
│   ├── plugin-support.h            # Logging macros (MIDI_LOG_INFO / ERR)
│   ├── midi-engine.hpp/.cpp        # Thread-safe RtMidi wrapper + event queue
│   └── sources/
│       ├── piano-source.*          # Keys (MIDI) — piano roll + waterfall
│       ├── drum-source.*           # Drums (MIDI) — pad grid + device presets
│       └── cc-source.*             # CC Lanes (MIDI) — bar graph
└── data/locale/en-US.ini           # OBS Properties panel strings
```

---

## Development roadmap

### Phase 1 — Scaffolding ✅
CMake project, GitHub Actions CI (Linux / macOS / Windows), hello-world OBS sources.

### Phase 2 — MIDI engine ✅
RtMidi integration; thread-safe event queue; multi-port support (one `RtMidiIn`
per device, all open simultaneously); MIDI device selector in every source.

### Phase 3 — Piano Roll renderer ✅
- White/black key drawing with velocity-coloured flash and decay
- Up to 4 simultaneous MIDI controllers with per-controller colours, blending onto a single roll
- Synthesia-style falling-note waterfall; configurable keyboard height so the roll can fill any canvas size

### Phase 4 — Drum Pad renderer ✅ (ongoing)

#### 4a — Core grid renderer ✅
GPU-side pad grid with velocity flash; 3×5 bitmap font labels (GM drum names or note numbers); proportional gaps; luminance-adaptive text colour.

#### 4b — Device preset system ✅
25 presets across Roland, Akai MPC, NI Maschine, Novation Launchpad, Elektron,
Arturia, Korg, and hip-hop heritage machines (SP-1200, Oberheim DMX, LinnDrum).
Three pad styles: Square, Rounded, Circle. Separate panel/idle/hit colours per preset.
Selecting a preset auto-fills all grid and colour properties.

#### 4c — Authentic step-sequencer layouts 🔜
TR-808/909/707 in their native 16-step row-per-instrument view (not a 4×4 grid);
Launchpad top/right auxiliary button rows; LaunchControl XL with knob rings.

#### 4d — DJ deck and controller skins 🔜
Pioneer CDJ/XDJ waveform area visualisation; Rane Seventy-Two EQ section;
LaunchControl bank switching visualisation.

### Phase 5 — CC Lanes renderer 🔜
Actual vertical bar graph per CC lane with animated smoothing, numeric value
overlay, and configurable CC assignments per lane.

### Phase 6 — Polish 🔜
CPack installers (NSIS for Windows, macOS pkg/dmg); OBS source icons;
per-scene preset save/restore; MIDI channel filtering.

---

## License

MIT
