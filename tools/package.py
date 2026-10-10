#!/usr/bin/env python3
"""Package the built NRO and complete port/FFmpeg sources for a release."""
import argparse
import hashlib
import pathlib
import struct
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', action='store_true')
    args = parser.parse_args()
    version = next(line.split(':=', 1)[1].strip() for line in
                   (ROOT / 'Makefile').read_text().splitlines() if line.startswith('APP_VERSION'))
    nro = ROOT / 'opendisplay-nx.nro'
    data = nro.read_bytes()
    if data[16:20] != b'NRO0':
        raise ValueError('invalid NRO header')
    size, = struct.unpack_from('<I', data, 24)
    if data[size:size+4] != b'ASET':
        raise ValueError('missing NRO assets')
    dist = ROOT / 'dist'
    dist.mkdir(exist_ok=True)
    archive = dist / f'OpenDisplay-NX-v{version}.zip'
    files = [(nro, 'switch/opendisplay-nx/opendisplay-nx.nro'),
             (ROOT / 'README.md', 'switch/opendisplay-nx/README.md'),
             (ROOT / 'CHANGELOG.md', 'switch/opendisplay-nx/CHANGELOG.md'),
             (ROOT / 'LICENSE', 'switch/opendisplay-nx/LICENSE')]
    files.extend((p, f'switch/opendisplay-nx/LICENSES/{p.name}')
                 for p in sorted((ROOT / 'LICENSES').iterdir()) if p.is_file())
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for source, target in files:
            z.write(source, target)
    checksums = [(archive.name, hashlib.sha256(archive.read_bytes()).hexdigest()),
                 (nro.name, hashlib.sha256(data).hexdigest())]
    if args.sources:
        ffmpeg = ROOT / 'upstream/ffmpeg-nvtegra'
        revision = (ROOT / 'tools/ffmpeg-nvtegra-revision.txt').read_text().strip()
        actual = subprocess.check_output(['git', '-C', str(ffmpeg), 'rev-parse', 'HEAD'], text=True).strip()
        if revision != actual:
            raise ValueError('FFmpeg source revision does not match build pin')
        source_archive = dist / f'OpenDisplay-NX-v{version}-sources.zip'
        with zipfile.ZipFile(source_archive, 'w', zipfile.ZIP_DEFLATED) as z:
            for dirname in ('source', 'include', 'tests', 'tools', 'LICENSES', 'docs', 'assets'):
                for p in sorted((ROOT / dirname).rglob('*')):
                    if p.is_file() and '__pycache__' not in p.parts:
                        z.write(p, p.relative_to(ROOT))
            for filename in ('Makefile', 'README.md', 'CHANGELOG.md', 'LICENSE', '.gitignore'):
                z.write(ROOT / filename, filename)
            names = subprocess.check_output(['git', '-C', str(ffmpeg), 'ls-files', '-z']).decode().split('\0')
            for name in names:
                if name and (ffmpeg / name).is_file():
                    z.write(ffmpeg / name, f'upstream/ffmpeg-nvtegra/{name}')
            # An extracted source archive has no Git database; the build script
            # accepts the embedded pinned revision in its place.
            z.writestr('upstream/ffmpeg-nvtegra/PINNED_REVISION', revision + '\n')
        checksums.append((source_archive.name, hashlib.sha256(source_archive.read_bytes()).hexdigest()))
        print(source_archive)
    (dist / f'OpenDisplay-NX-v{version}-SHA256SUMS.txt').write_text(
        ''.join(f'{digest}  {name}\n' for name, digest in checksums))
    print(archive)


if __name__ == '__main__':
    main()
