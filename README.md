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

## Previews

### Keys (MIDI) — piano roll with waterfall, two controllers blending

![Piano roll](docs/images/piano-roll.svg)

*Blue = controller 1 (C-major chord); red = controller 2 (A-minor); purple blend where both play the same note (C4).*

### Drums (MIDI) — Roland TR-808 preset

![TR-808 drum grid](docs/images/drum-808.svg)

*Warm orange hits on near-black wood-panel background. Bass1, Snare, Tom1, and Tom3 are currently triggered.*

### Drums (MIDI) — Novation Launchpad X preset

![Launchpad X drum grid](docs/images/drum-launchpad.svg)

*8×8 rounded pads, purple RGB hits — matching the Launchpad X's native aesthetic.*

### Drums (MIDI) — NI Maschine Studio preset

![Maschine Studio drum grid](docs/images/drum-maschine.svg)

*4×4 rounded pads, deep red hits on near-black panel.*

### CC Lanes (MIDI)

![CC Lanes](docs/images/cc-lanes.svg)

*8 CC lanes (Mod, Breath, Expr, Sustain, Vol, Pan, Filter, Reso) at current values. Sustain (100%) glows cyan; low values render in standard blue.*

### Drums (MIDI) — Roland TR-808 step-sequencer view

![TR-808 step sequencer](docs/images/seq-808.svg)

*Instruments as rows (BD / SD / CP / CH / OH / LT / MT / HT), 16 time steps as columns. The highlighted column (step 9) just fired BD + CP + CH simultaneously. Steps 1–8 show decayed orange traces; steps 10–16 are dark (not yet played this bar). Thin lines separate the four 4-step groups.*

### Drums (MIDI) — Roland TB-303 bassline step-sequencer view

![TB-303 bassline step sequencer](docs/images/seq-tb303.svg)

*12-row chromatic pitch-class display (C through B). Any octave of a note fires the same row — here an A-minor acid figure has built up a scrolling green pattern. Step 9 fired a G (row 7 fully lit); earlier steps show the A → C → D → E → G → A → C ascent fading to the left. Steps 10–16 are idle.*

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

## macOS: Gatekeeper & quarantine

### Why OBS won't load the plugin out of the box

macOS Gatekeeper automatically quarantines every file downloaded from the internet. A quarantined `.plugin` bundle is blocked from loading — OBS will silently skip it, and no source types will appear. This is **not** a bug in the plugin; it is standard macOS security behaviour for unsigned third-party code.

### Quick fix (one-time terminal command)

After copying the bundle to your plugins folder, run:

```bash
xattr -dr com.apple.quarantine \
  ~/Library/Application\ Support/obs-studio/plugins/obs-midi-viz.plugin
```

`-d` removes the attribute, `-r` recurses into the bundle's subdirectories. You only need to do this once per install.

Verify it worked — the following should print nothing:

```bash
xattr ~/Library/Application\ Support/obs-studio/plugins/obs-midi-viz.plugin
```

### Permanent fix — Apple code signing + notarization

The root cause is that the plugin is not signed with an Apple-issued certificate. Until it is, every user who downloads it must run `xattr` manually. To remove that requirement permanently:

| Step | What it involves | Cost |
|---|---|---|
| **Apple Developer account** | Required to obtain any Apple-issued signing certificate | $99 / year |
| **Developer ID Application certificate** | The specific certificate type needed for software distributed outside the Mac App Store | Included with membership |
| **Code-sign the bundle** | `codesign --sign "Developer ID Application: …" --deep --force --options runtime obs-midi-viz.plugin` | Free once enrolled |
| **Notarize with Apple** | Submit the signed bundle to Apple's notarization service via `xcrun notarytool submit`; Apple scans for malware and returns a ticket | Free (a few minutes per submission) |
| **Staple the ticket** | `xcrun stapler staple obs-midi-viz.plugin` — embeds the notarization result so macOS can verify offline | Free |

Once signed, notarized, and stapled, macOS will trust the plugin on any machine without requiring `xattr`. The CI workflow can be updated to perform all of these steps automatically using secrets for the certificate and notarization credentials — let me know when you have a Developer ID certificate and I'll add that to the GitHub Actions build.

---

## Drum Machine Device Presets

Pick a preset from the **Device Preset** dropdown in the source properties.
It auto-fills the grid size, base note, colours, and pad style — you can then
fine-tune any individual field.

Presets marked **(Step Seq)** use the **step-sequencer layout**: instruments as
rows, 16 time steps as columns. The other presets use the standard **pad grid** layout.

### Roland

| Preset | Layout | Grid / Rows | Pad Style | Notable use |
|---|---|---|---|---|
| Roland TR-808 | Pad grid | 4×4 | Square | Trap, hip-hop, 808 bass |
| Roland TR-808 (Step Seq) | Step seq | 8 rows × 16 steps | — | BD/SD/CP/CH/OH/LT/MT/HT |
| Roland TR-909 | Pad grid | 4×4 | Square | House, techno |
| Roland TR-909 (Step Seq) | Step seq | 8 rows × 16 steps | — | BD/SD/RM/CP/CH/OH/LT/HT |
| Roland TR-707 | Pad grid | 4×4 | Square | Pop, early electronic |
| Roland TR-707 (Step Seq) | Step seq | 8 rows × 16 steps | — | BD/SD/RM/CH/OH/LT/MT/CY |
| Roland TR-606 | Pad grid | 2×3 | Square | Acid, TB-303 companion |
| Roland SP-404 | Pad grid | 4×4 | Square | Lo-fi, beat tapes |
| Roland TB-303 (Bassline) | Step seq | 12 rows × 16 steps | — | Chromatic pitch-class acid bass |

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
├── docs/
│   ├── images/                     # SVG mockup images used in this README
│   └── render/                     # gen-mockups.ps1 — regenerate the images
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
29 presets across Roland, Akai MPC, NI Maschine, Novation Launchpad, Elektron,
Arturia, Korg, and hip-hop heritage machines (SP-1200, Oberheim DMX, LinnDrum).
Three pad styles: Square, Rounded, Circle. Separate panel/idle/hit colours per preset.
Selecting a preset auto-fills all grid and colour properties.

#### 4c — Authentic step-sequencer layouts ✅
TR-808, TR-909, and TR-707 in their native **16-step row-per-instrument** view —
instruments as horizontal rows (BD, SD, CP, CH, OH, LT, MT, HT), time steps as
columns; a current-step highlight band scrolls right as notes arrive; group
separator lines mark every 4 steps (one bar).

**Roland TB-303 (Transistor Bass / Bassline)** — 12-row chromatic pitch-class view:
each row is one semitone (C through B), any octave of that pitch fires the same row.
Acid basslines build up a scrolling chromatic pattern in the signature TB-303 green.

Step-seq presets appear in the Device Preset dropdown alongside the existing pad-grid
presets (e.g. "Roland TR-808 (Step Seq)").

Still to come: Launchpad auxiliary button rows; LaunchControl XL knob rings.

#### 4d — DJ deck and controller skins 🔜
Pioneer CDJ/XDJ waveform area visualisation; Rane Seventy-Two EQ section;
LaunchControl bank switching visualisation.

### Phase 5 — CC Lanes renderer 🔜
Actual vertical bar graph per CC lane with animated smoothing, numeric value
overlay, and configurable CC assignments per lane.

### Phase 6 — Polish 🔜
CPack installers (NSIS for Windows, macOS pkg/dmg, Linux .deb); OBS source icons;
code signing + notarization for macOS (eliminates xattr requirement);
per-scene preset save/restore; MIDI channel filtering.

---

## License

MIT
