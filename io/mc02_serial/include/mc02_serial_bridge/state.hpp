#ifndef MC02_SERIAL_BRIDGE__STATE_HPP_
#define MC02_SERIAL_BRIDGE__STATE_HPP_

#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

#include "geometry_msgs/msg/twist.hpp"
#include "robot_interfaces/msg/gimbal_command.hpp"
#include "robot_interfaces/msg/vision_target2_d.hpp"
#include "mc02_serial_bridge/protocol.hpp"

namespace mc02_serial_bridge
{
using SteadyClock = std::chrono::steady_clock;
using Milliseconds = std::chrono::duration<double, std::milli>;

enum class Reason {ok, waiting, timeout, zero_stamp, malformed_stamp, old_stamp, future_stamp,
  repeated_stamp, regressed_stamp, clock_reset, nonfinite, float_overflow, invalid, disabled, target_id};
inline const char * reason_name(Reason reason)
{
  switch (reason) {
#define MC02_REASON(x) case Reason::x: return #x
    MC02_REASON(ok); MC02_REASON(waiting); MC02_REASON(timeout); MC02_REASON(zero_stamp);
    MC02_REASON(malformed_stamp); MC02_REASON(old_stamp); MC02_REASON(future_stamp);
    MC02_REASON(repeated_stamp); MC02_REASON(regressed_stamp); MC02_REASON(clock_reset);
    MC02_REASON(nonfinite); MC02_REASON(float_overflow); MC02_REASON(invalid);
    MC02_REASON(disabled); MC02_REASON(target_id);
#undef MC02_REASON
  }
  return "unknown";
}
struct Policy
{
  Milliseconds nav_timeout{200}, gimbal_timeout{200}, target_timeout{500};
  Milliseconds future_tolerance{50};
};
struct ChannelFreshness
{
  bool received{};
  SteadyClock::time_point received_at{};
  int64_t source_stamp_ns{}, high_water_ns{}, last_ros_now_ns{};
  bool ros_clock_seen{};
  Reason stamp_reason{Reason::waiting};
  uint64_t samples{}, accepted_stamps{}, repeated_stamps{}, regressed_stamps{}, clock_resets{};

  void receive(SteadyClock::time_point now)
  { received = true; received_at = now; ++samples; }

  void source_update(const builtin_interfaces::msg::Time & stamp, int64_t ros_now,
    Milliseconds timeout, Milliseconds future, SteadyClock::time_point now)
  {
    receive(now);
    const bool reset = ros_clock_seen && ros_now < last_ros_now_ns;
    ros_clock_seen = true;
    last_ros_now_ns = ros_now;
    if (reset) { high_water_ns = 0; ++clock_resets; }
    source_stamp_ns = static_cast<int64_t>(stamp.sec) * 1000000000LL + stamp.nanosec;
    stamp_reason = Reason::ok;
    if (stamp.sec < 0 || stamp.nanosec >= 1000000000U) stamp_reason = Reason::malformed_stamp;
    else if (source_stamp_ns == 0 || ros_now <= 0) stamp_reason = Reason::zero_stamp;
    else if (static_cast<double>(source_stamp_ns - ros_now) / 1e6 > future.count())
      stamp_reason = Reason::future_stamp;
    else if (static_cast<double>(ros_now - source_stamp_ns) / 1e6 > timeout.count())
      stamp_reason = Reason::old_stamp;
    else if (source_stamp_ns == high_water_ns) {
      ++repeated_stamps; stamp_reason = Reason::repeated_stamp;
    } else if (source_stamp_ns < high_water_ns) {
      ++regressed_stamps; stamp_reason = Reason::regressed_stamp;
    }
    if (stamp_reason == Reason::ok) { high_water_ns = source_stamp_ns; ++accepted_stamps; }
    // Quarantine the first sample after an observed local ROS clock rollback.
    if (reset) stamp_reason = Reason::clock_reset;
  }

  Reason freshness(Milliseconds timeout, SteadyClock::time_point now,
    bool require_stamp = false, int64_t ros_now = 0, Milliseconds future = Milliseconds{50}) const
  {
    if (!received) return Reason::waiting;
    if (now < received_at || now - received_at > timeout) return Reason::timeout;
    if (!require_stamp) return Reason::ok;  // Twist has no Header.
    if (stamp_reason != Reason::ok) return stamp_reason;
    if (ros_now <= 0) return Reason::zero_stamp;
    if (static_cast<double>(source_stamp_ns - ros_now) / 1e6 > future.count())
      return Reason::future_stamp;
    if (static_cast<double>(ros_now - source_stamp_ns) / 1e6 > timeout.count())
      return Reason::old_stamp;
    return Reason::ok;
  }
  double age_ms(SteadyClock::time_point now) const
  { return received ? Milliseconds(now - received_at).count() : -1.0; }
};
// Only fixed-size scalar data is copied; Header.frame_id strings are not cached.
struct GimbalData { bool valid{}, control_enabled{}; double yaw{}, pitch{}; };
struct TargetData { bool valid{}; int16_t target_id{}; double x{}, y{}; };
struct ControlSnapshot
{
  geometry_msgs::msg::Twist nav;
  GimbalData gimbal;
  TargetData target;
  ChannelFreshness nav_state, gimbal_state, target_state;
};
class LatestState
{
public:
  explicit LatestState(Policy policy = {}) : policy_(policy) {}
  void receive_nav(const geometry_msgs::msg::Twist & msg,
    SteadyClock::time_point now = SteadyClock::now())
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.nav = msg;
    state_.nav_state.receive(now);
  }
  void receive_gimbal(const robot_interfaces::msg::GimbalCommand & msg, int64_t ros_now,
    SteadyClock::time_point now = SteadyClock::now())
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.gimbal = {msg.valid, msg.control_enabled, msg.yaw, msg.pitch};
    state_.gimbal_state.source_update(msg.header.stamp, ros_now,
      policy_.gimbal_timeout, policy_.future_tolerance, now);
  }
  void receive_target(const robot_interfaces::msg::VisionTarget2D & msg, int64_t ros_now,
    SteadyClock::time_point now = SteadyClock::now())
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.target = {msg.valid, msg.target_id, msg.x, msg.y};
    state_.target_state.source_update(msg.header.stamp, ros_now,
      policy_.target_timeout, policy_.future_tolerance, now);
  }
  ControlSnapshot snapshot() const
  { std::lock_guard<std::mutex> lock(mutex_); return state_; }
private:
  Policy policy_;
  mutable std::mutex mutex_;
  ControlSnapshot state_;
};
inline bool finite_twist(const geometry_msgs::msg::Twist & m)
{
  return std::isfinite(m.linear.x) && std::isfinite(m.linear.y) && std::isfinite(m.linear.z) &&
    std::isfinite(m.angular.x) && std::isfinite(m.angular.y) && std::isfinite(m.angular.z);
}
inline bool fits_float(double v)
{ return std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max(); }
struct Evaluation
{
  ControlPayload payload;
  Reason nav{}, gimbal{}, target{};
};
inline Evaluation evaluate(const ControlSnapshot & s, const Policy & p,
  SteadyClock::time_point now, int64_t ros_now)
{
  Evaluation e;
  e.nav = s.nav_state.freshness(p.nav_timeout, now);
  if (e.nav == Reason::ok && !finite_twist(s.nav)) e.nav = Reason::nonfinite;
  if (e.nav == Reason::ok && (!fits_float(s.nav.linear.x) || !fits_float(s.nav.linear.y) ||
      !fits_float(s.nav.angular.z))) e.nav = Reason::float_overflow;
  if (e.nav == Reason::ok) {
    e.payload.nav_valid = 1;
    e.payload.vx = static_cast<float>(s.nav.linear.x);
    e.payload.vy = static_cast<float>(s.nav.linear.y);
    e.payload.wz = static_cast<float>(s.nav.angular.z);
  }
  e.gimbal = s.gimbal_state.freshness(p.gimbal_timeout, now, true, ros_now, p.future_tolerance);
  e.payload.control_enabled = e.gimbal == Reason::ok && s.gimbal.control_enabled ? 1 : 0;
  if (e.gimbal == Reason::ok && !s.gimbal.valid) e.gimbal = Reason::invalid;
  if (e.gimbal == Reason::ok && !s.gimbal.control_enabled) e.gimbal = Reason::disabled;
  if (e.gimbal == Reason::ok && (!std::isfinite(s.gimbal.yaw) || !std::isfinite(s.gimbal.pitch)))
    e.gimbal = Reason::nonfinite;
  if (e.gimbal == Reason::ok && (!fits_float(s.gimbal.yaw) || !fits_float(s.gimbal.pitch)))
    e.gimbal = Reason::float_overflow;
  if (e.gimbal == Reason::ok) {
    e.payload.gimbal_valid = 1;
    e.payload.yaw = static_cast<float>(s.gimbal.yaw);
    e.payload.pitch = static_cast<float>(s.gimbal.pitch);
  }
  e.target = s.target_state.freshness(p.target_timeout, now, true, ros_now, p.future_tolerance);
  if (e.target == Reason::ok && !s.target.valid) e.target = Reason::invalid;
  if (e.target == Reason::ok && (s.target.target_id < 1 || s.target.target_id > 8))
    e.target = Reason::target_id;
  if (e.target == Reason::ok && (!std::isfinite(s.target.x) || !std::isfinite(s.target.y)))
    e.target = Reason::nonfinite;
  return e;
}
}  // namespace mc02_serial_bridge
#endif  // MC02_SERIAL_BRIDGE__STATE_HPP_
