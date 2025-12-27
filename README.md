# OBS MPV Source Plugin

A powerful, high-performance media source plugin for OBS Studio powered by `libmpv`. This plugin offers superior format support, hardware acceleration, and advanced playback controls compared to the standard VLC or Media Source.

## Key Features

*   **Advanced Format Support**: Leverages the power of `libmpv` to play almost any media format (MKV, MP4, MOV, AVI, etc.).
*   **Playlist Management**: Dedicated dock with a playlist table, drag-and-drop support, and detailed metadata.
*   **Auto-Match OBS FPS**: Automatically switch OBS global frame rate to match the currently playing video for perfectly smooth playback.
*   **Detailed Metadata**: View video FPS and audio channel count directly in the playlist.
*   **Smooth Transitions**: Fixed frame flickering/blackout issues when switching between files in a playlist.
*   **Cross-Platform**: Fully compatible with **Windows**, **macOS**, and **Linux**.
*   **Audio Flexibility**: Professional audio handling via FIFO/Named Pipes for low-latency synchronization.
*   **Subtitle Support**: Full support for internal and external subtitles with advanced styling settings.
*   **Hardware Acceleration**: Built-in support for GPU-accelerated decoding.

## Installation

### Windows
1. Download the latest `.zip` or `.exe` from the [Releases](https://github.com/zapaincoffee/obs-mpv/releases) page.
2. Extract or install to your OBS Studio plugins directory (usually `%ProgramData%\obs-studio\plugins`).

### macOS
1. Download the `.pkg` installer from the [Releases](https://github.com/zapaincoffee/obs-mpv/releases) page.
2. Run the installer and follow the instructions.

### Linux
1. Download the `.deb` package or the source tarball.
2. Install via your package manager: `sudo apt install ./obs-mpv-source.deb`.

## Usage

1. Open OBS Studio.
2. Go to **View -> Docks -> MPV Controls & Playlist** to enable the control panel.
3. Add a new **MPV Source** to your scene.
4. Drag and drop files into the playlist dock and control playback from there.
5. Check **"Auto Match OBS FPS"** in the dock if you want OBS to synchronize its frame rate with your media.

## Building from Source

### Prerequisites
*   CMake (3.28+)
*   Qt 5 or Qt 6
*   libmpv development files
*   libobs development files

### Steps
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

## License
GNU General Public License v2.0 or later.