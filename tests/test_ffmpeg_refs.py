"""Execute the patched NVTEGRA DPB collection block with synthetic H.264 refs."""
import os
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ReferenceRegression(unittest.TestCase):
    def test_long_term_references(self):
        source = pathlib.Path(os.environ.get('FFMPEG_SOURCE_FILE', ROOT / 'upstream/ffmpeg-nvtegra/libavcodec/nvtegra_h264.c')).read_text()
        start = source.index('    num_refs = 0, max = FFMIN(h->short_ref_count')
        end = source.index('    /* Add all frames with an already allocated DPB index', start)
        block = source[start:end]
        program = '''
#include <cassert>
#include <algorithm>
#include <iostream>
#define FFMIN(a,b) std::min(int(a),int(b))
#define FF_ARRAY_ELEMS(a) (sizeof(a)/sizeof((a)[0]))
struct Picture { int identity; };
struct Context { int short_ref_count=0; Picture* short_ref[32]={}; Picture* long_ref[32]={}; };
int collect(Context* h, Picture* (&refs)[17]) {
  int num_refs, max, i;
''' + block + '''
  return num_refs;
}
int main() {
  Picture shortPicture{1}, longPicture{2}, edgePicture{3};
  Picture* refs[17]={}; Context h;
  // Regression: an all-long-term DPB must not dereference absent short_ref[0].
  h.long_ref[0]=&longPicture;
  assert(collect(&h,refs)==1); assert(refs[0]==&longPicture);
  // High long-term indices remain independent of the short-term array.
  h.long_ref[0]=nullptr; h.long_ref[15]=&edgePicture;
  assert(collect(&h,refs)==1); assert(refs[0]==&edgePicture);
  h.short_ref_count=1;h.short_ref[0]=&shortPicture;h.long_ref[0]=&longPicture;
  assert(collect(&h,refs)==3);
  assert(refs[0]==&shortPicture&&refs[1]==&longPicture&&refs[2]==&edgePicture);
  std::cout << "OK: NVTEGRA long-term DPB reference regression\\n";
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp)
            (path / 'refs.cpp').write_text(program)
            subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17',
                            '-fsanitize=address,undefined', str(path / 'refs.cpp'),
                            '-o', str(path / 'refs')], check=True)
            subprocess.run([str(path / 'refs')], check=True)


if __name__ == '__main__':
    unittest.main()
