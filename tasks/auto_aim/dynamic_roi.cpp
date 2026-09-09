#include "dynamic_roi.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <yaml-cpp/yaml.h>

namespace auto_aim
{
namespace
{
int read_int(const YAML::Node & node, const char * key, int default_value)
{
  return node[key] ? node[key].as<int>() : default_value;
}
}  // namespace

DynamicRoiController::DynamicRoiController(const std::string & config_path)
: enabled_(false),
  use_fixed_roi_(false),
  fixed_roi_(),
  margin_(100),
  min_width_(600),
  min_height_(600),
  max_width_(1200),
  max_height_(900),
  enemy_color_(Color::red)
{
  const auto yaml = YAML::LoadFile(config_path);
  const auto roi = yaml["roi"];
  const auto dynamic_roi = yaml["dynamic_roi"];

  enabled_ = yaml["use_dynamic_roi"] ? yaml["use_dynamic_roi"].as<bool>() : false;
  use_fixed_roi_ = yaml["use_roi"] ? yaml["use_roi"].as<bool>() : false;
  enemy_color_ =
    yaml["enemy_color"].as<std::string>() == "red" ? Color::red : Color::blue;

  fixed_roi_ = cv::Rect(
    roi && roi["x"] ? roi["x"].as<int>() : 0, roi && roi["y"] ? roi["y"].as<int>() : 0,
    roi && roi["width"] ? roi["width"].as<int>() : -1,
    roi && roi["height"] ? roi["height"].as<int>() : -1);

  if (dynamic_roi) {
    margin_ = read_int(dynamic_roi, "margin", margin_);
    min_width_ = read_int(dynamic_roi, "min_width", min_width_);
    min_height_ = read_int(dynamic_roi, "min_height", min_height_);
    max_width_ = read_int(dynamic_roi, "max_width", max_width_);
    max_height_ = read_int(dynamic_roi, "max_height", max_height_);
  }
}

std::vector<cv::Rect> DynamicRoiController::candidates(
  const cv::Size & image_size, const std::optional<Target> & last_target, const Solver & solver,
  std::chrono::steady_clock::time_point timestamp, const std::string & tracker_state) const
{
  std::vector<cv::Rect> result;

  auto add_unique = [&](const cv::Rect & rect) {
    const auto valid = clamp_rect(rect, image_size);
    if (valid.width <= 0 || valid.height <= 0) return;
    if (std::find(result.begin(), result.end(), valid) == result.end()) result.push_back(valid);
  };

  if (enabled_ && last_target.has_value()) {
    const auto dynamic_roi =
      make_dynamic_roi(image_size, *last_target, solver, timestamp, tracker_state);
    if (dynamic_roi.has_value()) add_unique(*dynamic_roi);
  }

  if (use_fixed_roi_) {
    auto fixed_roi = fixed_roi_;
    if (fixed_roi.width == -1) fixed_roi.width = image_size.width - fixed_roi.x;
    if (fixed_roi.height == -1) fixed_roi.height = image_size.height - fixed_roi.y;
    add_unique(fixed_roi);
  }

  add_unique(cv::Rect(0, 0, image_size.width, image_size.height));
  return result;
}

bool DynamicRoiController::is_enemy_color(const Armor & armor) const
{
  return armor.color == enemy_color_;
}

cv::Rect DynamicRoiController::clamp_rect(const cv::Rect & rect, const cv::Size & image_size) const
{
  const auto image_rect = cv::Rect(0, 0, image_size.width, image_size.height);
  return rect & image_rect;
}

std::optional<cv::Rect> DynamicRoiController::make_dynamic_roi(
  const cv::Size & image_size, const Target & target, const Solver & solver,
  std::chrono::steady_clock::time_point timestamp, const std::string & tracker_state) const
{
  if (tracker_state != "tracking" && tracker_state != "temp_lost") return std::nullopt;

  auto predicted_target = target;
  predicted_target.predict(timestamp);

  double min_x = std::numeric_limits<double>::max();
  double min_y = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double max_y = std::numeric_limits<double>::lowest();
  bool has_valid_point = false;

  for (const auto & xyza : predicted_target.armor_xyza_list()) {
    const auto points = solver.reproject_armor(
      xyza.head(3), xyza[3], predicted_target.armor_type, predicted_target.name);
    for (const auto & point : points) {
      if (!std::isfinite(point.x) || !std::isfinite(point.y)) continue;
      min_x = std::min(min_x, static_cast<double>(point.x));
      min_y = std::min(min_y, static_cast<double>(point.y));
      max_x = std::max(max_x, static_cast<double>(point.x));
      max_y = std::max(max_y, static_cast<double>(point.y));
      has_valid_point = true;
    }
  }

  if (!has_valid_point) return std::nullopt;

  const auto center_x = (min_x + max_x) / 2.0;
  const auto center_y = (min_y + max_y) / 2.0;
  const auto width = std::clamp(
    std::max(max_x - min_x + 2.0 * margin_, static_cast<double>(min_width_)),
    static_cast<double>(min_width_), static_cast<double>(max_width_));
  const auto height = std::clamp(
    std::max(max_y - min_y + 2.0 * margin_, static_cast<double>(min_height_)),
    static_cast<double>(min_height_), static_cast<double>(max_height_));

  const cv::Rect roi(
    static_cast<int>(std::lround(center_x - width / 2.0)),
    static_cast<int>(std::lround(center_y - height / 2.0)),
    static_cast<int>(std::lround(width)), static_cast<int>(std::lround(height)));
  return clamp_rect(roi, image_size);
}
}  // namespace auto_aim
