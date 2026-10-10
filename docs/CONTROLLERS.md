# Controller input

Available in the **v0.2.0 development build**, not the published v0.1.0 release.

## Controls

| Input during video | Action |
|---|---|
| Left stick | Move the Mac pointer; a radial deadzone prevents drift |
| ZR | Hold the left mouse button; move the left stick while held to drag |
| Right stick | Scroll vertically and horizontally |
| ZL | Hold the right mouse button (Mac companion required) |
| X | Open the native Switch keyboard (Mac companion required) |
| + | Open/close settings; display pairing code and receiver IP address |

Sticks still navigate the menu while it is open. Touchscreen input remains available. Controller buttons are cancelled on HOME, menu/keyboard opening, touch takeover and connection loss; held triggers must be released before clicking again after a mode change.

Without the companion, pointer movement, ZR and scrolling use the official OpenDisplay input messages. ZL stays inactive. X opens the menu to show pairing details instead of a keyboard that cannot send text.

## Mac companion

The official OpenDisplay Sender continues to create and stream your display. **OpenDisplay NX Input** is a separate optional application that handles controller input and text. It requires macOS 13 or later; the local build matches the host Mac architecture. It is currently ad-hoc signed for local testing, not notarized for public distribution.

1. Build the Switch NRO with `./tools/build.sh` and install it.
2. Build and open the Mac companion with `./script/build_and_run.sh` (Xcode's Swift toolchain required).
3. Connect the official Sender to the Switch.
4. In the companion, enable **Accessibility** in macOS System Settings. This permission allows it to post mouse and keyboard events; screen recording stays with the Sender.
5. On Switch, press **+**. Enter the displayed receiver address and six-digit input pairing code in the companion.
6. Select the same **Target screen** that the Sender is streaming, then click **Connect**.
7. Return to video. Use the left stick and triggers to click a Mac text field, then press **X**. Confirming the system keyboard inserts the entered text into the currently focused Mac application. Cancelling it sends no text.

The keyboard sends confirmed text as a batch, including Cyrillic, Spanish, Chinese and emoji. It does not yet provide arbitrary key chords or a live hardware-keyboard stream. The decoder and companion heartbeat workers keep running while the system keyboard is open.

Pairing codes change when the main Sender reconnects. Re-pair the companion with the new code. Closing either application releases companion-owned mouse buttons. If the input connection fails, video remains connected and the basic controller mapping falls back to the official Sender.

## Input channel

The Switch listens on TCP port **9011** in addition to the unchanged video port 9000. Only the current video sender's IPv4 address can pair. A six-digit OS-random pairing code is required, with five failed authentication attempts allowed per video session. This is a trusted-LAN channel; it is not encrypted and the pairing code is not a substitute for TLS on an untrusted network.

Messages are UTF-8 JSON preceded by a four-byte big-endian payload length, capped at 8192 bytes. Mac to Switch: `auth` with `code`, then `ping`. Switch to Mac: `ready` (`id`, `device`), `pong`, `touch` (left/hover), `mouse` (right button), `scroll`, `text`, and `release`. Coordinate pairs are normalized 0…1 in video space. Input payloads and session state are bounded; incomplete frames, invalid coordinates and unknown commands cannot cause input injection.

## Verification

```sh
./tools/test.sh
swift test --package-path companion
./script/build_and_run.sh --verify
```

Tests cover deadzones, rate-independent motion, click/drag/release state, menu/HOME cancellation, right button transitions, fractional scrolling, real companion sockets, authentication, framing limits, heartbeat and Unicode payloads. They do not establish actual system-keyboard behavior or CGEvent delivery on Switch/Mac; physical paired testing remains required.
