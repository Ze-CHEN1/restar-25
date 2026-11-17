#include "ros2.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace std::chrono_literals;
void require(bool condition, const char * message)
{
  if (!condition) throw std::runtime_error(message);
}
template <typename Predicate>
void wait_for(Predicate predicate, const char * message)
{
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!predicate()) {
    require(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::sleep_for(10ms);
  }
}
}

// Exercises the real wrapper without constructing any camera, CBoard or Gimbal.
int main(int argc, char ** argv)
{
  try {
    io::RosRuntime source(argc, argv);
    auto options = source.node_options();
    options.arguments({"--ros-args", "-r", "__ns:=/restar_inputs_test"});
    auto node = std::make_shared<rclcpp::Node>("input_mock", options);
    auto enemy = node->create_publisher<restar_msgs::msg::EnemyStatusMsg>("enemy_status", 10);
    auto target = node->create_publisher<restar_msgs::msg::AutoaimTargetMsg>("autoaim_target", 10);
    source.add(node);
    source.start();
    char program[] = "inputs_test";
    char ros_args[] = "--ros-args";
    char remap[] = "-r";
    char ns[] = "__ns:=/restar_inputs_test";
    char * arguments[] = {program, ros_args, remap, ns};
    for (int cycle = 0; cycle < 2; ++cycle) {
      io::ROS2 receiver(4, arguments);
      require(receiver.subscribe_enemy_status().empty(), "Initial enemy list is not empty");
      require(receiver.subscribe_autoaim_target().empty(), "Initial target list is not empty");
      wait_for([&]() { return enemy->get_subscription_count() >= 1 &&
        target->get_subscription_count() >= 1; }, "Input subscriptions not discovered");
      restar_msgs::msg::EnemyStatusMsg enemies;
      enemies.timestamp = node->now();
      enemies.invincible_enemy_ids = {1, 6};
      restar_msgs::msg::AutoaimTargetMsg targets;
      targets.timestamp = node->now();
      targets.target_ids = {3, 4};
      // An external observer may be discovered before the recreated wrapper. Like a
      // periodic state source, publish until the actual wrapper confirms reception.
      wait_for([&]() {
        enemies.timestamp = node->now();
        targets.timestamp = node->now();
        enemy->publish(enemies);
        target->publish(targets);
        return receiver.subscribe_enemy_status() == std::vector<int8_t>({1, 6}) &&
          receiver.subscribe_autoaim_target() == std::vector<int8_t>({3, 4});
      }, "ID mapping failed");
      targets.target_ids.clear();
      target->publish(targets);
      wait_for([&]() { return receiver.subscribe_autoaim_target().empty(); }, "Empty list not applied");
      require(receiver.subscribe_enemy_status() == std::vector<int8_t>({1, 6}), "Inputs are not independent");
      // Keep target input alive while enemy input expires on the receive steady clock.
      const auto end = std::chrono::steady_clock::now() + 1700ms;
      targets.target_ids = {8};
      while (std::chrono::steady_clock::now() < end) {
        targets.timestamp = node->now();
        target->publish(targets);
        std::this_thread::sleep_for(50ms);
      }
      require(receiver.subscribe_enemy_status().empty(), "Stopped enemy input did not expire");
      require(receiver.subscribe_autoaim_target() == std::vector<int8_t>({8}), "Live target input expired");
      std::cout << "PASS cycle " << cycle + 1 << ": mapping, empty update, independent timeout" << std::endl;
    }
    // Both wrapper contexts have been destroyed; the independent source remains usable.
    target->publish(restar_msgs::msg::AutoaimTargetMsg{});
    source.stop();
    std::cout << "PASS wrapper restart and independent context shutdown" << std::endl;
    return 0;
  } catch (const std::exception & e) {
    std::cerr << "FAIL " << e.what() << std::endl;
    return 1;
  }
}
