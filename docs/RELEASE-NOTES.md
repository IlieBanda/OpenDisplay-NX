# OpenDisplay NX v0.1.0

First public release: turn a Nintendo Switch / Switch OLED into an extra display for your Mac over Wi-Fi.

## Install

Download `OpenDisplay-NX-v0.1.0.zip` and extract it to the SD card root, or place the standalone `opendisplay-nx.nro` in `sd:/switch/opendisplay-nx/`.
Launch Homebrew Menu using **R** title override on a game, then OpenDisplay NX. Full application memory is required.
Connect Mac and Switch to the same Wi-Fi network (5 GHz recommended). In the [OpenDisplay Mac sender](https://github.com/peetzweg/opendisplay), select **Switch OLED** and start with **Fast** quality.

## Features

- 720p H.264 through Tegra NVDEC, with software fallback and decoder recovery.
- Separate network/decoder worker and latest-frame rendering.
- Mac PNG cursor, UDP cursor updates and touch mouse input.
- Switch-style menu, console system fonts/button symbols and light/dark theme.
- English, Russian, Spanish and Simplified Chinese; saved language choice.
- Prevent inactivity dimming and automatic sleep while connected.
- HOME retains the connection; full close sends a goodbye to the Mac.
- Optional FPS/estimated-delay statistics and diagnostic logs.

**Plus** opens the menu, either stick or D-pad selects, **A** confirms and **B** returns to video. Decoder mode, statistics, language, Disconnect and Exit are in the menu. X/Y/Minus have no playback shortcuts.

## Validation and limits

Host ASan/UBSan tests cover framing, cursor decoding, shutdown, actual decoder/socket sessions, resolution changes, heartbeats during a stalled UI, UDP ordering and retained frames. Font preview checks cover all four translation catalogs and required button glyphs.

A Switch OLED user confirmed roughly ten minutes of smooth live YouTube playback, comfortable lip synchronization with audio through Mac-connected AirPods, HOME return and corrected full shutdown. These are user observations, not measured capture-to-photon latency.

Continuous 720p/60 tests normally sampled 57–60 FPS. One run also showed a severe network latency spike; stable 60 FPS in every condition is not established. Timestamp delays from prerecorded test patterns exclude live Mac capture/encoding. See [performance notes](https://github.com/IlieBanda/OpenDisplay-NX/blob/main/docs/PERFORMANCE.md).

Wi-Fi IPv4 only. Audio remains on the Mac. No USB, HEVC, rotation or controller mouse; docked mode uses the handheld 1280×720 layout. Hardware presentation uses VIC transfer and SDL texture upload.

Installation and source archives include license notices. The source archive includes the pinned patched FFmpeg/NVTEGRA dependency. SHA-256 sums cover the NRO and both archives. This is an independent project, not an official OpenDisplay or Nintendo app.
