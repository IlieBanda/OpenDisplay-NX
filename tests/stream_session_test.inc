#include <functional>
#include <iostream>
static void writePayload(int fd, const std::vector<uint8_t> &p) {
  uint32_t len = htonl(p.size());
  std::vector<uint8_t> wireBytes(4 + p.size());
  memcpy(wireBytes.data(), &len, 4);
  memcpy(wireBytes.data() + 4, p.data(), p.size());
  size_t offset = 0;
  while (offset < wireBytes.size()) {
    // Deliberately fragment frame headers and bodies on a real TCP stream.
    int n = send(fd, wireBytes.data() + offset,
                 std::min(size_t(3333), wireBytes.size() - offset), 0);
    assert(n > 0);
    offset += n;
  }
}
static void writeControl(int fd, const json &j) {
  auto s = j.dump();
  writePayload(fd, std::vector<uint8_t>(s.begin(), s.end()));
}
static bool until(std::function<bool()> predicate, int timeout = 1000) {
  double end = now() + timeout;
  while (now() < end) {
    if (predicate())
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return predicate();
}
static void connectPair(int &server, int &client) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  assert(listener >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(listener, reinterpret_cast<sockaddr *>(&address),
              sizeof(address)) == 0);
  socklen_t size = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) ==
         0);
  assert(listen(listener, 1) == 0);
  client = socket(AF_INET, SOCK_STREAM, 0);
  assert(client >= 0);
  assert(connect(client, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) == 0);
  server = accept(listener, nullptr, nullptr);
  assert(server >= 0);
  close(listener);
  assert(fcntl(server, F_SETFL, O_NONBLOCK) == 0);
}
static void stressSessions(Video &video,
                           const std::vector<std::vector<uint8_t>> &packets) {
  outbox::Queue outgoing;
  int udp = socket(AF_INET, SOCK_DGRAM, 0);
  assert(udp >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(udp, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
         0);
  socklen_t size = sizeof(address);
  assert(getsockname(udp, reinterpret_cast<sockaddr *>(&address), &size) == 0);
  assert(fcntl(udp, F_SETFL, O_NONBLOCK) == 0);
  StreamSession session(video, outgoing, udp);
  for (int i = 0; i < 25; ++i) {
    int server, client;
    connectPair(server, client);
    assert(video.reset(true));
    session.queue({{"type", "hello"}});
    session.start(server);
    for (auto &p : packets)
      writePayload(client, p);
    assert(until([&] { return video.decodedFrames == 6; }));
    assert(!video.ready); // network + decoder don't need a UI refresh
    video.present(false, 0, 0);
    assert(video.ready && video.displayedFrames == 1);
    // Sender close is detected independently of rendering.
    writeControl(client, {{"type", "closing"}});
    assert(until([&] { return session.failure == StreamSession::Closed; }));
    session.stop();
    close(server);
    close(client);
    outgoing.clear();
    video.clearStream();
  }
  {
    int server, client;
    connectPair(server, client);
    assert(video.reset(true));
    session.queue({{"type", "hello"}});
    session.start(server);
    writePayload(client, packets.front());
    assert(until([&] { return video.decodedFrames == 1; }));
    video.present(false, 0, 0);
    assert(video.ready);
    std::ifstream input("build/video-small.h264", std::ios::binary);
    std::vector<uint8_t> small((std::istreambuf_iterator<char>(input)), {});
    writePayload(client, small);
    assert(until([&] { return bool(session.reconfigure); }));
    session.stop();
    assert(video.reset(true, true));
    assert(video.ready);
    session.start(server, false);
    session.queue({{"type", "kf"}});
    writePayload(client, small);
    assert(until([&] { return video.decodedFrames == 1; }));
    video.present(false, 0, 0);
    assert(video.ready);
    session.stop();
    close(server);
    close(client);
    outgoing.clear();
    video.clearStream();
  }
  int server, client;
  connectPair(server, client);
  assert(video.reset(true));
  session.queue({{"type", "hello"}});
  session.start(server);
  for (auto &p : packets)
    writePayload(client, p);
  assert(until([&] { return video.decodedFrames == 6; }));
  std::atomic<bool> respond{true};
  std::atomic<int> pings{0}, acks{0};
  std::thread responder([&] {
    wire::Framer framer;
    uint8_t bytes[4096];
    while (respond) {
      pollfd p{client, POLLIN, 0};
      if (poll(&p, 1, 10) <= 0)
        continue;
      int n = recv(client, bytes, sizeof(bytes), 0);
      if (n <= 0)
        break;
      assert(framer.feed(bytes, n, [&](const auto &payload) {
        auto j = json::parse(payload.begin(), payload.end());
        if (j["type"] == "ping") {
          ++pings;
          writeControl(client,
                       {{"type", "pong"}, {"t", j["t"]}, {"mt", wallTime()}});
        }
        if (j["type"] == "cursorAck")
          ++acks;
      }));
    }
  });
  // UI deliberately does no work: heartbeats and UDP cursor must still run.
  std::this_thread::sleep_for(std::chrono::milliseconds(2200));
  assert(pings >= 2 && session.failure == StreamSession::None);
  assert(video.meter.sample(now()).offsetKnown);
  int sender = socket(AF_INET, SOCK_DGRAM, 0);
  assert(sender >= 0);
  auto datagram = [&](uint64_t seq, double x) {
    auto s =
        json({{"type", "cursor"}, {"s", seq}, {"v", 1}, {"x", x}, {"y", .5}})
            .dump();
    assert(sendto(sender, s.data(), s.size(), 0,
                  reinterpret_cast<sockaddr *>(&address),
                  sizeof(address)) == int(s.size()));
  };
  datagram(2, .9);
  assert(until([&] { return acks == 1; }));
  datagram(1, .1); // late packet must not replace the newest cursor
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  auto controls = session.takeControls();
  assert(controls.size() == 1 && controls.front()["x"] == .9);
  // Return from HOME with a static sender: the retained latest frame appears.
  video.present(false, 0, 0);
  assert(video.ready && video.displayedFrames == 1);
  for (int i = 0; i < 5; ++i)
    video.present(false, 0, 0);
  assert(video.displayedFrames == 1); // never count refreshes as video FPS
  respond = false;
  responder.join();
  close(sender);
  // Half-closed socket, not only the explicit closing control, is detected.
  shutdown(client, SHUT_WR);
  assert(until([&] { return session.failure == StreamSession::Closed; }));
  session.stop();
  close(server);
  close(client);
  close(udp);
  outgoing.clear();
  video.clearStream();
  std::cout << "OK: actual socket worker, 27 reconnects, quality/resolution "
               "change, fragmented H.264, heartbeat during stalled UI, UDP "
               "cursor ack/order, HOME retained frame, remote close\n";
}
