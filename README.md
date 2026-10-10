# OpenDisplay NX

Use a Nintendo Switch / Switch OLED as an extra display for your Mac.
Independent OpenDisplay receiver for Switch homebrew, built with devkitPro/libnx.

**Current release: v0.1.0.** H.264 hardware decoding, Switch-style
menu, touch input, and English/Russian/Spanish/Simplified Chinese UI.

A Switch OLED user confirmed roughly ten minutes of smooth YouTube playback
with audio through Mac-connected AirPods, working HOME return and corrected
full application shutdown. Physical 720p/60 tests normally sampled 57–60 FPS
on the latest pipeline, but one Wi-Fi run had a severe latency spike.
Stable 60 FPS in every network condition is not established.
See [performance measurements](docs/PERFORMANCE.md) for test conditions.

Download the NRO or installation ZIP from [Releases](https://github.com/IlieBanda/OpenDisplay-NX/releases).

## Install

1. Extract the release archive into the root of your SD card. The executable goes to
   `sd:/switch/opendisplay-nx/opendisplay-nx.nro`.
2. Launch Homebrew Menu through a game using **R** title override, then OpenDisplay NX.
   Full application memory is required.
3. Put Switch and Mac on the same Wi-Fi network (5 GHz recommended).
4. In the [OpenDisplay Mac sender](https://github.com/peetzweg/opendisplay), select
   **Switch OLED**. Start with its **Fast** setting.

The receiver announces a 1280×720 panel. It offers H.264 up to 60 FPS when the
hardware device initializes and up to 30 FPS in software mode. These are
negotiation ceilings. The optional overlay shows one FPS value: distinct video
images submitted to the display per second. Redrawing the same image does not
inflate FPS. A static desktop shows DESKTOP IDLE. DELAY estimates Mac capture to
SDL presentation return, with the same minimum-RTT clock synchronization used by
OpenDisplay. It is not a measured capture-to-photon number. Missing timestamps or
clock synchronization show `--`.
Statistics are hidden by default; open the menu with + and select Statistics. The settings language
can be English, Russian, Spanish or Simplified Chinese. Select Language and
press A to cycle; the choice is saved on the SD card. Both sticks and D-pad
navigate settings.

| Input | Action |
|---|---|
| Plus | Open / close menu |
| Either stick / D-pad Up / Down | Select a menu item |
| A | Confirm selected action |
| B | Return to video |
| Touch | Mac mouse input during video; select menu items |

The menu contains decoder mode, statistics, language, Disconnect and Exit.
X, Y and Minus have no playback shortcuts.

Network reads, heartbeats and H.264 decoding run on a dedicated worker. The UI
consumes only the newest decoded image, so vsync never blocks TCP reads and
superseded images are not copied through VIC/uploaded. Compressed inter-frame
H.264 packets are still decoded in order. SPS/PPS changes rebuild the decoder
and request a new keyframe without ending the connection. GPU/SDL calls stay on
the main thread. The UDP cursor side channel is advertised only after binding,
confirms delivery with cursorAck, and falls back to TCP on unsupported senders.

The connection screen uses the Switch shared system font and follows the console's
light/dark theme. Settings remain available during video by pressing +.
While a Mac is connected, native activity reporting prevents inactivity dimming
and automatic sleep, including on a static desktop. Reporting stops on disconnect.
Pressing HOME keeps the TCP connection and decoder alive in the background; video
presentation pauses until you return, when the latest retained frame is uploaded
even if the desktop has stopped sending updates. Closing the application fully, or exiting
through the Exit menu item, flushes a closing message before closing the socket. Explicit Disconnect
and changing the decoder also end the session; select the receiver on the Mac to
connect again. A lost network or invalid stream can still end a session.

The sender's PNG cursor is drawn with its original hotspot. An arrow appears
until the sprite arrives. Install identity is persisted in `id.txt`, and a fresh
`session.log` is written on every launch (the preceding log is kept as
`session-previous.log`), both in `sd:/switch/opendisplay-nx/`.

## Scope

Wi-Fi IPv4 discovery via Bonjour, TCP on port 9000, OpenDisplay wire protocol v3,
H.264 video, PNG cursor, touch input, hardware decode with software fallback.
Hardware frames are transferred through VIC to aligned NV12 buffers and uploaded
to SDL; this is not a zero-copy presentation pipeline.

No USB transport, sound, HEVC, rotation or controller mouse yet. Handheld 1280×720
layout is used in the dock too. Bonjour is a minimal implementation without full
conflict probing; future discovery work should replace it with a complete responder.
Use on a trusted local network, as the OpenDisplay v3 transport is unauthenticated.

## Build

Install devkitPro with devkitA64, libnx, switch-sdl2 and switch-libpng.

```sh
./tools/build.sh
```

The script fetches and builds the pinned `nvtegra` branch of
[Averne's FFmpeg](https://github.com/averne/FFmpeg/tree/nvtegra) with H.264/NVDEC,
then creates `opendisplay-nx.nro`. `DEVKITPRO` defaults to `/opt/devkitpro`.
Set `REBUILD_FFMPEG=1` to force dependency rebuilding; `JOBS` defaults to 8.

Host validation requires a C++17 compiler, libpng, Python 3 and FFmpeg with libx264:

```sh
./tools/test.sh
python3 tools/test_sender.py SWITCH_IP --seconds 30 --fps 60 --bitrate 6M
```

Tests exercise TCP fragmentation/coalescing, size limits, control/video demux,
DNS-SD records, base64 and PNG cursor decoding, and a real framed H.264 test stream.
With host FFmpeg/SDL2 development headers, tests also execute the actual Video
implementation through 30 software sessions, plus 27 actual TCP worker
sessions, including a quality change, delayed UI, UDP cursor ordering,
clock sync, close, and HOME frame retention under ASan/UBSan.
`VIDEO_SANITIZER=thread python3 tests/test_video.py` runs the concurrency checks. They do not execute the Tegra hardware path; that requires a Switch.

## License and credits

Project code: GPL-3.0 (see [LICENSE](LICENSE)). The selected FFmpeg/NVTEGRA build
is GPL-2.0-or-later and is compatible with this distribution under GPL-3.0.
The vendored stb_truetype rasterizer is MIT; the Switch system font is loaded
from console shared memory and is not redistributed. nlohmann/json v3.12.0 is MIT; libpng and zlib retain their own licenses.

- [OpenDisplay](https://github.com/peetzweg/opendisplay): protocol and Mac sender.
- [Averne/FFmpeg](https://github.com/averne/FFmpeg/tree/nvtegra): Tegra hardware backend.
- [Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch): hardware integration reference.
- [devkitPro/libnx](https://github.com/switchbrew/libnx), SDL2, libpng, nlohmann/json.

Exact dependency revisions and licensing notices are in `tools/` and `LICENSES/`.
