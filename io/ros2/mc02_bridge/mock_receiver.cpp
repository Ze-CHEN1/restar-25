#include "mc02_serial_bridge/protocol.hpp"

#include <algorithm>
#include <memory>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

class MockReceiver : public rclcpp::Node
{
public:
  MockReceiver() : Node("mc02_mock_receiver")
  {
    const auto topic = declare_parameter<std::string>("frame_topic", "bridge/frame");
    const auto output_file = declare_parameter<std::string>("output_file", "");
    if (!output_file.empty()) {
      output_.open(output_file, std::ios::binary | std::ios::trunc);
      if (!output_) throw std::runtime_error("Cannot open Mock output_file");

    }
    subscription_ = create_subscription<std_msgs::msg::UInt8MultiArray>(
      topic, rclcpp::QoS(rclcpp::KeepLast(10)).reliable().durability_volatile(),
      [this](std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
        const auto frame = mc02_serial_bridge::decode(msg->data.data(), msg->data.size());
        if (!frame) {
          ++invalid_;
          RCLCPP_WARN(get_logger(), "invalid frame count=%lu", static_cast<unsigned long>(invalid_));
          return;
        }
        ++valid_;
        if (output_.is_open()) {
          std::array<uint8_t, mc02_serial_bridge::kFrameSize> copy;
          std::copy(msg->data.begin(), msg->data.end(), copy.begin());
          {
            std::lock_guard<std::mutex> lock(mutex_);
            if (queue_.size() < 256) queue_.push_back(copy);
            else ++dropped_;  // Bounded diagnostic recording, never block the ROS callback on disk.
          }
          cv_.notify_one();
        }
        if (valid_ == 1 || valid_ % 50 == 0) {
          const auto & p = frame->payload;
          RCLCPP_INFO(
            get_logger(),
            "decoded=%lu seq=%u nav_valid=%u gimbal_valid=%u control=%u "
            "vx=%.4f vy=%.4f wz=%.4f yaw=%.4f pitch=%.4f",
            static_cast<unsigned long>(valid_), frame->sequence, p.nav_valid, p.gimbal_valid,
            p.control_enabled, p.vx, p.vy, p.wz, p.yaw, p.pitch);
        }
      });
    if (output_.is_open()) writer_ = std::thread([this] { write_loop(); });
  }

  ~MockReceiver() override
  {
    { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
    cv_.notify_one();
    if (writer_.joinable()) writer_.join();
    RCLCPP_INFO(get_logger(), "receiver totals decoded=%lu invalid=%lu recorded=%lu dropped=%lu io_error=%d",
      static_cast<unsigned long>(valid_), static_cast<unsigned long>(invalid_),
      static_cast<unsigned long>(written_), static_cast<unsigned long>(dropped_), io_error_.load());
  }
  bool io_error() const { return io_error_; }
  void finish()
  {
    { std::lock_guard<std::mutex> lock(mutex_); done_ = true; }
    cv_.notify_one();
    if (writer_.joinable()) writer_.join();
  }
private:
  void write_loop()
  {
    for (;;) {
      std::array<uint8_t, mc02_serial_bridge::kFrameSize> bytes;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_ || !queue_.empty(); });
        if (queue_.empty() && done_) break;
        bytes = queue_.front(); queue_.pop_front();
      }
      output_.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      if (!output_) io_error_ = true;
      else ++written_;
    }
    output_.flush();
    if (!output_) io_error_ = true;
  }
  std::ofstream output_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::array<uint8_t, mc02_serial_bridge::kFrameSize>> queue_;
  bool done_{};
  std::thread writer_;
  std::atomic<bool> io_error_{};
  uint64_t written_{}, dropped_{};
  uint64_t valid_{}, invalid_{};
  rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr subscription_;
};

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto receiver = std::make_shared<MockReceiver>();
    rclcpp::spin(receiver);
    receiver->finish();
    if (receiver->io_error()) result = 1;
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("mc02_mock_receiver"), "%s", error.what()); result = 1;
  }
  rclcpp::shutdown();
  return result;
}
