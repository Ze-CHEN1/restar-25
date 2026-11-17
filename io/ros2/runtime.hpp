#ifndef IO__ROS2_RUNTIME_HPP
#define IO__ROS2_RUNTIME_HPP

#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include "rclcpp/rclcpp.hpp"

namespace io
{
// Private context: no repeated global init/shutdown and no process signal-handler ownership.
// Nodes are registered on the constructing thread. Executor callbacks run on one owned thread.
class RosRuntime
{
public:
  explicit RosRuntime(int argc = 0, char ** argv = nullptr)
  : context_(std::make_shared<rclcpp::Context>())
  {
    context_->init(argc, argv);
    rclcpp::ExecutorOptions options;
    options.context = context_;
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>(options);
  }
  ~RosRuntime()
  {
    stop();
    if (context_->is_valid()) context_->shutdown("Restar ROS runtime stopped");
  }
  RosRuntime(const RosRuntime &) = delete;
  RosRuntime & operator=(const RosRuntime &) = delete;

  rclcpp::NodeOptions node_options() const
  {
    rclcpp::NodeOptions options;
    options.context(context_);
    return options;
  }
  void add(const rclcpp::Node::SharedPtr & node)
  {
    executor_->add_node(node);
    nodes_.push_back(node);
  }
  void start()
  {
    if (thread_.joinable()) return;
    thread_ = std::thread([this]() {
      try {
        // Bounded wait also handles stop immediately after thread creation, before spin starts.
        while (!stopping_ && context_->is_valid())
          executor_->spin_once(std::chrono::milliseconds(20));
      } catch (const std::exception & e) {
        RCLCPP_ERROR(rclcpp::get_logger("restar_ros_runtime"), "Executor stopped: %s", e.what());
      }
    });
  }
  void stop()
  {
    stopping_ = true;
    if (executor_) executor_->cancel();
    if (thread_.joinable()) thread_.join();
    // Release executor references before nodes. Context shuts down in the destructor.
    if (executor_) {
      for (const auto & node : nodes_) executor_->remove_node(node);
      executor_.reset();
    }
    nodes_.clear();
  }

private:
  std::shared_ptr<rclcpp::Context> context_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::vector<rclcpp::Node::SharedPtr> nodes_;
  std::thread thread_;
  std::atomic<bool> stopping_{false};
};
}  // namespace io
#endif
