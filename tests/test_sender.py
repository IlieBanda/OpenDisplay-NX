import importlib.util
import json
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sender', ROOT / 'tools/test_sender.py')
sender = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sender)


class SenderTest(unittest.TestCase):
    def test_start_codes(self):
        units = sender.access_units(b'\0\0\1\x09\xf0\0\0\0\1\x67\xab'
                                    b'\0\0\1\x65\x42\0\0\1\x09\xf0\0\0\1\x41\x55')
        self.assertEqual(len(units), 2)
        self.assertTrue(all(u.startswith(b'\0\0\0\1\x09') for u in units))

    def test_live_stream(self):
        video, controls, errors = [], [], []
        with socket.socket() as server:
            server.bind(('127.0.0.1', 0))
            server.listen(1)
            port = server.getsockname()[1]
            def receive():
                try:
                    with server.accept()[0] as peer:
                        hello = json.dumps({'type': 'hello', 'pv': 3}).encode()
                        packet = struct.pack('!I', len(hello)) + hello
                        # Exercise the sender's exact-read framing too.
                        for b in packet:
                            peer.sendall(bytes([b]))
                        def exact(n):
                            data = bytearray()
                            while len(data) < n:
                                chunk = peer.recv(n-len(data))
                                if not chunk:
                                    raise EOFError
                                data.extend(chunk)
                            return bytes(data)
                        while True:
                            n, = struct.unpack('!I', exact(4))
                            data = exact(n)
                            if b'\0' in data:
                                i = data.index(b'\0\0\0\1')
                                prefix = json.loads(data[:i])
                                self.assertIn('cap', prefix)
                                video.append(data[i:])
                            else:
                                controls.append(json.loads(data))
                except EOFError:
                    pass
                except Exception as e:
                    errors.append(e)
            thread = threading.Thread(target=receive)
            thread.start()
            result = subprocess.run([sys.executable, str(ROOT / 'tools/test_sender.py'),
                '127.0.0.1', '--port', str(port), '--seconds', '1'],
                capture_output=True, timeout=20)
            thread.join(timeout=5)
        self.assertFalse(errors)
        self.assertEqual(result.returncode, 0, result.stderr.decode())
        self.assertEqual(len(video), 15)
        self.assertEqual(controls[0]['type'], 'welcome')
        self.assertEqual(controls[1]['codec'], 'h264')
        decoded = subprocess.run(['ffmpeg', '-v', 'error', '-f', 'h264', '-i', 'pipe:0',
                                  '-f', 'null', '-'], input=b''.join(video),
                                 capture_output=True, timeout=10)
        self.assertEqual(decoded.returncode, 0, decoded.stderr.decode())


if __name__ == '__main__':
    unittest.main()
