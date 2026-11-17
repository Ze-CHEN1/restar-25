#ifndef IO__ROS2_HPP
#define IO__ROS2_HPP

#include "publish2nav.hpp"
#include "runtime.hpp"
#include "subscribe2nav.hpp"

namespace io
{
class ROS2
{
public:
  explicit ROS2(int argc = 0, char ** argv = nullptr);
  ~ROS2();
  ROS2(const ROS2 &) = delete;
  ROS2 & operator=(const ROS2 &) = delete;

  void publish(const Eigen::Vector4d & target_pos);
  void publish_target(const Eigen::Vector4d & target_pos, bool observation_available);
  void publish_gimbal_command(const Command & command, bool output_available = true);
  std::vector<int8_t> subscribe_enemy_status();
  std::vector<int8_t> subscribe_autoaim_target();

  template <typename T>
  std::shared_ptr<rclcpp::Publisher<T>> create_publisher(
    const std::string & node_name, const std::string & topic_name, size_t queue_size)
  {
    auto node = std::make_shared<rclcpp::Node>(node_name, runtime_.node_options());
    auto publisher = node->create_publisher<T>(topic_name, queue_size);
    runtime_.add(node);  // Retained and spun by the same executor, no detached thread.
    return publisher;
  }

private:
  RosRuntime runtime_;
  std::shared_ptr<Publish2Nav> publish2nav_;
  std::shared_ptr<Subscribe2Nav> subscribe2nav_;
};
}  // namespace io
#endif
