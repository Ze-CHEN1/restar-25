#ifndef MC02_SERIAL_BRIDGE__SERIAL_SENDER_HPP_
#define MC02_SERIAL_BRIDGE__SERIAL_SENDER_HPP_

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <sys/types.h>

#include "mc02_serial_bridge/protocol.hpp"

namespace mc02_serial_bridge
{
// One worker owns the fd. ROS only overwrites a single pending frame; no I/O in submit().
class SerialSender
{
public:
  using Frame = std::array<uint8_t, kFrameSize>;
  using Writer = std::function<ssize_t(int, const void *, size_t)>;
  SerialSender(std::string device, bool motion_enabled,
    std::chrono::milliseconds reconnect = std::chrono::milliseconds(250),
    std::chrono::milliseconds write_timeout = std::chrono::milliseconds(50), Writer writer = {});
  ~SerialSender();
  SerialSender(const SerialSender &) = delete;
  SerialSender & operator=(const SerialSender &) = delete;

  void submit(const Frame & frame, const Frame & disabled_frame);
  bool connected() const { return connected_.load(); }
  bool motion_enabled() const { return motion_enabled_.load(); }
  uint64_t written() const { return written_.load(); }  // Complete frames accepted by the OS, not MCU ACKs.
  uint64_t failures() const { return failures_.load(); }
  int last_error() const { return last_error_.load(); }

private:
  struct Pending
  {
    Frame frame, disabled_frame;
    std::chrono::steady_clock::time_point created_at;
  };
  void run();
  int open_port();
  bool write_frame(int fd, const Frame & frame);
  void failed(int error);
  bool stopping() const { return stopping_.load(); }

  const std::string device_;
  const std::chrono::milliseconds reconnect_, write_timeout_;
  Writer writer_;
  std::atomic<bool> stopping_{false}, connected_{false}, motion_enabled_;
  std::atomic<uint64_t> written_{0}, failures_{0};
  std::atomic<int> last_error_{0};
  std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<Pending> pending_;
  std::thread worker_;  // Declared last; all state exists before the worker starts.
};
}  // namespace mc02_serial_bridge
#endif
