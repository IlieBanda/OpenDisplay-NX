#pragma once
#include "performance.hpp"
#include <mutex>
extern "C" {
#include <libavutil/frame.h>
}
// Exactly one pending decoded image; compressed H.264 packets are never
// skipped.
class LatestFrame {
  mutable std::mutex mutex;
  AVFrame *pending = av_frame_alloc();
  perf::Stamp stamp;
  bool available = false;

public:
  ~LatestFrame() { av_frame_free(&pending); }
  bool valid() const { return pending != nullptr; }
  bool publish(AVFrame *frame, const perf::Stamp &meta, bool &replaced) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!pending)
      return false;
    replaced = available;
    av_frame_unref(pending);
    if (av_frame_ref(pending, frame) < 0) {
      available = false;
      return false;
    }
    stamp = meta;
    available = true;
    return true;
  }
  bool take(AVFrame *frame, perf::Stamp &meta) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!available)
      return false;
    av_frame_unref(frame);
    av_frame_move_ref(frame, pending);
    meta = stamp;
    available = false;
    return true;
  }
  void clear() {
    std::lock_guard<std::mutex> lock(mutex);
    if (pending)
      av_frame_unref(pending);
    available = false;
  }
};
