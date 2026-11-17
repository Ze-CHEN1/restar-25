#include "mc02_serial_bridge/serial_sender.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <termios.h>
#include <thread>
#include <vector>
#include <algorithm>
#include <unistd.h>
using namespace std::chrono_literals;
using namespace mc02_serial_bridge;

struct Pty { int master{-1}; std::string slave; };
Pty open_pty()
{
  Pty p; p.master = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  assert(p.master >= 0 && ::grantpt(p.master) == 0 && ::unlockpt(p.master) == 0);
  std::array<char, 256> name{}; assert(::ptsname_r(p.master, name.data(), name.size()) == 0);
  p.slave = name.data(); return p;
}
std::vector<DecodedFrame> read_frames(int fd, std::chrono::milliseconds duration)
{
  StreamDecoder decoder; std::vector<DecodedFrame> frames;
  const auto end = std::chrono::steady_clock::now() + duration;
  std::array<uint8_t,256> bytes;
  while (std::chrono::steady_clock::now() < end) {
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p,1,10) <= 0) continue;
    const auto n = ::read(fd, bytes.data(), bytes.size());
    if (n > 0) { auto next = decoder.feed(bytes.data(), n); frames.insert(frames.end(),next.begin(),next.end()); }
  }
  return frames;
}
int main()
{
  char directory[] = "/tmp/mc02-pty-XXXXXX"; assert(::mkdtemp(directory));
  const std::string link = std::string(directory) + "/cdc";
  Pty first = open_pty(); assert(::symlink(first.slave.c_str(), link.c_str()) == 0);
  const auto active = encode(ControlPayload{.4F,-.2F,.3F,.25F,-.1F,1,1,1,0}, 42);
  const auto disabled = encode(ControlPayload{}, 42);
  std::atomic<unsigned> calls{0};
  {
  SerialSender sender(link, true, 20ms, 100ms,
    [&calls](int fd, const void * data, size_t size) -> ssize_t {
      if (++calls % 7 == 1) { errno = EINTR; return -1; }
      return ::write(fd, data, std::min<size_t>(size, 5));  // Force partial writes.
    });
  const auto wait_connected = [&sender] {
    const auto until = std::chrono::steady_clock::now()+1s;
    while (!sender.connected() && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(2ms);
    assert(sender.connected());
  };
  wait_connected();
  for (unsigned i=0; i<6; ++i) { sender.submit(active, disabled); std::this_thread::sleep_for(20ms); }
  auto received = read_frames(first.master,150ms);
  assert(received.size() >= 3 && sender.written() >= 3 && calls > received.size());
  for (const auto & frame : received) assert(frame.payload.gimbal_valid == 1 && frame.payload.vx == .4F);

  // Disconnect. The sender must latch motion off, discard queued commands and reconnect disabled.
  ::close(first.master); first.master=-1;
  const auto failure_deadline=std::chrono::steady_clock::now()+1s;
  while (sender.motion_enabled() && std::chrono::steady_clock::now()<failure_deadline)
    std::this_thread::sleep_for(2ms);
  assert(!sender.motion_enabled());
  ::unlink(link.c_str());
  Pty second = open_pty(); assert(::symlink(second.slave.c_str(), link.c_str())==0);
  wait_connected();
  for (unsigned i=0; i<5; ++i) { sender.submit(active, disabled); std::this_thread::sleep_for(20ms); }
  received = read_frames(second.master,150ms);
  assert(received.size() >= 2 && !sender.motion_enabled());
  for (const auto & frame : received) {
    assert(frame.payload.nav_valid == 0 && frame.payload.gimbal_valid == 0 &&
      frame.payload.control_enabled == 0 && frame.payload.vx == 0 && frame.payload.yaw == 0);
  }
  assert(calls > 10 && sender.written() >= 5);

  ::close(second.master); second.master=-1;
  } // Stop the first reconnect loop before testing a new sender.

  // Open failure retries; first successful open still stays motion-disabled.
  ::unlink(link.c_str());
  SerialSender retry(link, true, 20ms, 30ms);
  std::this_thread::sleep_for(80ms);
  assert(!retry.connected() && !retry.motion_enabled() && retry.failures() > 0);
  Pty third=open_pty(); assert(::symlink(third.slave.c_str(),link.c_str())==0);
  const auto third_deadline=std::chrono::steady_clock::now()+1s;
  while (!retry.connected() && std::chrono::steady_clock::now()<third_deadline)
    std::this_thread::sleep_for(2ms);
  assert(retry.connected() && !retry.motion_enabled());
  retry.submit(active,disabled);
  auto received=read_frames(third.master,100ms);
  assert(!received.empty());
  for (const auto & frame : received)
    assert(frame.payload.nav_valid==0 && frame.payload.gimbal_valid==0);

  // A perpetually backpressured nonblocking write must time out and shut down promptly.
  Pty fill_pty=open_pty();
  const auto stop_at=std::chrono::steady_clock::now();
  std::atomic<unsigned> backpressure_calls{0};
  {
  SerialSender blocked(fill_pty.slave,true,20ms,30ms,
    [&backpressure_calls](int, const void *, size_t) -> ssize_t { ++backpressure_calls; errno=EAGAIN; return -1; });
  const auto block_deadline=std::chrono::steady_clock::now()+1s;
  while (blocked.motion_enabled() && std::chrono::steady_clock::now()<block_deadline) {
    blocked.submit(active,disabled); std::this_thread::sleep_for(5ms);
  }
  assert(!blocked.motion_enabled() && blocked.failures()>0 && backpressure_calls>0);
  }  // Shutdown joins the bounded nonblocking worker.
  assert(std::chrono::steady_clock::now()-stop_at<200ms);
  ::close(fill_pty.master); ::close(third.master);
  ::unlink(link.c_str()); ::rmdir(directory);
  return 0;
}
