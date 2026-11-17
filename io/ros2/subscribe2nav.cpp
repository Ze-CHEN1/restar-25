#include "subscribe2nav.hpp"

namespace io
{
Subscribe2Nav::Subscribe2Nav(const rclcpp::NodeOptions & options) : Node("nav_subscriber", options)
{
  enemy_status_subscription_ = create_subscription<restar_msgs::msg::EnemyStatusMsg>(
    "enemy_status", 10, [this](const restar_msgs::msg::EnemyStatusMsg::SharedPtr msg) {
      std::lock_guard<std::mutex> lock(mutex_);
      enemy_ids_ = msg->invincible_enemy_ids;
      enemy_received_ = std::chrono::steady_clock::now();
    });
  autoaim_target_subscription_ = create_subscription<restar_msgs::msg::AutoaimTargetMsg>(
    "autoaim_target", 10, [this](const restar_msgs::msg::AutoaimTargetMsg::SharedPtr msg) {
      std::lock_guard<std::mutex> lock(mutex_);
      target_ids_ = msg->target_ids;
      target_received_ = std::chrono::steady_clock::now();
    });
}

void Subscribe2Nav::start() { rclcpp::spin(shared_from_this()); }

std::vector<int8_t> Subscribe2Nav::subscribe_enemy_status()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (std::chrono::steady_clock::now() - enemy_received_ > std::chrono::milliseconds(1500))
    return {};
  return enemy_ids_;
}
std::vector<int8_t> Subscribe2Nav::subscribe_autoaim_target()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (std::chrono::steady_clock::now() - target_received_ > std::chrono::milliseconds(1500))
    return {};
  return target_ids_;
}
}  // namespace io
