#include "controller_input.hpp"
#include "cursor.hpp"
#include "dashboard.hpp"
#include "device_identity.hpp"
#include "discovery.hpp"
#include "input_companion.hpp"
#include "json.hpp"
#include "latest_frame.hpp"
#include "outbox.hpp"
#include "overlay.hpp"
#include "performance.hpp"
#include "stability.hpp"
#include "wire.hpp"
#include <SDL2/SDL.h>
#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <malloc.h>
#include <mutex>
#include <netinet/tcp.h>
#include <poll.h>
#include <switch.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
using json = nlohmann::json;
static std::mutex logMutex;
static FILE *sessionLog = nullptr;
static void logMessage(const char *format, ...) {
  std::lock_guard<std::mutex> lock(logMutex);
  if (!sessionLog)
    return;
  va_list args;
  va_start(args, format);
  vfprintf(sessionLog, format, args);
  va_end(args);
  fputc('\n', sessionLog);
  fflush(sessionLog);
}

static double now() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
static double wallTime() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
static bool finishSession(int peer, outbox::Queue &output) {
  if (peer < 0)
    return true;
  output.append("{\"type\":\"closing\"}");
  double deadline = now() + 750;
  while (!output.empty() && now() < deadline) {
    if (!output.pump(
            [&](const uint8_t *p, size_t n) { return send(peer, p, n, 0); }))
      return false;
    if (!output.empty())
      svcSleepThread(5000000);
  }
  if (!output.empty())
    return false;
  shutdown(peer, SHUT_WR);
  // Leave the read side open briefly for the peer to consume closing and end
  // its session.
  uint8_t discard[65536];
  deadline = now() + 150;
  while (now() < deadline) {
    int n = recv(peer, discard, sizeof(discard), 0);
    if (n == 0)
      break;
    if (n > 0)
      continue;
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
      break;
    svcSleepThread(5000000);
  }
  return true;
}
static void nonblock(int s) {
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
}
class Video {
  AVCodecContext *ctx = nullptr;
  AVFrame *frame = av_frame_alloc();
  AVFrame *linear = av_frame_alloc();
  AVFrame *displayFrame = av_frame_alloc();
  LatestFrame latest;
  perf::Stamp displayStamp;
  std::deque<perf::Stamp> stamps;
  int64_t packetSequence = 0;
  double uploadDuration = 0;
  AVBufferRef *hardwareDevice = nullptr;
  SDL_Texture *cursorTexture = nullptr;
  unsigned cursorRevision = 0;
  int textureFormat = -1;
  SwsContext *sws = nullptr;
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  static constexpr unsigned textureCount = 2;
  SDL_Texture *videoTextures[textureCount]{};
  double transferDuration = 0, textureDuration = 0;
  unsigned nextTexture = 0;
  void clearTextures() {
    for (auto &item : videoTextures) {
      if (item)
        SDL_DestroyTexture(item);
      item = nullptr;
    }
    texture = nullptr;
    nextTexture = 0;
  }
  std::vector<uint8_t> rgba;
  int width = 0, height = 0;
  bool graphical = false;
  bool planar = false;
  bool dirty = false;

public:
  SDL_Rect rect{0, 0, 1280, 720};
  bool ready = false;
  cursor::Image cursorImage;
  std::atomic<bool> hardwareAvailable{false};
  std::atomic<bool> hardwareActive{false};
  std::atomic<uint64_t> decodedFrames{0};
  uint64_t displayedFrames = 0;
  perf::Meter meter;
  bool showOverlay = false;
  std::string state = "WAITING FOR MAC", address = "0.0.0.0";
  bool softwarePreference = false;
  bool darkTheme = true, menuOpen = false, connected = false;

  locale::Language language = locale::Language::English;
  std::string inputPairCode;
  bool inputConnected = false;
  int focus = 0;
  bool fontService = false;
  int maxFrameRate() const { return hardwareAvailable ? 60 : 30; }
  void releaseDecoder() {
    if (frame)
      av_frame_unref(frame);
    if (linear)
      av_frame_unref(linear);
    if (displayFrame)
      av_frame_unref(displayFrame);
    latest.clear();
    stamps.clear();
    avcodec_free_context(&ctx);
    av_buffer_unref(&hardwareDevice);
    sws_freeContext(sws);
    sws = nullptr;
    hardwareAvailable = false;
    hardwareActive = false;
  }
  bool reset(bool softwareOnly = false, bool preserveImage = false) {
    if (!linear || !frame || !displayFrame || !latest.valid())
      return false;
    releaseDecoder();
    hardwareAvailable = false;
    hardwareActive = false;
    decodedFrames = 0;
    displayedFrames = 0;
    meter.reset(now());
    if (!softwareOnly &&
        av_hwdevice_ctx_create(&hardwareDevice, AV_HWDEVICE_TYPE_NVTEGRA,
                               nullptr, nullptr, 0) >= 0)
      hardwareAvailable = true;
    logMessage("Decoder: hardware device %s",
               hardwareAvailable ? "ready" : "unavailable / software selected");
    ctx = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
    if (!ctx || !frame)
      return false;
    ctx->opaque = this;
    if (hardwareAvailable) {
      ctx->hw_device_ctx = av_buffer_ref(hardwareDevice);
      ctx->get_format = [](AVCodecContext *context,
                           const AVPixelFormat *formats) {
        auto *self = static_cast<Video *>(context->opaque);
        for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
          if (*p == AV_PIX_FMT_NVTEGRA)
            return *p;
        self->hardwareAvailable = false;
        for (auto p = formats; *p != AV_PIX_FMT_NONE; ++p)
          if (*p == AV_PIX_FMT_YUV420P)
            return *p;
        return formats[0];
      };
    }
    ctx->max_pixels = 1280 * 768;
    ctx->thread_count = 1;
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->framerate = AVRational{maxFrameRate(), 1};
    ctx->pkt_timebase = AVRational{1, 1000};
    if (!preserveImage)
      ready = false;
    return avcodec_open2(ctx, ctx->codec, nullptr) >= 0;
  }
  bool decode(const std::vector<uint8_t> &p, double arrival = 0) {
    if (!ctx || !frame || !linear || !displayFrame)
      return false;
    const double started = now();
    meter.receive(started, p.size());
    size_t start = wire::annexB(p);
    if (start == p.size())
      return false;
    perf::Stamp stamp;
    stamp.id = ++packetSequence;
    stamp.arrival = arrival > 0 ? arrival : started;
    if (start > 0 && start < 4096) {
      std::vector<uint8_t> prefix(p.begin(), p.begin() + start);
      if (wire::safeJsonDepth(prefix)) {
        auto meta = json::parse(prefix.begin(), prefix.end(), nullptr, false);
        if (meta.is_object()) {
          if (meta.contains("cap") && meta["cap"].is_number())
            stamp.capture = meta["cap"].get<double>();
          if (meta.contains("snd") && meta["snd"].is_number())
            stamp.sent = meta["snd"].get<double>();
        }
      }
    }
    stamps.push_back(stamp);
    if (stamps.size() > 256)
      stamps.pop_front();
    AVPacket *pkt = av_packet_alloc();
    if (!pkt)
      return false;
    if (av_new_packet(pkt, p.size() - start) < 0) {
      av_packet_free(&pkt);
      return false;
    }
    memcpy(pkt->data, p.data() + start, p.size() - start);
    pkt->pts = stamp.id;
    int rc = avcodec_send_packet(ctx, pkt);
    if (rc == AVERROR(EAGAIN)) {
      // Drain a pending image, then retry the same packet: never lose an AU.
      if (!receivePictures()) {
        av_packet_free(&pkt);
        return false;
      }
      rc = avcodec_send_packet(ctx, pkt);
    }
    av_packet_free(&pkt);
    if (rc < 0)
      return false;
    bool ok = receivePictures();
    meter.decoded(now() - started);
    return ok;
  }
  bool receivePictures() {
    int rc;
    while ((rc = avcodec_receive_frame(ctx, frame)) >= 0) {
      if (!stability::validPicture(frame->width, frame->height))
        return false;
      hardwareActive = frame->format == AV_PIX_FMT_NVTEGRA;
      auto found =
          std::find_if(stamps.begin(), stamps.end(), [&](const perf::Stamp &s) {
            return s.id == frame->pts;
          });
      perf::Stamp stamp;
      if (found != stamps.end()) {
        stamp = *found;
        stamps.erase(stamps.begin(), std::next(found));
      }
      stamp.decoded = now();
      bool replaced = false;
      if (!latest.publish(frame, stamp, replaced))
        return false;
      if (replaced)
        meter.replace();
      if (decodedFrames == 0)
        logMessage("First decoded image: format=%d raster=%dx%d", frame->format,
                   frame->width, frame->height);
      ++decodedFrames;
    }
    return rc == AVERROR(EAGAIN) || rc == AVERROR_EOF;
  }
  bool uploadPicture() {
    if (!stability::validPicture(displayFrame->width, displayFrame->height))
      return false;
    transferDuration = textureDuration = 0;
    AVFrame *picture = displayFrame;
    hardwareActive = displayFrame->format == AV_PIX_FMT_NVTEGRA;
    if (decodedFrames == 0)
      logMessage("First frame: format=%d raster=%dx%d", displayFrame->format,
                 displayFrame->width, displayFrame->height);
    if (hardwareActive) {
      if (!displayFrame->hw_frames_ctx || !displayFrame->hw_frames_ctx->data)
        return false;
      auto *frames = reinterpret_cast<AVHWFramesContext *>(
          displayFrame->hw_frames_ctx->data);
      if (!stability::validSurface(frames->width, frames->height,
                                   displayFrame->width, displayFrame->height))
        return false;
      if (!linear->buf[0] || linear->format != frames->sw_format ||
          linear->width != frames->width || linear->height != frames->height) {
        av_frame_unref(linear);
        linear->format = frames->sw_format;
        linear->width = frames->width;
        linear->height = frames->height;
        int size = av_image_get_buffer_size(frames->sw_format, frames->width,
                                            frames->height, 256);
        if (size <= 0)
          return false;
        size = (size + 4095) & ~4095;
        auto *memory = static_cast<uint8_t *>(memalign(4096, size));
        if (!memory)
          return false;
        linear->buf[0] = av_buffer_create(
            memory, size, [](void *, uint8_t *p) { free(p); }, nullptr, 0);
        if (!linear->buf[0]) {
          free(memory);
          return false;
        }
        if (av_image_fill_arrays(linear->data, linear->linesize, memory,
                                 frames->sw_format, frames->width,
                                 frames->height, 256) < 0)
          return false;
      }
      const double transferStart = now();
      if (av_hwframe_transfer_data(linear, displayFrame, 0) < 0)
        return false;
      transferDuration = now() - transferStart;
      if (decodedFrames == 0)
        logMessage("Hardware frame transfer complete");
      picture = linear;
    }
    if (displayFrame->width < 1 || displayFrame->height < 1 ||
        displayFrame->width > 1280 || displayFrame->height > 720)
      return false;
    if (!ensureGraphics())
      return false;
    bool useYuv = picture->format == AV_PIX_FMT_YUV420P;
    bool useNV12 = picture->format == AV_PIX_FMT_NV12;
    if (!texture || displayFrame->width != width ||
        displayFrame->height != height || textureFormat != picture->format) {
      // Rotate upload targets so the preceding draw can retain its texture.
      // This is GPU resource buffering, not a queue of older video images.
      SDL_Texture *replacement[textureCount]{};
      const Uint32 format = useNV12  ? SDL_PIXELFORMAT_NV12
                            : useYuv ? SDL_PIXELFORMAT_IYUV
                                     : SDL_PIXELFORMAT_ABGR8888;
      for (auto &item : replacement) {
        item = SDL_CreateTexture(renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                 displayFrame->width, displayFrame->height);
        if (!item) {
          for (auto allocated : replacement)
            if (allocated)
              SDL_DestroyTexture(allocated);
          return false;
        }
      }
      clearTextures();
      for (unsigned i = 0; i < textureCount; ++i)
        videoTextures[i] = replacement[i];
      planar = useYuv;
      textureFormat = picture->format;
      width = displayFrame->width;
      height = displayFrame->height;
      logMessage("Upload path: %s raster=%dx%d pitch=%d",
                 useNV12 ? "NV12" : (useYuv ? "IYUV" : "RGBA"), width, height,
                 picture->linesize[0]);
      if (!useYuv && !useNV12)
        rgba.resize(width * height * 4);
      double scale = std::min(1280.0 / width, 720.0 / height);
      rect.w = width * scale;
      rect.h = height * scale;
      rect.x = (1280 - rect.w) / 2;
      rect.y = (720 - rect.h) / 2;
    }
    auto *uploadTarget = videoTextures[nextTexture];
    if (!uploadTarget)
      return false;
    const double textureStart = now();
    int upload = 0;
    if (useNV12) {
      upload = SDL_UpdateNVTexture(uploadTarget, nullptr, picture->data[0],
                                   picture->linesize[0], picture->data[1],
                                   picture->linesize[1]);
    } else if (planar) {
      upload = SDL_UpdateYUVTexture(uploadTarget, nullptr, picture->data[0],
                                    picture->linesize[0], picture->data[1],
                                    picture->linesize[1], picture->data[2],
                                    picture->linesize[2]);
    } else {
      sws = sws_getCachedContext(
          sws, width, height, (AVPixelFormat)picture->format, width, height,
          AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
      if (!sws)
        return false;
      uint8_t *dest[] = {rgba.data()};
      int pitch[] = {width * 4};
      if (sws_scale(sws, picture->data, picture->linesize, 0, height, dest,
                    pitch) != height)
        return false;
      upload = SDL_UpdateTexture(uploadTarget, nullptr, rgba.data(), width * 4);
    }
    textureDuration = now() - textureStart;
    if (upload < 0)
      return false;
    texture = uploadTarget;
    nextTexture = (nextTexture + 1) % textureCount;
    ready = true;
    dirty = true;
    return true;
  }
  bool ensureGraphics() {
    if (graphical)
      return true;
    consoleExit(nullptr);
    graphical = true;
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      stop();
      return false;
    }
    window = SDL_CreateWindow("OpenDisplay NX", 0, 0, 1280, 720,
                              SDL_WINDOW_FULLSCREEN);
    renderer = window ? SDL_CreateRenderer(window, -1,
                                           SDL_RENDERER_ACCELERATED |
                                               SDL_RENDERER_PRESENTVSYNC)
                      : nullptr;
    if (!renderer) {
      stop();
      return false;
    }
    if (R_SUCCEEDED(plInitialize(PlServiceType_User))) {
      fontService = true;
      PlFontData font{};
      PlFontData chinese{}, icons{};
      plGetSharedFontByType(&chinese, PlSharedFontType_ChineseSimplified);
      plGetSharedFontByType(&icons, PlSharedFontType_NintendoExt);
      if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard)))
        ui::initializeFont(renderer,
                           static_cast<const unsigned char *>(font.address),
                           static_cast<const unsigned char *>(chinese.address),
                           static_cast<const unsigned char *>(icons.address));
    }
    if (R_SUCCEEDED(setsysInitialize())) {
      ColorSetId theme = ColorSetId_Dark;
      if (R_SUCCEEDED(setsysGetColorSetId(&theme)))
        darkTheme = theme == ColorSetId_Dark;
      setsysExit();
    }
    SDL_RendererInfo info{};
    SDL_GetRendererInfo(renderer, &info);
    logMessage("SDL renderer: %s flags=%u", info.name ? info.name : "unknown",
               info.flags);
    bool nativeNV12 = false;
    for (Uint32 i = 0; i < info.num_texture_formats; ++i)
      if (info.texture_formats[i] == SDL_PIXELFORMAT_NV12)
        nativeNV12 = true;
    logMessage("Native NV12 textures: %s", nativeNV12 ? "yes" : "no");
    return true;
  }
  void dashboard() {
    ::dashboard::draw(renderer, state, address, softwarePreference, darkTheme,
                      focus, showOverlay, connected, language, inputPairCode,
                      inputConnected);
  }
  void present(bool cursor, double x, double y) {
    if (!ensureGraphics()) {
      consoleUpdate(nullptr);
      svcSleepThread(10000000);
      return;
    }
    if (!menuOpen && latest.take(displayFrame, displayStamp)) {
      double started = now();
      if (!uploadPicture())
        logMessage("Frame upload failed: %s", SDL_GetError());
      av_frame_unref(displayFrame);
      uploadDuration = now() - started;
      meter.uploadStages(transferDuration, textureDuration);
    }
    const auto measured = meter.sample(now());
    if (!ready || menuOpen) {
      dashboard();
      return;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, &rect);
    if (cursor) {
      if (!cursorImage.rgba.empty() && cursorRevision != cursorImage.revision) {
        if (cursorTexture)
          SDL_DestroyTexture(cursorTexture);
        cursorTexture = SDL_CreateTexture(
            renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC,
            cursorImage.width, cursorImage.height);
        if (cursorTexture) {
          SDL_UpdateTexture(cursorTexture, nullptr, cursorImage.rgba.data(),
                            cursorImage.width * 4);
          SDL_SetTextureBlendMode(cursorTexture, SDL_BLENDMODE_BLEND);
          cursorRevision = cursorImage.revision;
        }
      }
      int px = rect.x + x * rect.w, py = rect.y + y * rect.h;
      if (cursorTexture) {
        SDL_Rect bounds;
        bounds.w = std::max(1, int(cursorImage.nw * rect.w));
        bounds.h = std::max(1, int(cursorImage.nh * rect.h));
        bounds.x = px - cursorImage.ax * bounds.w;
        bounds.y = py - cursorImage.ay * bounds.h;
        SDL_RenderCopy(renderer, cursorTexture, nullptr, &bounds);
      } else {
        // Arrow until the sender supplies its current cursor PNG.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        for (int row = 0; row < 19; ++row)
          SDL_RenderDrawLine(renderer, px, py + row, px + row / 2, py + row);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        for (int row = 2; row < 17; ++row)
          SDL_RenderDrawLine(renderer, px + 1, py + row,
                             px + std::max(1, row / 2 - 1), py + row);
      }
    }
    if (showOverlay) {
      SDL_Rect background{12, 12, 680, 80};
      SDL_SetRenderDrawColor(renderer, 12, 18, 28, 255);
      SDL_RenderFillRect(renderer, &background);
      SDL_SetRenderDrawColor(renderer, 232, 237, 246, 255);
      char label[100];
      snprintf(label, sizeof(label), "%s / %s",
               hardwareActive ? "NVDEC" : "CPU",
               locale::text(measured.idle ? locale::Idle : locale::Streaming,
                            language));
      overlay::text(renderer, label, 24, 24, 3);
      if (measured.idle)
        snprintf(label, sizeof(label), "%s",
                 locale::text(locale::DesktopIdle, language));
      else if (measured.delayMs >= 0)
        snprintf(label, sizeof(label), "%.0f FPS / %s %.0f ms", measured.fps,
                 locale::text(locale::Delay, language), measured.delayMs);
      else
        snprintf(label, sizeof(label), "%.0f FPS / %s --", measured.fps,
                 locale::text(locale::Delay, language));
      overlay::text(renderer, label, 24, 58, 3);
    }
    const double presentStarted = now();
    SDL_RenderPresent(renderer);
    if (dirty) {
      ++displayedFrames;
      meter.presented(displayStamp, now(), wallTime(), uploadDuration,
                      now() - presentStarted);
      dirty = false;
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
    }
  }
  void clearStream() {
    releaseDecoder();
    connected = false;
    menuOpen = false;
    if (cursorTexture)
      SDL_DestroyTexture(cursorTexture);
    clearTextures();
    cursorTexture = nullptr;
    cursorRevision = 0;
    cursorImage.clear();
    ready = dirty = false;
    width = height = 0;
    textureFormat = -1;
    hardwareActive = false;
    meter.reset(now());
  }
  void stop() {
    ui::releaseFont();
    if (fontService) {
      plExit();
      fontService = false;
    }
    if (cursorTexture)
      SDL_DestroyTexture(cursorTexture);
    cursorTexture = nullptr;
    cursorRevision = 0;
    cursorImage.clear();
    clearTextures();
    if (renderer)
      SDL_DestroyRenderer(renderer);
    if (window)
      SDL_DestroyWindow(window);
    texture = nullptr;
    renderer = nullptr;
    window = nullptr;
    SDL_Quit();
    if (graphical)
      consoleInit(nullptr);
    graphical = false;
    ready = false;
    width = height = 0;
  }
  ~Video() {
    stop();
    releaseDecoder();
    av_frame_free(&frame);
    av_frame_free(&linear);
    av_frame_free(&displayFrame);
  }
};
#include "stream_session.hpp"

static std::string identity() {
  const char *path = "sdmc:/switch/opendisplay-nx/id.txt";
  mkdir("sdmc:/switch/opendisplay-nx", 0777);
  std::ifstream f(path);
  std::string id;
  f >> id;
  if (id.size() == 36 && std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-';
      }))
    return id;
  uint8_t b[16];
  randomGet(b, 16);
  b[6] = (b[6] & 15) | 0x40;
  b[8] = (b[8] & 63) | 0x80;
  char s[37];
  snprintf(
      s, sizeof(s),
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11],
      b[12], b[13], b[14], b[15]);
  std::ofstream(path) << s;
  return s;
}
int main() {
  consoleInit(nullptr);
  appletSetFocusHandlingMode(AppletFocusHandlingMode_NoSuspend);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  hidInitializeTouchScreen();
  appletSetWirelessPriorityMode(AppletWirelessPriorityMode_OptimizedForWlan);
  SocketInitConfig socketConfig = *socketGetDefaultInitConfig();
  socketConfig.tcp_rx_buf_size = 256 * 1024;
  socketConfig.tcp_rx_buf_max_size = 1024 * 1024;
  socketConfig.num_bsd_sessions = 4;
  Result result = socketInitialize(&socketConfig);
  if (R_FAILED(result)) {
    printf("Socket initialization failed: %08x\n", result);
    while (appletMainLoop()) {
      padUpdate(&pad);
      if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
        break;
      consoleUpdate(nullptr);
    }
    consoleExit(nullptr);
    return 1;
  }
  nifmInitialize(NifmServiceType_User);
  u32 ip = 0;
  nifmGetCurrentIpAddress(&ip);
  std::string id = identity();
  rename("sdmc:/switch/opendisplay-nx/session.log",
         "sdmc:/switch/opendisplay-nx/session-previous.log");
  sessionLog = fopen("sdmc:/switch/opendisplay-nx/session.log", "w");
  logMessage("OpenDisplay NX 0.2.0");
  Result modelService = setsysInitialize();
  if (R_SUCCEEDED(modelService)) {
    SetSysProductModel productModel = SetSysProductModel_Invalid;
    Result modelResult = setsysGetProductModel(&productModel);
    if (R_SUCCEEDED(modelResult)) {
      switch (productModel) {
      case SetSysProductModel_Nx:
      case SetSysProductModel_Iowa:
        device::name = device::nameForModel(device::Model::Standard);
        break;
      case SetSysProductModel_Hoag:
        device::name = device::nameForModel(device::Model::Lite);
        break;
      case SetSysProductModel_Aula:
        device::name = device::nameForModel(device::Model::Oled);
        break;
      default:
        break;
      }
      logMessage("Console product model: %u (%s)", unsigned(productModel),
                 device::name);
    } else {
      logMessage("Product model query failed: %08x; using %s", modelResult,
                 device::name);
    }
    setsysExit();
  } else {
    logMessage("Product model service unavailable: %08x; using %s",
               modelService, device::name);
  }
  av_log_set_level(AV_LOG_WARNING);
  av_log_set_callback([](void *, int level, const char *format, va_list args) {
    std::lock_guard<std::mutex> lock(logMutex);
    if (!sessionLog || level > AV_LOG_WARNING ||
        ftell(sessionLog) > 1024 * 1024)
      return;
    vfprintf(sessionLog, format, args);
    fflush(sessionLog);
  });
  int listener = socket(AF_INET, SOCK_STREAM, 0),
      mdns = socket(AF_INET, SOCK_DGRAM, 0);
  int one = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(9000);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (listener < 0 || bind(listener, (sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(listener, 2) < 0) {
    printf("Cannot listen on TCP 9000. Restart receiver.\n");
    while (appletMainLoop()) {
      padUpdate(&pad);
      if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
        break;
      consoleUpdate(nullptr);
    }
    if (listener >= 0)
      close(listener);
    if (mdns >= 0)
      close(mdns);
    nifmExit();
    socketExit();
    consoleExit(nullptr);
    return 1;
  }
  nonblock(listener);
  setsockopt(mdns, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  addr.sin_port = htons(5353);
  bind(mdns, (sockaddr *)&addr, sizeof(addr));
  ip_mreq group{};
  group.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
  group.imr_interface.s_addr = ip;
  setsockopt(mdns, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));
  unsigned char ttl = 255;
  setsockopt(mdns, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  nonblock(mdns);
  sockaddr_in multicast{};
  multicast.sin_family = AF_INET;
  multicast.sin_port = htons(5353);
  multicast.sin_addr.s_addr = group.imr_multiaddr.s_addr;
  auto announce = [&](unsigned lifetime) {
    auto packet = discovery::announcement(id, ip, lifetime);
    sendto(mdns, packet.data(), packet.size(), 0, (sockaddr *)&multicast,
           sizeof(multicast));
  };
  Video video;
  {
    int savedLanguage = 0;
    std::ifstream preferences("sdmc:/switch/opendisplay-nx/language.txt");
    if ((preferences >> savedLanguage) && savedLanguage >= 0 &&
        savedLanguage < 4)
      video.language = static_cast<locale::Language>(savedLanguage);
  }
  auto status = [&](const char *text) {
    in_addr a{ip};
    video.address = inet_ntoa(a);
    video.state = text;
    logMessage("State: %s", text);
  };
  status("WAITING FOR MAC");
  int peer = -1;
  outbox::Queue output;
  int cursorSocket = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in cursorLocal{};
  cursorLocal.sin_family = AF_INET;
  cursorLocal.sin_addr.s_addr = INADDR_ANY;
  cursorLocal.sin_port = 0;
  socklen_t cursorLocalSize = sizeof(cursorLocal);
  if (cursorSocket < 0 ||
      bind(cursorSocket, reinterpret_cast<sockaddr *>(&cursorLocal),
           sizeof(cursorLocal)) < 0 ||
      getsockname(cursorSocket, reinterpret_cast<sockaddr *>(&cursorLocal),
                  &cursorLocalSize) < 0) {
    if (cursorSocket >= 0)
      close(cursorSocket);
    cursorSocket = -1;
  } else
    nonblock(cursorSocket);
  StreamSession stream(video, output, cursorSocket);
  InputCompanion companion;
  const bool companionAvailable = companion.start();
  logMessage("Input companion listener: %s",
             companionAvailable ? "ready" : "unavailable");
  bool companionMode = false;
  double lastAd = 0, networkCheck = 0, discoveryCheck = -250;
  double lastActivityReport = -30000;
  controller::Pointer controllerPointer;
  double lastControllerTime = now();
  bool touching = false, cursor = false, touchArmed = true;
  double tx = 0, ty = 0, cx = 0, cy = 0;
  bool drop = false;
  auto queue = [&](const json &j) {
    if (peer >= 0 && !stream.queue(j))
      drop = true;
  };
  auto queueController = [&](const json &j) {
    if (companionMode)
      companion.queue(j);
    else
      queue(j);
  };
  bool softwareOnly = false;
  int offeredRate = 30;
  int hardwareResyncs = 0;
  double hardwareResyncAt = 0;
  double statsTime = 0;
  auto hello = [&]() {
    auto message = json(
        {{"type", "hello"},
         {"pixelsWide", 1280},
         {"pixelsHigh", 720},
         {"scale", 1},
         {"panel", {{"pixelsWide", 1280}, {"pixelsHigh", 720}, {"scale", 1}}},
         {"device", device::name},
         {"id", id},
         {"pv", 3},
         {"displayMaxFrameRate", 60},
         {"maxEncodeWide", 1280},
         {"maxEncodeHigh", 720},
         {"videoCaps",
          json::array({{{"codec", "h264"},
                        {"maxWidth", 1280},
                        {"maxHeight", 720},
                        {"maxFrameRate", video.maxFrameRate()}}})}});
    if (cursorSocket >= 0)
      message["cursorPort"] = ntohs(cursorLocal.sin_port);
    return message;
  };
  auto disconnect = [&]() {
    stream.stop();
    companion.end();
    video.inputPairCode.clear();
    video.inputConnected = false;
    companionMode = false;
    if (peer >= 0)
      close(peer);
    peer = -1;
    output.clear();
    video.clearStream();
    touching = false;
    touchArmed = true;
    controllerPointer.reset();
    cursor = false;
    drop = false;
    status("Waiting for sender...");
  };
  auto closeCleanly = [&]() {
    if (peer >= 0)
      for (auto &event : controllerPointer.cancel())
        queueController(event);
    stream.stop();
    if (peer >= 0 && touching)
      queueController(
          {{"type", "touch"}, {"phase", "cancelled"}, {"x", tx}, {"y", ty}});
    if (peer >= 0)
      logMessage("Closing notification %s",
                 finishSession(peer, output) ? "sent" : "failed");
    disconnect();
  };
  bool menuTouching = false;
  bool withdrawn = false;
  std::function<void()> leaveSession = [&]() {
    closeCleanly();
    announce(0);
    withdrawn = true;
  };
  // System-menu close must allow the goodbye to reach the Mac before the
  // process is terminated. HOME alone does not request exit.
  const Result exitLockResult = appletLockExit();
  const bool exitLocked = R_SUCCEEDED(exitLockResult);
  logMessage("Graceful system exit lock: %08x", exitLockResult);
  AppletHookCookie hookCookie{};
  appletHook(
      &hookCookie,
      [](AppletHookType type, void *context) {
        if (type == AppletHookType_OnExitRequest)
          (*static_cast<std::function<void()> *>(context))();
      },
      &leaveSession);
  while (appletMainLoop()) {
    bool foreground = appletGetFocusState() == AppletFocusState_InFocus;
    padUpdate(&pad);
    u64 down = foreground ? padGetButtonsDown(&pad) : 0;
    HidTouchScreenState touch{};
    if (foreground)
      hidGetTouchScreenStates(&touch, 1);
    bool menuTouch = touch.count > 0 && (!video.ready || video.menuOpen);
    if (menuTouch && !menuTouching) {
      int item = dashboard::hitTest(touch.touches[0].x, touch.touches[0].y);
      if (item >= 0) {
        video.focus = item;
        down |= HidNpadButton_A;
      }
    }
    menuTouching = menuTouch;
    if (down & HidNpadButton_Plus) {
      video.menuOpen = !video.menuOpen;
      if (video.menuOpen)
        video.focus = 0;
    }
    if (down & HidNpadButton_B)
      video.menuOpen = false;
    if (!video.ready || video.menuOpen) {
      if (down & (HidNpadButton_Down | HidNpadButton_StickLDown |
                  HidNpadButton_StickRDown))
        video.focus = (video.focus + 1) % 5;
      if (down &
          (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp))
        video.focus = (video.focus + 4) % 5;
      if (down & HidNpadButton_A) {
        switch (video.focus) {
        case 0:
          softwareOnly = !softwareOnly;
          video.softwarePreference = softwareOnly;
          logMessage("User selected %s", softwareOnly ? "software" : "NVDEC");
          closeCleanly();
          status(softwareOnly ? "Software mode. Waiting for sender..."
                              : "NVDEC mode. Waiting for sender...");
          break;
        case 1:
          video.showOverlay = !video.showOverlay;
          break;
        case 2: {
          video.language =
              static_cast<locale::Language>((int(video.language) + 1) % 4);
          std::ofstream preferences("sdmc:/switch/opendisplay-nx/language.txt");
          preferences << int(video.language) << '\n';
          break;
        }
        case 3:
          closeCleanly();
          break;
        case 4:
          goto finished;
        }
      }
    }
    double t = now();
    const double controllerSeconds = (t - lastControllerTime) / 1000.0;
    lastControllerTime = t;
    if (withdrawn && appletGetFocusState() == AppletFocusState_InFocus) {
      announce(120);
      withdrawn = false;
      lastAd = t;
    }
    u32 newIp = ip;
    if (t - networkCheck > 1000) {
      nifmGetCurrentIpAddress(&newIp);
      networkCheck = t;
    }
    if (newIp && newIp != ip) {
      announce(0);
      disconnect();
      setsockopt(mdns, IPPROTO_IP, IP_DROP_MEMBERSHIP, &group, sizeof(group));
      ip = newIp;
      group.imr_interface.s_addr = ip;
      setsockopt(mdns, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));
      lastAd = 0;
      status("Network changed. Waiting for sender...");
    }
    // Service discovery does not need a BSD IPC call on every video refresh.
    const bool checkDiscovery = t - discoveryCheck >= 100;
    uint8_t buf[65536];
    int got = -1;
    if (checkDiscovery) {
      discoveryCheck = t;
      got = recv(mdns, buf, sizeof(buf), 0);
    }
    if ((got >= 12 && !(buf[2] & 0x80) && t - lastAd > 250) ||
        t - lastAd > 30000) {
      announce(120);
      lastAd = t;
    }
    if (peer >= 0 && (drop || stream.failure != StreamSession::None)) {
      logMessage("Session ended: reason=%d", stream.failure.load());
      disconnect();
    }
    if (video.decodedFrames > 0 && t - hardwareResyncAt > 10000)
      hardwareResyncs = 0;
    if (peer >= 0 && (stream.resync || stream.reconfigure)) {
      stream.stop();
      bool formatChange = stream.reconfigure;
      bool cpu = softwareOnly || !video.hardwareAvailable;
      if (!formatChange) {
        cpu = cpu || ++hardwareResyncs >= 2;
        hardwareResyncAt = t;
      }
      logMessage("Decoder reset: %s / %s",
                 formatChange ? "stream format changed" : "decode recovery",
                 cpu ? "CPU" : "NVDEC");
      if (!video.reset(cpu, true))
        disconnect();
      else {
        video.connected = true;
        if (video.maxFrameRate() != offeredRate) {
          offeredRate = video.maxFrameRate();
          queue(hello());
        }
        queue({{"type", "kf"}});
        stream.start(peer, false);
      }
    }
    int incoming = checkDiscovery ? accept(listener, nullptr, nullptr) : -1;
    if (incoming >= 0 && peer >= 0) {
      // An accidental second connection must not evict a working display.
      close(incoming);
      incoming = -1;
    }
    if (incoming >= 0) {
      disconnect();
      peer = incoming;
      if (companionAvailable) {
        sockaddr_in sender{};
        socklen_t senderSize = sizeof(sender);
        if (getpeername(peer, reinterpret_cast<sockaddr *>(&sender),
                        &senderSize) == 0) {
          char code[7];
          snprintf(code, sizeof(code), "%06u",
                   unsigned(randomGet64() % 1000000));
          video.inputPairCode = code;
          companion.begin(
              sender.sin_addr, code,
              {{"type", "ready"}, {"device", device::name}, {"id", id}});
        }
      }
      lastActivityReport = t - 30000;
      nonblock(peer);
      setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
      if (!video.reset(softwareOnly)) {
        disconnect();
        status("H.264 decoder initialization failed.");
        continue;
      }
      queue(hello());
      offeredRate = video.maxFrameRate();
      hardwareResyncs = 0;
      hardwareResyncAt = t;
      video.connected = true;
      statsTime = t;
      stream.start(peer);
      status("Connected. Waiting for H.264 video...");
    }
    if (peer >= 0) {
      // Receiving video is user activity even without controller input.
      // The native idle service suppresses dimming and automatic sleep; no
      // persistent console power settings are changed.
      if (t - lastActivityReport >= 30000) {
        Result activityResult = appletReportUserIsActive();
        if (R_FAILED(activityResult))
          logMessage("Keep-awake activity report failed: %08x", activityResult);
        lastActivityReport = t;
      }
      for (auto &j : stream.takeControls()) {
        auto type = j["type"].get<std::string>();
        if (type == "cursorImg")
          video.cursorImage.load(j);
        if (type == "cursor") {
          if (j.contains("v") && j["v"].is_boolean())
            cursor = j["v"].get<bool>();
          else if (j.contains("v") && j["v"].is_number())
            cursor = j["v"].get<double>() != 0;
          if (j.contains("x") && j["x"].is_number())
            cx = std::clamp(j["x"].get<double>(), 0.0, 1.0);
          if (j.contains("y") && j["y"].is_number())
            cy = std::clamp(j["y"].get<double>(), 0.0, 1.0);
        }
        if (type == "streamConfig") {
          logMessage("Sender stream configuration: %s", j.dump().c_str());
          status("Video stream connected");
        }
      }
      video.inputConnected = companion.connected();
      if (video.inputConnected != companionMode) {
        for (auto &event : controllerPointer.cancel())
          queueController(event);
        if (touching) {
          queueController({{"type", "touch"},
                           {"phase", "cancelled"},
                           {"x", tx},
                           {"y", ty}});
          touching = false;
          touchArmed = false;
        }
        companionMode = video.inputConnected;
      }
      bool keyboardUsed = false;
      if (foreground && video.ready && !video.menuOpen &&
          (down & HidNpadButton_X)) {
        for (auto &event : controllerPointer.cancel())
          queueController(event);
        if (touching) {
          queueController({{"type", "touch"},
                           {"phase", "cancelled"},
                           {"x", tx},
                           {"y", ty}});
          touching = false;
        }
        if (companionMode) {
          keyboardUsed = true;
          companion.queue({{"type", "release"}});
          SwkbdConfig keyboard{};
          Result result = swkbdCreate(&keyboard, 0);
          if (R_SUCCEEDED(result)) {
            swkbdConfigMakePresetDefault(&keyboard);
            swkbdConfigSetType(&keyboard, SwkbdType_All);
            swkbdConfigSetStringLenMax(&keyboard, 1024);
            swkbdConfigSetHeaderText(
                &keyboard, locale::text(locale::KeyboardTitle, video.language));
            swkbdConfigSetGuideText(
                &keyboard, locale::text(locale::KeyboardGuide, video.language));
            char text[4097]{};
            result = swkbdShow(&keyboard, text, sizeof(text));
            if (R_SUCCEEDED(result) && text[0] && companion.connected())
              companion.queue({{"type", "text"}, {"text", text}});
            swkbdClose(&keyboard);
          }
          lastControllerTime = now();
          foreground = appletGetFocusState() == AppletFocusState_InFocus;
          touch = {};
          padUpdate(&pad);
          logMessage("Keyboard completed: %08x", result);
        } else {
          video.menuOpen = true;
          video.focus = 0;
        }
      }
      if (touch.count == 0)
        touchArmed = true;
      if (video.menuOpen)
        touchArmed = false;
      bool active = foreground && !video.menuOpen && touch.count > 0 &&
                    video.ready && touchArmed;
      const auto leftStick = padGetStickPos(&pad, 0);
      const auto rightStick = padGetStickPos(&pad, 1);
      const u64 held = foreground ? padGetButtons(&pad) : 0;
      for (auto &event : controllerPointer.update(
               foreground && video.ready && !video.menuOpen && !active &&
                   !touching && !keyboardUsed,
               {leftStick.x / 32767.0, leftStick.y / 32767.0},
               {rightStick.x / 32767.0, rightStick.y / 32767.0},
               (held & HidNpadButton_ZR) != 0, controllerSeconds, cx, cy,
               video.rect.w, video.rect.h,
               companionMode && (held & HidNpadButton_ZL) != 0))
        queueController(event);
      double x = tx, y = ty;
      if (active) {
        x = std::clamp((double(touch.touches[0].x) - video.rect.x) /
                           video.rect.w,
                       0.0, 1.0);
        y = std::clamp((double(touch.touches[0].y) - video.rect.y) /
                           video.rect.h,
                       0.0, 1.0);
      }
      if (active || touching) {
        if (active != touching || x != tx || y != ty)
          queueController(
              {{"type", "touch"},
               {"phase", active ? (touching ? "moved" : "began")
                                : (!foreground || video.menuOpen ? "cancelled"
                                                                 : "ended")},
               {"x", x},
               {"y", y}});
        tx = x;
        ty = y;
        touching = active;
      }
      if (video.maxFrameRate() != offeredRate) {
        offeredRate = video.maxFrameRate();
        queue(hello());
        logMessage("Video capability changed to %d FPS", offeredRate);
      }
      if (t - statsTime >= 5000) {
        auto measured = video.meter.sample(now());
        queue({{"type", "stats"},
               {"transport", "WiFi"},
               {"codec", "H.264"},
               {"fps", measured.fps},
               {"mbps", measured.mbps},
               {"e2e50", measured.delayMs},
               {"e2e95", measured.delayP95},
               {"rtt", measured.rttMs},
               {"dec50", measured.decodeMs},
               {"int50", measured.intervalP50},
               {"int95", measured.intervalP95},
               {"offsetKnown", measured.offsetKnown},
               {"idle", measured.idle}});
        logMessage("Perf %s fps=%.1f delay=%.1f rtt=%.1f decode=%.1f "
                   "upload=%.1f vic=%.1f texture=%.1f present=%.1f local=%.1f "
                   "replaced=%llu "
                   "macCap=%d macEncDrops=%d macNetDrops=%d pending=%d",
                   video.hardwareActive ? "NVDEC" : "CPU", measured.fps,
                   measured.delayMs, measured.rttMs, measured.decodeMs,
                   measured.uploadMs, measured.transferMs, measured.textureMs,
                   measured.presentMs, measured.localMs,
                   (unsigned long long)measured.replaced, measured.capFps,
                   measured.macEncoderDrops, measured.macNetworkDrops,
                   measured.macPending);
        statsTime = t;
      }
    }
    if (foreground)
      video.present(cursor, cx, cy);
    else
      svcSleepThread(16000000);
  }
finished:
  appletUnhook(&hookCookie);
  closeCleanly();
  announce(0);
  companion.stop();
  close(listener);
  close(mdns);
  if (cursorSocket >= 0)
    close(cursorSocket);
  nifmExit();
  socketExit();
  video.stop();
  consoleExit(nullptr);
  av_log_set_callback(av_log_default_callback);
  if (exitLocked)
    appletUnlockExit();
  if (sessionLog) {
    fclose(sessionLog);
    sessionLog = nullptr;
  }
  return 0;
}
