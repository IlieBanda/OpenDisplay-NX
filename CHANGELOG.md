# Changelog

## 0.1.0 — 2026-10-10

First public release.

- Use Nintendo Switch / Switch OLED as an extra display for the OpenDisplay Mac sender over Wi-Fi.
- Receive 1280×720 H.264 with Tegra NVDEC hardware decoding and software fallback.
- Separate network reads and decoding from rendering; present only the latest decoded frame.
- Recover from decoder faults and resolution changes; request fresh keyframes when needed.
- Show the Mac cursor with its original PNG and hotspot; support touch mouse input and UDP cursor updates.
- Prevent inactivity dimming and automatic sleep while a Mac is connected using native activity reporting. Normal idle behavior resumes after disconnection.
- Keep the connection alive on HOME; send an explicit goodbye on full application close.
- Provide a Switch-style menu, shared system fonts, native button glyphs and console light/dark theme.
- Open the menu with Plus; select using either stick, D-pad or touch; confirm with A and return with B.
- Offer decoder selection, statistics, language, Disconnect and Exit through the menu.
- Add English, Russian, Spanish and Simplified Chinese with persisted language selection.
- Show distinct-image FPS and estimated capture-to-present delay, with detailed stage diagnostics in session logs.

Physical testing confirmed comfortable video playback and HOME/system-close behavior on a Switch OLED. Continuous test streams normally sampled 57–60 FPS, but an intermittent network latency spike was also observed. Stable 60 FPS in all network conditions is not guaranteed. Audio stays on the Mac; no USB, HEVC or controller mouse support yet.
