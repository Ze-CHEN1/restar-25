#include "publish2nav.hpp"

#include <cmath>
#include <string>

namespace io
{
Publish2Nav::Publish2Nav(const rclcpp::NodeOptions & options)
: Node("restar_vision_publisher", options)
{
  publisher_ = create_publisher<std_msgs::msg::String>("auto_aim_target_pos", 10);
  target_publisher_ = create_publisher<robot_interfaces::msg::VisionTarget2D>(
    "vision/target", rclcpp::QoS(rclcpp::KeepLast(5)).best_effort().durability_volatile());
  command_publisher_ = create_publisher<robot_interfaces::msg::GimbalCommand>(
    "vision/gimbal_cmd", rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile());
}

void Publish2Nav::send_data(const Eigen::Vector4d & data, bool observation_available)
{
  std_msgs::msg::String legacy;
  legacy.data = std::to_string(data[0]) + "," + std::to_string(data[1]) + "," +
                std::to_string(data[2]) + "," + std::to_string(data[3]);
  publisher_->publish(legacy);

  robot_interfaces::msg::VisionTarget2D target;
  target.header.stamp = now();  // Publication time, never exposure time.
  // No TF equivalence between Restar gimbal and navigation has been established.
  target.valid = observation_available && data.allFinite() && data[2] == 1.0 &&
                 data[3] >= 1.0 && data[3] <= 8.0 && std::floor(data[3]) == data[3];
  if (target.valid) {
    target.x = data[0];
    target.y = data[1];
    target.target_id = static_cast<int16_t>(data[3]);
  }
  // Invalid frames explicitly clear coordinates and ID; never reuse the last observation.
  target_publisher_->publish(target);
}

void Publish2Nav::send_command(const Command & command, bool output_available)
{
  robot_interfaces::msg::GimbalCommand msg;
  msg.header.stamp = now();
  msg.control_enabled = command.control;
  const bool finite = std::isfinite(command.yaw) && std::isfinite(command.pitch);
  msg.valid = output_available && command.control && finite;
  msg.yaw = finite ? command.yaw : 0.0;
  msg.pitch = finite ? command.pitch : 0.0;
  command_publisher_->publish(msg);
}

void Publish2Nav::start() { rclcpp::spin(shared_from_this()); }
}  // namespace io
