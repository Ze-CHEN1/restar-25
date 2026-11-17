#ifndef IO__PUBLISH2NAV_HPP
#define IO__PUBLISH2NAV_HPP

#include <Eigen/Dense>
#include "io/command.hpp"
#include "rclcpp/rclcpp.hpp"
#include "robot_interfaces/msg/gimbal_command.hpp"
#include "robot_interfaces/msg/vision_target2_d.hpp"
#include "std_msgs/msg/string.hpp"

namespace io
{
// Shared by the full Restar application and the explicitly labelled hardware-free mock.
class Publish2Nav : public rclcpp::Node
{
public:
  explicit Publish2Nav(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  void start();  // Compatibility for independently owned nodes; ROS2 uses its executor instead.
  void send_data(const Eigen::Vector4d & data, bool observation_available = true);
  void send_command(const Command & command, bool output_available = true);

private:
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Publisher<robot_interfaces::msg::VisionTarget2D>::SharedPtr target_publisher_;
  rclcpp::Publisher<robot_interfaces::msg::GimbalCommand>::SharedPtr command_publisher_;
};
}  // namespace io
#endif
