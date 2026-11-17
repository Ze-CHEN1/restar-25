#include "ros2.hpp"

namespace io
{
ROS2::ROS2(int argc, char ** argv) : runtime_(argc, argv)
{
  publish2nav_ = std::make_shared<Publish2Nav>(runtime_.node_options());
  subscribe2nav_ = std::make_shared<Subscribe2Nav>(runtime_.node_options());
  runtime_.add(publish2nav_);
  runtime_.add(subscribe2nav_);
  runtime_.start();
}

ROS2::~ROS2()
{
  // Cancel and join before destroying callback state. Context is private to this wrapper.
  runtime_.stop();
  subscribe2nav_.reset();
  publish2nav_.reset();
}

void ROS2::publish(const Eigen::Vector4d & target_pos) { publish2nav_->send_data(target_pos); }
void ROS2::publish_target(const Eigen::Vector4d & target_pos, bool observation_available)
{
  publish2nav_->send_data(target_pos, observation_available);
}
void ROS2::publish_gimbal_command(const Command & command, bool output_available)
{
  publish2nav_->send_command(command, output_available);
}
std::vector<int8_t> ROS2::subscribe_enemy_status()
{
  return subscribe2nav_->subscribe_enemy_status();
}
std::vector<int8_t> ROS2::subscribe_autoaim_target()
{
  return subscribe2nav_->subscribe_autoaim_target();
}
}  // namespace io
