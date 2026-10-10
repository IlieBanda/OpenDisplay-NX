<p align="center">
  <img src="https://raw.githubusercontent.com/IlieBanda/OpenDisplay-NX/main/assets/opendisplay-nx-neon-preview.png" alt="OpenDisplay NX icon" width="144">
</p>

<h1 align="center">OpenDisplay NX 0.1.0</h1>

<p align="center">Your Nintendo Switch, now a second display for your Mac.</p>

The first public release of OpenDisplay NX: a free, open-source Switch homebrew receiver for the [OpenDisplay Mac sender](https://github.com/peetzweg/opendisplay).

Works directly with the [official OpenDisplay Sender for Mac](https://github.com/peetzweg/opendisplay/releases/latest) through the native OpenDisplay protocol. No modified sender or additional bridge is needed. Select your Switch from the sender’s device list to connect.

## What's included

- 720p H.264 video with Tegra NVDEC hardware decoding and software fallback.
- Mac cursor rendering and touch input.
- Switch-style menus with system fonts, native button symbols and light/dark themes.
- English, Russian, Spanish and Simplified Chinese.
- Screen stays awake while connected, including when the desktop is static.
- HOME preserves the connection; fully closing the application disconnects the Mac.
- Optional FPS and estimated-delay statistics, available from the **+** menu.

## Get started

1. Download **OpenDisplay-NX-v0.1.0.zip** below and extract it to the root of your SD card.
2. Hold **R** while launching a game to open Homebrew Menu with full application memory, then launch **OpenDisplay NX**.
3. Connect the Switch and Mac to the same Wi-Fi network. **5 GHz** is recommended.
4. In the OpenDisplay Mac sender, select your Switch from the device list and start with **Fast** quality.

You can also install the standalone **opendisplay-nx.nro** in `sd:/switch/opendisplay-nx/`.

Press **+** for the menu, navigate with either stick or the D-pad, confirm with **A** and return with **B**. Disconnect and Exit are menu actions.

## Before you download

The receiver targets the original Nintendo Switch family: **Switch V1/V2, Switch Lite and Switch OLED**. It does not require an OLED panel or detachable Joy-Con. All three model families use a 1280 × 720 handheld display ([Nintendo comparison](https://www.nintendo.com/en-gb/Hardware/Nintendo-Switch-Family/Which-Nintendo-Switch-is-right-for-you-/Which-Nintendo-Switch-is-right-for-you-1596110.html)).

| Model | Validation status |
|---|---|
| Switch OLED | Tested on hardware |
| Switch V1/V2 | Expected to work; not yet tested on hardware |
| Switch Lite | Expected to work; not yet tested on hardware |

A console capable of running homebrew with full application memory is required. Performance on V1/V2 and Lite still needs device testing.

Hardware decoding supports up to 60 FPS; software decoding supports up to 30 FPS. Actual performance depends on your Mac and network. Switch OLED testing included smooth extended video playback, HOME return and a check that the screen stayed bright without controller input. See [performance notes](https://github.com/IlieBanda/OpenDisplay-NX/blob/main/docs/PERFORMANCE.md) for measured results and limitations.

Audio remains on the Mac. This release supports Wi-Fi IPv4; USB, HEVC, rotation and controller mouse input are not available yet. Docked mode uses the handheld 720p layout. Use a trusted local network.

## Downloads

| File | Contents |
|---|---|
| OpenDisplay-NX-v0.1.0.zip | Ready-to-install SD card folder and license notices |
| opendisplay-nx.nro | Standalone application |
| OpenDisplay-NX-v0.1.0-sources.zip | Release sources, including the pinned FFmpeg/NVTEGRA dependency |
| OpenDisplay-NX-v0.1.0-SHA256SUMS.txt | SHA-256 checksums for the three files above |

Licensed under GPL-3.0. Independent project; not an official OpenDisplay or Nintendo application.
