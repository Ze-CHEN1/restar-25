#ifndef AUTO_AIM__DYNAMIC_ROI_HPP
#define AUTO_AIM__DYNAMIC_ROI_HPP

#include <chrono>
#include <list>
#include <optional>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "armor.hpp"
#include "solver.hpp"
#include "target.hpp"

namespace auto_aim
{
enum class RoiStage
{
  dynamic,
  fixed,
  full,
};

struct RoiCandidate
{
  cv::Rect rect;
  RoiStage stage;
};

const char * roi_stage_name(RoiStage stage);

class DynamicRoiController
{
public:
  explicit DynamicRoiController(const std::string & config_path);

  std::vector<RoiCandidate> candidates(
    const cv::Size & image_size, const std::optional<Target> & last_target, const Solver & solver,
    std::chrono::steady_clock::time_point timestamp, const std::string & tracker_state) const;

  bool is_enemy_color(const Armor & armor) const;

private:
  bool enabled_;
  bool use_fixed_roi_;
  cv::Rect fixed_roi_;
  int margin_;
  int min_width_;
  int min_height_;
  int max_width_;
  int max_height_;
  Color enemy_color_;

  cv::Rect clamp_rect(const cv::Rect & rect, const cv::Size & image_size) const;
  std::optional<cv::Rect> make_dynamic_roi(
    const cv::Size & image_size, const Target & target, const Solver & solver,
    std::chrono::steady_clock::time_point timestamp, const std::string & tracker_state) const;
};
}  // namespace auto_aim

#endif  // AUTO_AIM__DYNAMIC_ROI_HPP
