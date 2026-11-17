#include "publish2nav.hpp"
#include "runtime.hpp"

#include <atomic>
#include <cmath>
#include <csignal>
#include <limits>
#include <stdexcept>

namespace
{
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
}

int main(int argc, char ** argv)
{
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  try {
    io::RosRuntime runtime(argc, argv);
    auto publisher = std::make_shared<io::Publish2Nav>(runtime.node_options());
    const double rate = publisher->declare_parameter("mock_rate_hz", 30.0);
    const double duration = publisher->declare_parameter("duration_s", 0.0);
    const auto scenario = publisher->declare_parameter<std::string>("mock_case", "valid");
    if (!std::isfinite(rate) || rate <= 0 || rate > 1000 || !std::isfinite(duration) || duration < 0)
      throw std::invalid_argument("mock_rate_hz must be (0,1000], duration_s >= 0");
    if (scenario != "valid" && scenario != "invalid" && scenario != "nonfinite" &&
        scenario != "no_target" && scenario != "disabled")
      throw std::invalid_argument("Unknown mock_case");
    RCLCPP_WARN(publisher->get_logger(), "MOCK ros_only: fixed synthetic inputs; no hardware devices or algorithm execution. case=%s", scenario.c_str());
    runtime.add(publisher);
    runtime.start();
    const auto started = std::chrono::steady_clock::now();
    auto next = started;
    const auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(1.0 / rate));
    while (!stopped) {
      if (duration > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() >= duration)
        break;
      io::Command command{scenario != "disabled", false, 0.25, -0.1};
      Eigen::Vector4d observation{1.25, -0.5, 1.0, 6.0};
      if (scenario == "nonfinite") {
        command.yaw = std::numeric_limits<double>::quiet_NaN();
        observation[0] = std::numeric_limits<double>::infinity();
      }
      if (scenario == "no_target") observation.setZero();
      publisher->send_command(command, scenario != "invalid");
      publisher->send_data(observation, scenario != "invalid");
      next += period;
      std::this_thread::sleep_until(next);
    }
    runtime.stop();
    publisher.reset();
    return 0;
  } catch (const std::exception & e) {
    RCLCPP_ERROR(rclcpp::get_logger("restar_ros_mock"), "%s", e.what());
    return 1;
  }
}
