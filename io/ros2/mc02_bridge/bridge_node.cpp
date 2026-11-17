#include "mc02_serial_bridge/protocol.hpp"
#include "mc02_serial_bridge/state.hpp"
#include "mc02_serial_bridge/serial_sender.hpp"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

namespace mc02_serial_bridge
{
class BridgeNode : public rclcpp::Node
{
public:
  BridgeNode() : Node("mc02_serial_bridge")
  {
    const auto nav_topic = parameter<std::string>("nav_topic", "cmd_vel");
    const auto gimbal_topic = parameter<std::string>("gimbal_topic", "vision/gimbal_cmd");
    const auto target_topic = parameter<std::string>("target_topic", "vision/target");
    const auto frame_topic = parameter<std::string>("frame_topic", "bridge/frame");
    const auto transport = parameter<std::string>("transport", "mock");
    const auto serial_device = parameter<std::string>("serial_device", "/dev/ttyACM0");
    const auto serial_enabled = parameter<bool>("serial_motion_enabled", false);
    const auto reconnect_ms = parameter<int>("serial_reconnect_ms", 250);
    const auto write_timeout_ms = parameter<int>("serial_write_timeout_ms", 50);
    if (transport != "mock" && transport != "serial")
      throw std::invalid_argument("transport must be mock or serial");
    if (reconnect_ms < 1 || reconnect_ms > 60000 || write_timeout_ms < 1 || write_timeout_ms > 1000)
      throw std::invalid_argument("serial reconnect must be [1,60000] ms; write timeout [1,1000] ms");
    rate_ = parameter<double>("frame_rate_hz", 50.0);
    policy_.nav_timeout = Milliseconds(parameter<double>("nav_timeout_ms", 200.0));
    policy_.gimbal_timeout = Milliseconds(parameter<double>("gimbal_timeout_ms", 200.0));
    policy_.target_timeout = Milliseconds(parameter<double>("target_timeout_ms", 500.0));
    policy_.future_tolerance = Milliseconds(parameter<double>("future_tolerance_ms", 50.0));
    subscribe_target_ = parameter<bool>("subscribe_target", true);
    if (!std::isfinite(rate_) || rate_ < 1.0 || rate_ > 1000.0)
      throw std::invalid_argument("frame_rate_hz must be finite in [1,1000]");
    for (auto value : {policy_.nav_timeout, policy_.gimbal_timeout, policy_.target_timeout})
      if (!std::isfinite(value.count()) || value.count() < 1 || value.count() > 60000)
        throw std::invalid_argument("timeouts must be finite in [1,60000] ms");
    if (!std::isfinite(policy_.future_tolerance.count()) || policy_.future_tolerance.count() < 0 ||
      policy_.future_tolerance.count() > 1000)
      throw std::invalid_argument("future_tolerance_ms must be finite in [0,1000]");
    latest_ = std::make_unique<LatestState>(policy_);
    const auto nav_qos = qos("nav_reliability", "best_effort", 1);
    const auto gimbal_qos = qos("gimbal_reliability", "reliable", 1);
    const auto target_qos = qos("target_reliability", "best_effort", 5);
    nav_sub_ = create_subscription<geometry_msgs::msg::Twist>(nav_topic, nav_qos,
      [this](geometry_msgs::msg::Twist::ConstSharedPtr m) { latest_->receive_nav(*m); });
    gimbal_sub_ = create_subscription<robot_interfaces::msg::GimbalCommand>(gimbal_topic, gimbal_qos,
      [this](robot_interfaces::msg::GimbalCommand::ConstSharedPtr m) {
        latest_->receive_gimbal(*m, now().nanoseconds());
      });
    if (subscribe_target_) target_sub_ = create_subscription<robot_interfaces::msg::VisionTarget2D>(
      target_topic, target_qos, [this](robot_interfaces::msg::VisionTarget2D::ConstSharedPtr m) {
        latest_->receive_target(*m, now().nanoseconds());
      });
    if (transport == "mock") {
      frame_pub_ = create_publisher<std_msgs::msg::UInt8MultiArray>(frame_topic,
        rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile());
    } else {
      serial_ = std::make_unique<SerialSender>(serial_device, serial_enabled,
        std::chrono::milliseconds(reconnect_ms), std::chrono::milliseconds(write_timeout_ms));
    }
    diagnostic_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("bridge/status", 1);
    timer_ = create_wall_timer(std::chrono::duration<double>(1.0 / rate_),
      [this] { publish_frame(); });
    RCLCPP_INFO(get_logger(), "Binary bridge v1 transport=%s: nav=%s gimbal=%s target=%s rate=%.1fHz output=%s",
      transport.c_str(),
      nav_sub_->get_topic_name(), gimbal_sub_->get_topic_name(),
      subscribe_target_ ? target_sub_->get_topic_name() : "disabled", rate_,
      frame_pub_ ? frame_pub_->get_topic_name() : serial_device.c_str());
    RCLCPP_INFO(get_logger(), "QoS: nav=%s/1 gimbal=%s/1 target=%s/5; all Volatile",
      get_parameter("nav_reliability").as_string().c_str(),
      get_parameter("gimbal_reliability").as_string().c_str(),
      get_parameter("target_reliability").as_string().c_str());
  }
private:
  template<typename T> T parameter(const std::string & name, T value)
  {
    rcl_interfaces::msg::ParameterDescriptor descriptor;
    descriptor.read_only = true;  // Startup-only configuration, never accept ineffective live changes.
    return declare_parameter<T>(name, value, descriptor);
  }
  rclcpp::QoS qos(const std::string & name, const std::string & value, size_t depth)
  {
    const auto reliability = parameter<std::string>(name, value);
    auto result = rclcpp::QoS(rclcpp::KeepLast(depth)).durability_volatile();
    if (reliability == "reliable") return result.reliable();
    if (reliability == "best_effort") return result.best_effort();
    throw std::invalid_argument(name + " must be reliable or best_effort");
  }
  void publish_frame()
  {
    const auto s = latest_->snapshot();
    const auto steady_now = SteadyClock::now();
    const auto ros_now = now();
    const auto e = evaluate(s, policy_, steady_now, ros_now.nanoseconds());
    const auto sequence = sequence_++;
    const auto bytes = encode(e.payload, sequence);
    if (serial_) {
      // The sender can select the disabled frame atomically after any link failure.
      // This gate affects transport only; the original ROS data cache/TTL is unchanged.
      serial_->submit(bytes, encode(ControlPayload{}, sequence));
    } else {
      std_msgs::msg::UInt8MultiArray msg;
      msg.data.assign(bytes.begin(), bytes.end());
      frame_pub_->publish(msg);
    }
    ++frames_;
    if (steady_now - last_log_ < std::chrono::seconds(1)) return;
    last_log_ = steady_now;
    diagnostic_msgs::msg::DiagnosticArray diagnostic;
    diagnostic.header.stamp = ros_now;
    const auto status = [&](const char * name, const ChannelFreshness & state, Reason reason,
      size_t publishers) {
      diagnostic_msgs::msg::DiagnosticStatus result;
      result.name = name;
      result.hardware_id = serial_ ? "serial_no_mcu_ack" : "mock_only";
      result.level = reason == Reason::ok ? 0 : 1;
      result.message = reason_name(reason);
      const auto add = [&](const char * key, auto value) {
        diagnostic_msgs::msg::KeyValue kv; kv.key = key; kv.value = std::to_string(value);
        result.values.push_back(kv);
      };
      add("samples", state.samples); add("accepted_stamps", state.accepted_stamps);
      add("repeated_stamps", state.repeated_stamps); add("regressed_stamps", state.regressed_stamps);
      add("clock_resets", state.clock_resets); add("age_ms", state.age_ms(steady_now));
      add("publishers", publishers); add("frames", frames_);
      diagnostic.status.push_back(result);
    };
    status("navigation", s.nav_state, e.nav, nav_sub_->get_publisher_count());
    status("gimbal", s.gimbal_state, e.gimbal, gimbal_sub_->get_publisher_count());
    if (subscribe_target_) status("target", s.target_state, e.target, target_sub_->get_publisher_count());
    diagnostic_pub_->publish(diagnostic);
    if (serial_) {
      RCLCPP_INFO(get_logger(), "serial connected=%d motion_enabled=%d written=%lu failures=%lu errno=%d (no MCU ACK)",
        serial_->connected(), serial_->motion_enabled(), static_cast<unsigned long>(serial_->written()),
        static_cast<unsigned long>(serial_->failures()), serial_->last_error());
    }
    RCLCPP_INFO(get_logger(), "frames=%lu nav=%s gimbal=%s target=%s (diagnostic only)",
      static_cast<unsigned long>(frames_), reason_name(e.nav), reason_name(e.gimbal),
      subscribe_target_ ? reason_name(e.target) : "disabled");
  }
  Policy policy_;
  std::unique_ptr<SerialSender> serial_;
  std::unique_ptr<LatestState> latest_;
  uint16_t sequence_{};
  uint64_t frames_{};
  double rate_{};
  bool subscribe_target_{};
  SteadyClock::time_point last_log_{};
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr nav_sub_;
  rclcpp::Subscription<robot_interfaces::msg::GimbalCommand>::SharedPtr gimbal_sub_;
  rclcpp::Subscription<robot_interfaces::msg::VisionTarget2D>::SharedPtr target_sub_;
  rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr frame_pub_;
  rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostic_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};
}  // namespace mc02_serial_bridge
int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  int result = 0;
  try {
    auto node = std::make_shared<mc02_serial_bridge::BridgeNode>();
    rclcpp::spin(node);  // One owned main-thread executor, no detached worker or device I/O.
  } catch (const std::exception & error) {
    RCLCPP_ERROR(rclcpp::get_logger("mc02_serial_bridge"), "%s", error.what()); result = 1;
  }
  rclcpp::shutdown();
  return result;
}
