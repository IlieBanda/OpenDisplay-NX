#!/usr/bin/env python3
"""Exercise the actual Video implementation with host FFmpeg/SDL and ASan.
Switch service calls are stubbed; the NVDEC path still requires device testing.
"""
import pathlib
import subprocess
import sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
from test_sender import access_units
root = pathlib.Path(__file__).resolve().parents[1]
build = root / 'build'
encoded = subprocess.check_output(['ffmpeg','-v','error','-f','lavfi','-i',
    'testsrc2=size=1280x720:rate=15','-frames:v','6','-c:v','libx264','-preset',
    'ultrafast','-tune','zerolatency','-x264-params','aud=1:repeat-headers=1',
    '-pix_fmt','yuv420p','-f','h264','pipe:1'])
units = access_units(encoded)
for i, unit in enumerate(units): (build / f'video-{i}.h264').write_bytes(unit)
small = subprocess.check_output(['ffmpeg','-v','error','-f','lavfi','-i',
    'testsrc2=size=640x360:rate=15','-frames:v','1','-c:v','libx264','-preset',
    'ultrafast','-tune','zerolatency','-x264-params','aud=1:repeat-headers=1',
    '-pix_fmt','yuv420p','-f','h264','pipe:1'])
(build / 'video-small.h264').write_bytes(access_units(small)[0])
source = (root / 'source/main.cpp').read_text()
video = source[source.index('class Video {'):source.index('static std::string identity()')]
prefix = r'''
#include "cursor.hpp"
#include "dashboard.hpp"
#include "overlay.hpp"
#include "performance.hpp"
#include "latest_frame.hpp"
#include "outbox.hpp"
#include "json.hpp"
#include <atomic>
#include <thread>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
using json=nlohmann::json;
#include "wire.hpp"
#include "stability.hpp"
#include <chrono>
#include <cstdlib>
#include <cassert>
#include <fstream>
#include <cstdarg>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#define AV_HWDEVICE_TYPE_NVTEGRA AV_HWDEVICE_TYPE_NONE
#define AV_PIX_FMT_NVTEGRA AV_PIX_FMT_NONE
static double now() { return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static double wallTime() {return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();}
static void logMessage(const char *, ...) {}
static void consoleExit(void *) {}
static void consoleInit(void *) {}
static void consoleUpdate(void *) {}
static void svcSleepThread(long) {}
static void *memalign(size_t align, size_t size) { void *p=nullptr; return posix_memalign(&p,align,size) ? nullptr : p; }
struct PlFontData {void *address;};
static int plInitialize(int) {return -1;}
static int plGetSharedFontByType(PlFontData *, int) {return -1;}
static void plExit() {}
static int setsysInitialize() {return -1;}
using ColorSetId = int;
static int setsysGetColorSetId(ColorSetId *) {return -1;}
static void setsysExit() {}
#define PlServiceType_User 0
#define PlSharedFontType_Standard 0
#define PlSharedFontType_ChineseSimplified 1
#define PlSharedFontType_NintendoExt 5
#define ColorSetId_Dark 1
#define R_SUCCEEDED(x) ((x)==0)
static SDL_Renderer *testRenderer(SDL_Window *w, int, unsigned) {return SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);}
#define SDL_CreateRenderer testRenderer
'''
test = r'''
#include "stream_session_test.inc"
int main() {
  SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
  av_log_set_level(AV_LOG_QUIET);
  std::vector<std::vector<uint8_t>> packets;
  for(int i=0;i<6;++i) {
    std::ifstream input("build/video-"+std::to_string(i)+".h264",std::ios::binary);
    packets.emplace_back(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
  }
  Video video;
  for(int session=0;session<30;++session) {
    assert(video.reset(true));
    assert(!video.decode({1,2,3,4}));
    for(const auto &p : packets) assert(video.decode(p));
    assert(!video.ready); // decoding must not invoke SDL or wait for vsync
    video.present(false,0,0); // single latest image, no fresh packet needed
    assert(video.decodedFrames==6);
    assert(video.ready);
    video.present(false,0,0);
    assert(video.displayedFrames==1);
    video.menuOpen=true;
    video.present(false,0,0);
    assert(video.ready); // opening settings retains the current video
    video.clearStream();
    assert(!video.ready && !video.hardwareActive && !video.connected);
    assert(!video.decode(packets.front())); // closed context is guarded
  }
  assert(video.reset(true));
  for (const auto &p : packets) {
    assert(video.decode(p));
    video.present(false, 0, 0);
  }
  assert(video.displayedFrames == packets.size());
  video.clearStream();
  assert(video.reset(true));
  double wall=wallTime();
  video.meter.pong(wall-1,wall+1,wall+1000);
  auto prefix=json({{"cap",wall+1000-20},{"snd",wall+1000-10}}).dump();
  std::vector<uint8_t> tagged(prefix.begin(),prefix.end());
  tagged.insert(tagged.end(),packets.front().begin(),packets.front().end());
  assert(video.decode(tagged)); video.present(false,0,0);
  auto stats=video.meter.sample(now()+1000);
  assert(stats.delayMs>=20 && stats.delayMs<500);
  video.clearStream();
  stressSessions(video,packets);
  video.stop();
}
'''
fixture = build / 'video-test.cpp'
fixture.write_text(prefix + video + test)
flags = subprocess.check_output(['pkg-config','--cflags','--libs','libavcodec','libavutil','libswscale','sdl2','libpng'],text=True).split()
subprocess.run(['clang++','-std=c++17','-fsanitize='+__import__('os').environ.get('VIDEO_SANITIZER','address,undefined'),'-Iinclude','-Itests',str(fixture),
                'source/ui_font.cpp',*flags,'-o','build/video-test'],cwd=root,check=True)
subprocess.run(['build/video-test'],cwd=root,check=True)
print('OK: real Video decoder, 30 reconnects, 180 H.264 images, latest-only presentation, HOME/menu retention and cleanup')
