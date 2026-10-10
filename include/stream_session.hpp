#pragma once
#include "h264_stream.hpp"
#include <pthread.h>
// Included after Video: socket and codec belong to this worker, SDL stays on
// main.
class StreamSession {
  Video &video;
  outbox::Queue &output;
  std::mutex outputMutex, controlsMutex;
  std::deque<json> controls;
  wire::Framer framer;
  pthread_t worker{};
  bool workerStarted = false;
  std::atomic<bool> running{false};
  int peer = -1, cursorSocket = -1;
  in_addr senderAddress{};
  uint16_t cursorFlow = 0;
  uint64_t cursorSequence = 0;
  std::vector<uint16_t> retiredFlows;
  double lastPing = 0, lastPingWall = 0, lastKeyframe = 0, started = 0;
  stability::Recovery recovery;
  h264::Parameters parameters;
  bool waitingForKeyframe = true;
  static int integer(const json &j, const char *key, int fallback) {
    if (!j.contains(key) || !j[key].is_number())
      return fallback;
    return int(std::clamp(j[key].get<double>(), 0.0, 2147483647.0));
  }
  void control(json j) {
    const auto type = j["type"].get<std::string>();
    if (type == "pong") {
      if (j.contains("t") && j["t"].is_number() && j.contains("mt") &&
          j["mt"].is_number()) {
        double t = j["t"].get<double>();
        if (std::abs(t - lastPingWall) < .01)
          video.meter.pong(t, wallTime(), j["mt"].get<double>());
      }
      return;
    }
    if (type == "ping") {
      auto s = video.meter.sample(now());
      video.meter.sender(
          integer(j, "capFps", s.capFps),
          integer(j, "encDrops", integer(j, "drops", s.macEncoderDrops)),
          integer(j, "netDrops", s.macNetworkDrops),
          integer(j, "pending", s.macPending));
      return;
    }
    if (type == "closing") {
      failure = Closed;
      return;
    }
    if (type == "updateRequired" ||
        (type == "streamConfig" && j.contains("codec") &&
         j["codec"] != "h264")) {
      failure = Protocol;
      return;
    }
    if (type == "cursor" && j.contains("s") && j["s"].is_number_unsigned()) {
      auto sequence = j["s"].get<uint64_t>();
      if (sequence <= cursorSequence)
        return;
      cursorSequence = sequence;
    }
    if (type != "cursor" && type != "cursorImg" && type != "streamConfig" &&
        type != "welcome")
      return;
    std::lock_guard<std::mutex> lock(controlsMutex);
    // Cursor events are state: replace older queued values, never queue
    // latency.
    if (type == "cursor" || type == "cursorImg") {
      for (auto it = controls.begin(); it != controls.end(); ++it) {
        if ((*it)["type"] == type) {
          controls.erase(it);
          break;
        }
      }
    }
    if (controls.size() >= 128) {
      failure = Protocol;
      return;
    }
    controls.push_back(std::move(j));
  }
  void payload(const std::vector<uint8_t> &p) {
    if (!running || failure != None || resync || reconfigure)
      return;
    if (wire::control(p)) {
      if (!wire::safeJsonDepth(p))
        return;
      auto j = json::parse(p.begin(), p.end(), nullptr, false);
      if (j.is_object() && j.contains("type") && j["type"].is_string())
        control(std::move(j));
      return;
    }
    if (parameters.changed(p)) {
      reconfigure = true;
      return;
    }
    if (waitingForKeyframe) {
      if (!h264::Parameters::idr(p)) {
        if (now() - lastKeyframe > 1000) {
          queue({{"type", "kf"}});
          lastKeyframe = now();
        }
        return;
      }
      waitingForKeyframe = false;
    }
    uint64_t before = video.decodedFrames;
    if (video.decode(p, lastRx.load())) {
      if (video.decodedFrames > before)
        recovery.recovered();
    } else {
      auto action = recovery.failed(video.hardwareAvailable);
      if (action == stability::Recovery::Software)
        resync = true;
      if (action == stability::Recovery::Disconnect)
        failure = Decode;
      if (now() - lastKeyframe > 1000) {
        queue({{"type", "kf"}});
        lastKeyframe = now();
      }
    }
  }
  void cursorDatagram() {
    uint8_t bytes[2048];
    sockaddr_in source{};
    socklen_t size = sizeof(source);
    for (int i = 0; i < 64; ++i) {
      int n = recvfrom(cursorSocket, bytes, sizeof(bytes), 0,
                       reinterpret_cast<sockaddr *>(&source), &size);
      if (n <= 0)
        break;
      if (n >= int(sizeof(bytes)) ||
          source.sin_addr.s_addr != senderAddress.s_addr)
        continue;
      std::vector<uint8_t> data(bytes, bytes + n);
      if (!wire::safeJsonDepth(data))
        continue;
      auto j = json::parse(data.begin(), data.end(), nullptr, false);
      if (!j.is_object() || !j.contains("type") || j["type"] != "cursor" ||
          !j.contains("s") || !j["s"].is_number_unsigned() ||
          j["s"].get<uint64_t>() == 0)
        continue;
      bool fresh = source.sin_port != cursorFlow;
      if (fresh) {
        if (std::find(retiredFlows.begin(), retiredFlows.end(),
                      source.sin_port) != retiredFlows.end())
          continue;
        if (cursorFlow)
          retiredFlows.push_back(cursorFlow);
        if (retiredFlows.size() > 16)
          retiredFlows.erase(retiredFlows.begin());
        cursorFlow = source.sin_port;
        cursorSequence = 0;
      }
      if (j["s"].get<uint64_t>() <= cursorSequence)
        continue;
      control(std::move(j));
      if (fresh)
        queue({{"type", "cursorAck"}});
    }
  }
  void loop() {
#ifdef __SWITCH__
    // Prefer a different allowed CPU core from the UI, respecting title
    // affinity.
    s32 preferred = -1;
    u64 mask = 0;
    if (R_SUCCEEDED(
            svcGetThreadCoreMask(&preferred, &mask, CUR_THREAD_HANDLE))) {
      int ordinal = 1;
      for (s32 core = 0; core < 4; ++core)
        if (mask & (1ULL << core)) {
          if (ordinal-- == 0) {
            svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, u32(mask));
            break;
          }
        }
    }
#endif
    std::vector<uint8_t> buffer(1 << 18);
    while (running && failure == None && !resync && !reconfigure) {
      double t = now();
      if (t - lastPing >= 2000) {
        lastPingWall = wallTime();
        queue({{"type", "ping"}, {"t", lastPingWall}});
        lastPing = t;
      }
      {
        std::lock_guard<std::mutex> lock(outputMutex);
        if (!output.pump([&](const uint8_t *p, size_t n) {
              return send(peer, p, n, 0);
            }))
          failure = Socket;
      }
      pollfd sockets[2] = {{peer, POLLIN, 0}, {cursorSocket, POLLIN, 0}};
      int ready = poll(sockets, cursorSocket >= 0 ? 2 : 1, 4);
      if (ready < 0 && errno != EINTR) {
        failure = Socket;
        break;
      }
      if (ready > 0 && (sockets[1].revents & POLLIN))
        cursorDatagram();
      if (ready > 0 && sockets[0].revents) {
        // recv drains data even with HUP: a final closing control is not lost.
        int n = recv(peer, buffer.data(), buffer.size(), 0);
        if (n == 0)
          failure = Closed;
        else if (n < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            failure = Socket;
        } else {
          lastRx = now();
          if (!framer.feed(buffer.data(), n,
                           [&](const std::vector<uint8_t> &p) { payload(p); }))
            failure = Protocol;
        }
      }
      if (now() - lastRx.load() > 5000)
        failure = Timeout;
      if (video.decodedFrames == 0 && now() - started > 15000)
        failure = NoVideo;
    }
  }

public:
  enum Failure {
    None,
    Closed,
    Socket,
    Timeout,
    Protocol,
    Decode,
    NoVideo,
    Backpressure,
    ThreadError
  };
  std::atomic<int> failure{None};
  std::atomic<bool> resync{false}, reconfigure{false};
  std::atomic<double> lastRx{0};
  StreamSession(Video &v, outbox::Queue &q, int udp = -1)
      : video(v), output(q), cursorSocket(udp) {}
  ~StreamSession() { stop(); }
  void start(int fd, bool fresh = true) {
    stop();
    peer = fd;
    if (fresh) {
      framer.reset();
      parameters.reset();
      {
        std::lock_guard<std::mutex> lock(controlsMutex);
        controls.clear();
      }
      cursorFlow = 0;
      cursorSequence = 0;
      retiredFlows.clear();
      sockaddr_in remote{};
      socklen_t size = sizeof(remote);
      if (getpeername(fd, reinterpret_cast<sockaddr *>(&remote), &size) == 0)
        senderAddress = remote.sin_addr;
      if (cursorSocket >= 0) {
        uint8_t old[2048];
        while (recv(cursorSocket, old, sizeof(old), 0) > 0) {
        }
      }
    }
    failure = None;
    resync = false;
    reconfigure = false;
    recovery.recovered();
    waitingForKeyframe = true;
    started = lastRx = now();
    lastPing = 0;
    lastKeyframe = 0;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) {
      failure = ThreadError;
      return;
    }
    if (pthread_attr_setstacksize(&attributes, 1024 * 1024) != 0) {
      pthread_attr_destroy(&attributes);
      failure = ThreadError;
      return;
    }
    running = true;
    int error = pthread_create(
        &worker, &attributes,
        [](void *context) -> void * {
          static_cast<StreamSession *>(context)->loop();
          return nullptr;
        },
        this);
    pthread_attr_destroy(&attributes);
    workerStarted = error == 0;
    if (!workerStarted) {
      running = false;
      failure = ThreadError;
    }
  }
  void stop() {
    running = false;
    if (workerStarted) {
      pthread_join(worker, nullptr);
      workerStarted = false;
    }
  }
  bool queue(json j) {
    if (j.contains("type") && j["type"] == "touch") {
      double t = video.meter.senderTime(wallTime());
      if (t > 0)
        j["t"] = t;
    }
    std::lock_guard<std::mutex> lock(outputMutex);
    if (output.size() >= 128) {
      failure = Backpressure;
      return false;
    }
    output.append(j.dump());
    return true;
  }
  std::deque<json> takeControls() {
    std::lock_guard<std::mutex> lock(controlsMutex);
    std::deque<json> result;
    result.swap(controls);
    return result;
  }
};
