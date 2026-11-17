#include <algorithm>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "io/ros2/publish2nav.hpp"
#include "io/ros2/runtime.hpp"
#include "tasks/auto_aim/aimer.hpp"
#include "tasks/auto_aim/solver.hpp"
#include "tasks/auto_aim/tracker.hpp"
#include "tasks/auto_aim/yolo.hpp"

namespace
{
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
struct Pose
{
  double t;
  Eigen::Quaterniond q;
};

void require_file(const std::filesystem::path & path)
{
  if (!std::filesystem::is_regular_file(path))
    throw std::invalid_argument("Replay requires an existing ordinary file: " + path.string());
}

std::vector<Pose> read_poses(const std::filesystem::path & path)
{
  require_file(path);
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read pose file");
  std::vector<Pose> poses;
  std::string line;
  while (std::getline(input, line)) {
    double t, w, x, y, z;
    std::string extra;
    std::istringstream row(line);
    if (!(row >> t >> w >> x >> y >> z) || (row >> extra))
      throw std::runtime_error("Pose row must contain t w x y z, row=" + std::to_string(poses.size()));
    Eigen::Quaterniond q(w, x, y, z);
    if (!std::isfinite(t) || t < 0 || t > 1e6 || !q.coeffs().allFinite() ||
        std::abs(q.norm() - 1.0) > 0.01 || (!poses.empty() && t <= poses.back().t))
      throw std::runtime_error("Invalid/nonmonotonic pose, row=" + std::to_string(poses.size()));
    poses.push_back({t, q});
  }
  if (poses.empty()) throw std::runtime_error("Empty pose file");
  return poses;
}
}  // namespace

// File-only assembly of the existing auto_aim_test computation; never constructs hardware I/O.
int main(int argc, char ** argv)
{
  std::signal(SIGINT, stop);
  std::signal(SIGTERM, stop);
  try {
    io::RosRuntime runtime(argc, argv);
    auto publisher = std::make_shared<io::Publish2Nav>(runtime.node_options());
    const auto config = publisher->declare_parameter<std::string>("config_path", "configs/demo.yaml");
    const auto prefix = publisher->declare_parameter<std::string>("input_prefix", "assets/demo/demo");
    const auto trace_path = publisher->declare_parameter<std::string>("trace_csv", "");
    const auto first = publisher->declare_parameter<int>("start_frame", 0);
    const auto limit = publisher->declare_parameter<int>("max_frames", 0);
    const double rate = publisher->declare_parameter("replay_rate_hz", 30.0);
    const double bullet_speed = publisher->declare_parameter("bullet_speed_mps", 27.0);
    if (first < 0 || limit < 0 || !std::isfinite(rate) || rate < 0 || rate > 1000 ||
        !std::isfinite(bullet_speed) || bullet_speed < 14 || bullet_speed > 50)
      throw std::invalid_argument("start_frame/max_frames >= 0, replay_rate_hz in [0,1000], bullet_speed_mps in [14,50]");
    require_file(config);
    require_file(prefix + ".avi");
    const auto poses = read_poses(prefix + ".txt");
    if (static_cast<size_t>(first) >= poses.size()) throw std::invalid_argument("start_frame beyond pose file");
    cv::VideoCapture video(prefix + ".avi", cv::CAP_FFMPEG);
    if (!video.isOpened()) throw std::runtime_error("Cannot decode replay video");
    // Sequential skip also works with recordings without a valid seek/index table.
    cv::Mat image;
    for (int i = 0; i < first; ++i)
      if (!video.read(image)) throw std::runtime_error("Video ended before start_frame");
    std::ofstream trace;
    if (!trace_path.empty()) {
      if (std::filesystem::exists(trace_path))
        throw std::invalid_argument("trace_csv already exists; choose a new output path");
      trace.open(trace_path);
      if (!trace) throw std::runtime_error("Cannot create trace_csv");
      trace << "frame,recorded_s,control,yaw,pitch,target_valid,target_id,x,y,algorithm_ms,publish_ms\n";
      trace << std::setprecision(17);
    }
    auto_aim::YOLO yolo(config, false);
    auto_aim::Solver solver(config);
    auto_aim::Tracker tracker(config, solver);
    auto_aim::Aimer aimer(config);
    runtime.add(publisher);
    runtime.start();
    RCLCPP_WARN(publisher->get_logger(),
      "FILE REPLAY ros_only: existing YOLO/Solver/Tracker/Aimer; no camera/CAN/serial or firing. "
      "bullet_speed=%.2f is configured, to_now=false; rate=%.2f is publication pacing, not recorded time.",
      bullet_speed, rate);
    const auto origin = std::chrono::steady_clock::now();
    size_t frames = 0, controlled = 0, observed = 0;
    auto next = origin;
    size_t index = first;
    for (; !stopped && (limit == 0 || frames < static_cast<size_t>(limit)); ++index) {
      if (!video.read(image)) {
        if (index < poses.size()) throw std::runtime_error("Video ended before pose rows were consumed");
        break;
      }
      if (index >= poses.size()) throw std::runtime_error("Video has more frames than pose rows");
      const auto & pose = poses[index];
      const auto timestamp = origin + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(pose.t - poses[first].t));
      const auto begun = std::chrono::steady_clock::now();
      solver.set_R_gimbal2world(pose.q);
      auto armors = yolo.detect(image, static_cast<int>(index));
      const auto targets = tracker.track(armors, timestamp);
      const auto command = aimer.aim(targets, timestamp, bullet_speed, false);
      // Same conservative source check as sentry: do not read unsolved armor coordinates.
      Eigen::Vector4d observation = Eigen::Vector4d::Zero();
      bool available = false;
      if (!targets.empty()) {
        const auto armor = std::find_if(armors.begin(), armors.end(), [&](const auto & a) {
          return a.name == targets.front().name;
        });
        if (armor != armors.end() && armor->type == targets.front().armor_type) {
          observation = {armor->xyz_in_gimbal[0], armor->xyz_in_gimbal[1], 1.0,
                         static_cast<double>(armor->name) + 1.0};
          available = observation.allFinite() && observation[3] >= 1 && observation[3] <= 8;
        }
      }
      const auto computed = std::chrono::steady_clock::now();
      publisher->send_command(command);
      publisher->send_data(observation, available);
      const auto published = std::chrono::steady_clock::now();
      ++frames;
      controlled += command.control;
      observed += available;
      if (trace.is_open()) {
        trace << index << ',' << pose.t << ',' << command.control << ',' << command.yaw << ','
              << command.pitch << ',' << available << ',' << (available ? observation[3] : 0) << ','
              << (available ? observation[0] : 0) << ',' << (available ? observation[1] : 0) << ','
              << std::chrono::duration<double, std::milli>(computed - begun).count() << ','
              << std::chrono::duration<double, std::milli>(published - computed).count() << '\n';
        if (!trace) throw std::runtime_error("trace_csv write failed");
      }
      if (rate > 0) {
        // No burst catch-up when inference is slower than the configured publication period.
        next = std::max(next + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(1.0 / rate)), published);
        while (!stopped && std::chrono::steady_clock::now() < next)
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    // Clear validity at EOF/limit/SIGINT, followed by receiver-owned timeout after process exit.
    publisher->send_command(io::Command{false, false, 0, 0}, false);
    publisher->send_data(Eigen::Vector4d::Zero(), false);
    RCLCPP_INFO(publisher->get_logger(), "Replay complete: frames=%zu control=%zu observations=%zu",
                frames, controlled, observed);
    runtime.stop();
    publisher.reset();
    return 0;
  } catch (const std::exception & e) {
    RCLCPP_ERROR(rclcpp::get_logger("restar_ros_replay"), "%s", e.what());
    return 1;
  }
}
