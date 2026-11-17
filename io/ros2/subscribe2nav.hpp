#ifndef IO__SUBSCRIBE2NAV_HPP
#define IO__SUBSCRIBE2NAV_HPP

#include <chrono>
#include <mutex>
#include <vector>
#include "rclcpp/rclcpp.hpp"
#include "restar_msgs/msg/autoaim_target_msg.hpp"
#include "restar_msgs/msg/enemy_status_msg.hpp"

namespace io
{
class Subscribe2Nav : public rclcpp::Node
{
public:
  explicit Subscribe2Nav(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());
  void start();
  std::vector<int8_t> subscribe_enemy_status();
  std::vector<int8_t> subscribe_autoaim_target();

private:
  std::mutex mutex_;
  std::vector<int8_t> enemy_ids_, target_ids_;
  std::chrono::steady_clock::time_point enemy_received_{}, target_received_{};
  rclcpp::Subscription<restar_msgs::msg::EnemyStatusMsg>::SharedPtr enemy_status_subscription_;
  rclcpp::Subscription<restar_msgs::msg::AutoaimTargetMsg>::SharedPtr autoaim_target_subscription_;
};
}  // namespace io
#endif
