# 🎬 OBS MPV Source Plugin

**A powerful, high-performance media source plugin for OBS Studio powered by `libmpv`.**

![Build Status](https://img.shields.io/github/actions/workflow/status/zapaincoffee/obs-mpv/push.yaml?style=flat-square)
![Release](https://img.shields.io/github/v/release/zapaincoffee/obs-mpv?style=flat-square&color=blue)
![License](https://img.shields.io/github/license/zapaincoffee/obs-mpv?style=flat-square)

This plugin replaces the standard Media Source with a robust, feature-rich alternative. It supports almost any media format, handles complex playlists, ensures perfect A/V sync, and gives you professional control over playback.

---

## ✨ What's New in v0.2.0?

This release brings a complete overhaul of the control interface and playlist management:

*   **🎛️ Redesigned Control Dock**: New buttons for **Play**, **Pause**, **Stop**, and **Restart** (Play from Beginning).
*   **🌫️ Smart Transitions**: dedicated **"Restart with Fade"** button and **"Play with Fade"** toggle for seamless live production.
*   **⏱️ Precision Timers**: Real-time display of **Current Time**, **Time Remaining** (current track), and **Total Playlist Duration**.
*   **🎨 Subtitle Styling**: A new settings dialog to customize **Font**, **Color**, **Shadows**, and **Size** of internal/external subtitles.
*   **📊 Enhanced Playlist View**: Now displays **FPS**, **Audio Channels**, and **Loop Count** for every file. The currently playing track is visually highlighted.
*   **🛠️ Under-the-hood Fixes**: Improved Windows pipe handling and Linux compatibility.

---

## 🚀 Key Features

*   **Format Support**: Plays MKV, MP4, MOV, AVI, WEBM, and virtually anything `libmpv` can handle.
*   **Hardware Acceleration**: Uses GPU decoding for low CPU usage.
*   **Advanced Audio**: Audio is handled via FIFO/Named Pipes for low-latency synchronization.
*   **Auto-Match OBS FPS**: Option to automatically switch OBS frame rate to match the video source.
*   **Gapless Playback**: Fixed frame flickering/blackout issues when switching files.
*   **Drag & Drop**: Easily add files to the playlist by dragging them into the dock.

---

## 📥 Installation

### Windows 🪟
1.  Download the latest `.zip` or `.exe` from the [Releases Page](https://github.com/zapaincoffee/obs-mpv/releases).
2.  Extract or run the installer.
3.  Restart OBS Studio.

### macOS 🍏
1.  Download the `.pkg` installer from the [Releases Page](https://github.com/zapaincoffee/obs-mpv/releases).
2.  Run the installer (Right-click -> Open if prompted by security).
3.  Restart OBS Studio.

### Linux 🐧
1.  Download the `.deb` package.
2.  Install: `sudo apt install ./obs-mpv-source.deb`
3.  Restart OBS Studio.

---

## 🎮 Usage Guide

### 1. The Source
Add a new **"MPV Source"** to your scene. You can set a default file or leave it empty and use the playlist.

### 2. The Control Dock
Go to **View -> Docks -> MPV Controls & Playlist**.
*   **Target**: Select which MPV source to control.
*   **Controls**: Use the sliders for Seek and Volume. Use the buttons for transport control.
*   **Fades**: Check "Fade In" or "Fade Out" and set the duration (in seconds) to automatically fade audio/video.
*   **Subtitle Settings**: Click to open the styling editor.

### 3. The Playlist
*   **Add Files**: Click "Add" or Drag & Drop files into the table.
*   **Reorder**: Use Up/Down buttons or Drag & Drop rows.
*   **Loop**: Set specific loop counts for individual items (0 = Once, -1 = Infinite).

---

## 🏗️ Building from Source

**Requirements:** `CMake` (3.28+), `Qt6`, `libmpv`, `libobs`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

---

## ✍️ Credits & Signature

Developed with passion for the open-source community.

**Open Stream via Gemini** 🤖
