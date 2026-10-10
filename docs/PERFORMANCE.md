# Receiver pipeline and performance verification

Build numbers in these measurements refer to internal development candidates,
not earlier public releases. The first public version is 0.1.0.

The inspected OpenDisplay source is pinned at
`b9eec1f232fb5ea571c87e8392379a84f47b237f`.
Its [shared receiver](https://github.com/peetzweg/opendisplay/blob/b9eec1f232fb5ea571c87e8392379a84f47b237f/Shared/StreamReceiver.swift)
uses a serial video queue, immediate sample display and minimum-RTT clock sync.
The optional [Metal renderer](https://github.com/peetzweg/opendisplay/blob/b9eec1f232fb5ea571c87e8392379a84f47b237f/iOS/MetalVideoRenderer.swift)
has a separate rendering queue and one pending decoded image. The default iOS
path uses the system video display layer; Metal is an optional experiment.

The Switch rc1 loop instead read TCP, decoded every available access unit,
transferred every image through VIC, uploaded each texture, then waited for
vsync. A real rc1 log recorded 53.7 received access units/s versus 13.9 presented
images/s. That demonstrates batch replacement and wasted copies; it does not
alone attribute every stall to the GPU or network.

rc2 uses one network/decoder worker and one decoded-image mailbox. SDL calls
stay on the main thread. The main thread takes the latest complete image and
transfers/uploads it once. Replacing decoded images is safe; skipping compressed
P-frames during normal playback would break references and is not done.
After a decoder reset, dependent packets are deliberately ignored until an IDR,
with periodic keyframe requests. SPS/PPS changes trigger a format reset while
retaining the socket. CPU fallback is used only after repeated decoder recovery
failures, not merely after changing quality.

SDL2's [OpenGL renderer](https://github.com/libsdl-org/SDL/blob/release-2.28.5/src/render/opengl/SDL_render_gl.c)
can sample NV12 through a shader. This port still does a VIC transfer and texture
upload; there is no claim of zero-copy rendering. Moonlight-Switch has a native
Deko3D direct-surface path, which is a possible later rendering replacement.

## Measurements

FPS counts only new video images presented, using monotonic one-second windows.
An unchanged desktop is labelled idle. DELAY uses capture timestamps threaded
through packet PTS and the lowest-RTT offset from the last 15 clock probes. Its
endpoint is SDL_RenderPresent return, not physical OLED scanout. Unknown delay
is shown as `--`. Asymmetric Wi-Fi can bias clock offset estimates.

The session log reports decode, VIC/upload, present wait, receiver residence,
replaced images, Mac capture FPS, encoder/network drops, and send queue depth.
Receiver telemetry uses OpenDisplay's standard fps/mbps/e2e/rtt/dec/interval keys.
Keeping these stage timings lets an actual test distinguish decoder slowness,
GPU wait and sender/network backpressure without inventing numbers.

## Validation

Host ASan/UBSan and ThreadSanitizer run the actual Video and StreamSession code.
Checks include repeated reconnects, fragmented H.264, latest-image retention,
quality/resolution changes, explicit close and EOF, clock mapping, UDP cursor
ack/deduplication, and a UI paused for 2.2 seconds while heartbeat continues.
These tests use host software decoding; NVDEC requires physical Switch testing.

For a continuous workload:

```
python3 tools/test_sender.py SWITCH_IP --seconds 30 --fps 60 --bitrate 6M
```

This prerecorded test pattern stamps capture at transmission time, so its delay
measures the delivery/presentation path, excluding a live Mac capture/encoder.
Use the real Mac sender for full capture-to-present measurements. Compare the
same quality, resolution, network and moving workload across versions; a static
desktop is not a 60 FPS benchmark. Check HOME return, full close, and a quality
change during the real sender session. Do not claim stable 60 FPS before this
physical benchmark succeeds.

### Physical Switch OLED test, 2026-10-10

The deployed rc2 completed a 30-second 1280×720, 60 FPS, 6 Mbit/s
continuous test without a disconnect. Five receiver samples reported
52.1–56.1 newly presented FPS, median decoding 2.30 ms, and estimated
delivery-to-present median 23.2–24.2 ms. Presentation interval median was
16.67 ms, but p95 was approximately 33.3 ms: some display updates are still
missed. This is evidence of working hardware playback, not a stable 60 FPS
result. The delay excludes live capture/encoding as described above.

Raw telemetry is retained locally in `dist/diagnostics/` and excluded from the public repository.
Detailed device stage timings are needed to identify the remaining bottleneck.

### rc3 comparison on the same physical Switch

The 30-second 720p/60, 6 Mbit/s test completed without disconnect.
Five one-second telemetry snapshots reported 60.0, 59.0, 60.1, 54.9 and
59.0 presented FPS. These are sampled windows, not a whole-test average.
Median estimated delivery-to-present latency increased to 33.0–34.9 ms
from rc2's 23.2–24.2 ms. Four sampled windows improved frame delivery,
but the latency regression and remaining low-FPS window prevent calling
this stable 60 FPS or an unconditional performance improvement. Device
stage timings are needed before selecting the final rendering strategy.

Raw rc3 telemetry is retained locally and excluded from the public repository.

Device rc3 stage logs showed median combined upload cost 4.1–4.4 ms,
versus 2.6–2.7 ms in rc2, and receiver residence 28.4–29.2 ms versus
19.4–20.9 ms. Median present wait remained about 12 ms. These measurements
do not isolate texture reuse from run-to-run producer/display phase or Wi-Fi
jitter. rc4 uses two upload textures and separately logs VIC transfer and
texture-update duration for a further comparison.

### rc4 physical comparison

The same 30-second 720p/60, 6 Mbit/s test completed without disconnect.
Sampled FPS windows: 59.0, 59.0, 60.0, 53.0, 19.6. The first four sampled
median delays were 29.9–30.4 ms, better than rc3 but above rc2. The final
window had 230.5 ms median delay, 479.9 ms p95 and 429.7 ms ping/pong RTT.
The user confirmed no HOME/menu interaction.

Device timings during the final stall remained close to earlier windows:
decode 2.3 ms, VIC 2.0 ms, texture update 1.6 ms, present wait 12.3 ms,
receiver residence 26.3 ms. Since residence is measured from local TCP
receipt, the much larger timestamp delay accumulated before receipt,
not in the measured decode/upload stages. The data does not identify
the specific Wi-Fi/router/TCP cause. A lower-bitrate control run is needed.

Normal rc4 windows had VIC 2.1 ms and texture update 1.6–1.7 ms.
Two textures remain the current experimental choice; stable 60 FPS
over Wi-Fi and live-Mac capture latency are not established.

Local telemetry and stage logs are in `dist/diagnostics/` with the
`0.3.0-rc4-switch-60fps-` prefix.

### rc4 lower-bitrate control, 3 Mbit/s

The user confirmed both Mac and Switch were on 5 GHz Wi-Fi. A further
30-second 720p/60 test at 3 Mbit/s completed without disconnect. Five
one-second snapshots reported 58.0, 58.0, 60.3, 57.1 and 59.1 FPS,
with median estimated delay 31.9–33.7 ms. Reported RTT was initially
13.5 ms, then 4.4–5.9 ms. No severe latency spike appeared in the five
snapshots. Normal delay did not improve over the 6 Mbit/s run.

This single control run does not establish that reducing bitrate fixes
the intermittent stall: the 6 Mbit/s rc2/rc3 runs also lacked a comparable
severe spike. These are sampled windows, not full-run extrema, and all
patterns exclude live Mac capture/encoding. The current receiver remains
a release candidate; longer runs and live-sender verification are needed.
Raw 3 Mbit/s telemetry is retained locally and excluded from the public repository.

### User live-playback validation

The user reported approximately ten minutes of YouTube playback via the
real Mac sender on rc4, with no visible image tears and comfortable
lip synchronization against audio played by Mac-connected AirPods.
HOME return also worked. These observations establish usability for
that session, not a quantitative capture-to-photon delay or proof of
indefinite stability. Full application close left the Mac reporting
lost connection for 5–10 seconds before teardown.

rc5 adds libnx's exit lock so a system-menu exit can execute the existing
bounded closing handshake before termination, and unlocks after cleanup.
Its system-close behavior needs a further physical check.

The user subsequently confirmed that rc5 corrected full system-menu close.
The final rc7 uses the same video pipeline, with updated menu and system button glyphs.
