#!/usr/bin/env python3
"""Stream a test pattern to OpenDisplay NX; requires host ffmpeg."""
import argparse
import json
import re
import socket
import struct
import subprocess
import threading
import time


def access_units(data):
    """Normalize FFmpeg's mixed 3/4-byte start codes and split at AUD NALUs."""
    nals = re.split(b'\x00\x00\x00\x01|\x00\x00\x01', data)
    units, current = [], bytearray()
    for nal in nals:
        if not nal:
            continue
        if nal[0] & 31 == 9 and current:
            units.append(bytes(current))
            current.clear()
        current.extend(b'\x00\x00\x00\x01' + nal)
    if current:
        units.append(bytes(current))
    return units


def read_frame(sock):
    def exact(n):
        out = bytearray()
        while len(out) < n:
            b = sock.recv(n - len(out))
            if not b:
                raise EOFError('receiver closed connection')
            out.extend(b)
        return bytes(out)
    n, = struct.unpack('!I', exact(4))
    if not 0 < n < 1 << 20:
        raise ValueError('invalid receiver frame size')
    return json.loads(exact(n))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ip')
    parser.add_argument('--port', type=int, default=9000)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--fps', type=int, choices=[15,30,60], default=15)
    parser.add_argument('--bitrate', default='6M')
    args = parser.parse_args()
    fps = args.fps
    encoded = subprocess.run([
        'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', f'testsrc2=size=1280x720:rate={fps}',
        '-t', str(args.seconds), '-an', '-c:v', 'libx264', '-preset', 'ultrafast',
        '-tune', 'zerolatency', '-pix_fmt', 'yuv420p', '-profile:v', 'baseline',
        '-b:v', args.bitrate, '-maxrate', args.bitrate, '-bufsize', args.bitrate,
        '-x264-params', f'aud=1:repeat-headers=1:keyint={fps}', '-f', 'h264', 'pipe:1'
    ], stdout=subprocess.PIPE, check=True).stdout
    units = access_units(encoded)
    lock = threading.Lock()
    stop = threading.Event()
    with socket.create_connection((args.ip, args.port), timeout=10) as sock:
        sock.settimeout(None)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        def send(payload):
            if isinstance(payload, dict):
                payload = json.dumps(payload, separators=(',', ':')).encode()
            with lock:
                sock.sendall(struct.pack('!I', len(payload)) + payload)
        hello = read_frame(sock)
        if hello.get('type') != 'hello':
            raise ValueError('first message must be hello')
        print('Receiver:', json.dumps(hello), flush=True)
        send({'type': 'welcome', 'pv': 3, 'min': 1})
        send({'type': 'streamConfig', 'codec': 'h264', 'width': 1280,
              'height': 720, 'framesPerSecond': fps})
        def controls():
            try:
                while not stop.is_set():
                    msg = read_frame(sock)
                    if msg.get('type') == 'ping':
                        send({'type': 'pong', 't': msg['t'], 'mt': time.time()*1000})
                    elif msg.get('type') == 'stats':
                        print('Stats:', json.dumps(msg), flush=True)
                    elif msg.get('type') == 'touch':
                        print('Touch:', msg, flush=True)
                    elif msg.get('type') == 'closing':
                        stop.set()
                    elif msg.get('type') == 'kf':
                        print('Keyframe requested (periodic IDR within 1 second)', flush=True)
            except (EOFError, OSError):
                stop.set()
        worker = threading.Thread(target=controls, daemon=True)
        worker.start()
        started = time.monotonic()
        try:
            for i, unit in enumerate(units):
                if stop.is_set():
                    break
                time.sleep(max(0, started + i/fps - time.monotonic()))
                stamp = time.time()*1000
                prefix = json.dumps({'cap': stamp, 'snd': stamp}).encode()
                send(prefix + unit)
                if i % (fps*2) == 0:
                    send({'type': 'ping'})
        finally:
            stop.set()
            sock.shutdown(socket.SHUT_RDWR)
            worker.join(timeout=2)
    print('Test stream finished.')


if __name__ == '__main__':
    main()
