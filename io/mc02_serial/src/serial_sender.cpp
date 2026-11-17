#include "mc02_serial_bridge/serial_sender.hpp"

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace mc02_serial_bridge
{
SerialSender::SerialSender(std::string device, bool motion_enabled,
  std::chrono::milliseconds reconnect, std::chrono::milliseconds write_timeout, Writer writer)
: device_(std::move(device)), reconnect_(reconnect), write_timeout_(write_timeout),
  writer_(writer ? std::move(writer) : Writer(::write)), motion_enabled_(motion_enabled)
{
  if (device_.empty() || reconnect_.count() < 1 || write_timeout_.count() < 1)
    throw std::invalid_argument("serial device and positive reconnect/write timeout required");
  worker_ = std::thread(&SerialSender::run, this);
}
SerialSender::~SerialSender()
{
  stopping_ = true;
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}
void SerialSender::submit(const Frame & frame, const Frame & disabled_frame)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = Pending{frame, disabled_frame, std::chrono::steady_clock::now()};
  }
  cv_.notify_one();
}
void SerialSender::failed(int error)
{
  motion_enabled_ = false;  // Latched until process restart with explicit startup enable.
  connected_ = false;
  last_error_ = error;
  ++failures_;
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.reset();
}
int SerialSender::open_port()
{
  int fd = ::open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return -1;
  termios config{};
  if (::ioctl(fd, TIOCEXCL) < 0) { const int saved=errno; ::close(fd); errno=saved; return -1; }
  const auto close_error = [&] {
    const int saved = errno; ::ioctl(fd, TIOCNXCL); ::close(fd); errno = saved; return -1;
  };
  if (::tcgetattr(fd, &config) < 0) return close_error();
  ::cfmakeraw(&config);
  config.c_cflag |= CLOCAL | CREAD;
  config.c_cflag &= ~(CRTSCTS | CSTOPB | PARENB | CSIZE);
  config.c_cflag |= CS8;
  config.c_cc[VMIN] = 0; config.c_cc[VTIME] = 0;
  // USB CDC usually ignores baud, but configure a conventional 115200 8N1 explicitly.
  if (::cfsetispeed(&config, B115200) < 0 || ::cfsetospeed(&config, B115200) < 0 ||
    ::tcsetattr(fd, TCSANOW, &config) < 0 || ::tcflush(fd, TCIOFLUSH) < 0) return close_error();
  return fd;
}
bool SerialSender::write_frame(int fd, const Frame & frame)
{
  size_t offset = 0;
  const auto deadline = std::chrono::steady_clock::now() + write_timeout_;
  while (!stopping() && offset < frame.size()) {
    if (std::chrono::steady_clock::now() >= deadline) { errno = ETIMEDOUT; return false; }
    const ssize_t n = writer_(fd, frame.data() + offset, frame.size() - offset);
    if (n > 0) { offset += static_cast<size_t>(n); continue; }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
    pollfd pfd{fd, POLLOUT, 0};
    const int result = ::poll(&pfd, 1, 5);  // Bounded even on disconnect or process shutdown.
    if (result < 0 && errno != EINTR) return false;
    if (result > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
      errno = EIO; return false;
    }
  }
  return offset == frame.size();
}
void SerialSender::run()
{
  int fd = -1;
  while (!stopping()) {
    if (fd < 0) {
      fd = open_port();
      if (fd < 0) {
        failed(errno);
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, reconnect_, [this] { return stopping(); });
        continue;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();  // No replay of frames queued before this open/reconnect.
      }
      connected_ = true;
    }
    // Check disconnect even if no frame has arrived since the last iteration.
    pollfd check{fd, 0, 0};
    if (::poll(&check, 1, 0) > 0 && (check.revents & (POLLERR | POLLHUP | POLLNVAL))) {
      failed(EIO); ::ioctl(fd, TIOCNXCL); ::close(fd); fd = -1; continue;
    }
    std::optional<Pending> next;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(lock, std::chrono::milliseconds(10), [this] { return stopping() || pending_; });
      next.swap(pending_);
    }
    if (!next || stopping()) continue;
    if (std::chrono::steady_clock::now() - next->created_at > write_timeout_) continue;
    const auto & frame = motion_enabled() ? next->frame : next->disabled_frame;
    if (write_frame(fd, frame)) ++written_;
    else if (!stopping()) { failed(errno); ::ioctl(fd, TIOCNXCL); ::close(fd); fd = -1; }
  }
  if (fd >= 0) { ::ioctl(fd, TIOCNXCL); ::close(fd); }
  connected_ = false;
}
}  // namespace mc02_serial_bridge
