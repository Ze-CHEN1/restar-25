#include "mc02_serial_bridge/protocol.hpp"
#include "mc02_serial_bridge/state.hpp"
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <vector>
using namespace std::chrono_literals;
using namespace mc02_serial_bridge;

void protocol_checks()
{
  ControlPayload payload{0.1F,-0.2F,0.3F,0.25F,-0.1F,1,1,1,0};
  const auto frame = encode(payload, 0x1234);
  const std::array<uint8_t, kFrameSize> expected{
    0x53,0x50,0x01,0x01,0x34,0x12,0x18,0x00,0xcd,0xcc,0xcc,0x3d,
    0xcd,0xcc,0x4c,0xbe,0x9a,0x99,0x99,0x3e,0x00,0x00,0x80,0x3e,
    0xcd,0xcc,0xcc,0xbd,0x01,0x01,0x01,0x00,0x85,0x43};
  assert(frame == expected);
  auto decoded = decode(frame.data(), frame.size());
  assert(decoded && decoded->sequence == 0x1234);
  assert(decoded->payload.vx == payload.vx && decoded->payload.vy == payload.vy &&
    decoded->payload.wz == payload.wz && decoded->payload.yaw == payload.yaw &&
    decoded->payload.pitch == payload.pitch);
  for (size_t len = 0; len < kFrameSize; ++len) assert(!decode(frame.data(), len));
  assert(!decode(nullptr, kFrameSize));
  assert(!decode(frame.data(), kFrameSize + 1));
  for (size_t i = 0; i < kFrameSize; ++i) for (unsigned bit = 0; bit < 8; ++bit) {
    auto corrupt = frame; corrupt[i] ^= 1U << bit;
    assert(!decode(corrupt.data(), corrupt.size()));
  }
  // Correct CRC does not make an unsupported header or payload acceptable.
  for (size_t offset : {size_t(2), size_t(3), size_t(6), size_t(7)}) {
    auto bad = frame; bad[offset] ^= 2;
    auto crc = crc16(bad.data(), 32); bad[32] = crc & 255; bad[33] = crc >> 8;
    assert(!decode(bad.data(), bad.size()));
  }
  const auto reject = [](ControlPayload p) {
    auto f = encode(p, 1); assert(!decode(f.data(), f.size()));
  };
  reject({0,0,0,0,0,0,2,0,0}); reject({0,0,0,0,0,0,1,0,0});
  reject({0,0,0,0,0,0,0,2,0}); reject({0,0,0,0,0,0,0,0,1});
  reject({1,0,0,0,0,0,0,0,0}); reject({0,0,0,1,0,0,0,0,0});
  reject({std::numeric_limits<float>::quiet_NaN(),0,0,0,0,1,0,0,0});
  reject({0,0,0,std::numeric_limits<float>::infinity(),0,0,1,1,0});
  for (size_t split = 0; split <= kFrameSize; ++split) {
    StreamDecoder stream;
    auto a = stream.feed(frame.data(), split);
    auto b = stream.feed(frame.data() + split, kFrameSize - split);
    assert(a.size() + b.size() == 1);
  }
  StreamDecoder null_stream;
  assert(null_stream.feed(nullptr, 100).empty());
  StreamDecoder stream;
  auto bad = frame; bad[10] ^= 1;
  std::vector<uint8_t> bytes(10000, 0xee);
  bytes.push_back('S'); bytes.insert(bytes.end(), bad.begin(), bad.end());
  bytes.insert(bytes.end(), frame.begin(), frame.end());
  bytes.insert(bytes.end(), frame.begin(), frame.end());
  auto frames = stream.feed(bytes.data(), bytes.size());
  assert(frames.size() == 2 && stream.buffered_bytes() < kFrameSize);
  auto wrap = encode({}, 65535); assert(decode(wrap.data(), wrap.size())->sequence == 65535);
  wrap = encode({}, 0); assert(decode(wrap.data(), wrap.size())->sequence == 0);
  std::cout << "protocol: golden frame, 272 bit flips, CRC-correct bad headers/payloads, "
    "all splits, noise/concatenation/recovery, sequence wrap PASS\n";
}
void state_checks()
{
  Policy policy;
  LatestState state(policy);
  const auto t0 = SteadyClock::time_point(100s);
  int64_t ros = 100000000000LL;
  auto check = [&](auto t) { return evaluate(state.snapshot(), policy, t, ros); };
  assert(check(t0).nav == Reason::waiting);
  geometry_msgs::msg::Twist nav;
  nav.linear.x = .4; nav.linear.y = -.2; nav.angular.z = .3;
  state.receive_nav(nav, t0);
  robot_interfaces::msg::GimbalCommand cmd;
  cmd.header.stamp.sec = 100; cmd.valid = cmd.control_enabled = true;
  cmd.yaw = .25; cmd.pitch = -.1;
  state.receive_gimbal(cmd, ros, t0);
  auto e = check(t0);
  assert(e.payload.nav_valid && e.payload.gimbal_valid);
  auto before = state.snapshot();
  for (int i = 0; i < 11; ++i) check(t0 + i * 20ms);
  assert(state.snapshot().nav_state.samples == before.nav_state.samples);
  assert(state.snapshot().gimbal_state.samples == before.gimbal_state.samples);
  // Navigation expires while vision is independently updated.
  ros += 210000000; cmd.header.stamp.nanosec = 210000000;
  state.receive_gimbal(cmd, ros, t0 + 210ms);
  e = check(t0 + 210ms);
  assert(!e.payload.nav_valid && e.payload.vx == 0 && e.payload.gimbal_valid);
  state.receive_nav(nav, t0 + 420ms);
  e = check(t0 + 420ms);
  assert(e.payload.nav_valid && !e.payload.gimbal_valid && !e.payload.control_enabled && e.payload.yaw == 0);
  // Every new invalid message replaces the last usable command immediately.
  auto send = [&](int ns) { cmd.header.stamp.nanosec = ns;
    ros = 100000000000LL + ns; state.receive_gimbal(cmd, ros, t0 + 500ms); };
  cmd.valid = false; send(300000000); assert(check(t0 + 500ms).gimbal == Reason::invalid);
  cmd.valid = true; cmd.control_enabled = false; send(310000000);
  assert(check(t0 + 500ms).gimbal == Reason::disabled);
  cmd.control_enabled = true; cmd.yaw = std::numeric_limits<double>::infinity(); send(320000000);
  assert(check(t0 + 500ms).gimbal == Reason::nonfinite);
  cmd.yaw = 1e100; send(330000000); assert(check(t0 + 500ms).gimbal == Reason::float_overflow);
  cmd.yaw = .25; send(340000000); assert(check(t0 + 500ms).payload.gimbal_valid);
  state.receive_gimbal(cmd, ros, t0 + 501ms);
  assert(check(t0 + 501ms).gimbal == Reason::repeated_stamp);
  cmd.header.stamp.nanosec = 335000000;
  state.receive_gimbal(cmd, ros, t0 + 502ms);
  assert(check(t0 + 502ms).gimbal == Reason::regressed_stamp);
  cmd.header.stamp.nanosec = 338000000;
  state.receive_gimbal(cmd, ros, t0 + 503ms); // Still below watermark, must remain rejected.
  assert(check(t0 + 503ms).gimbal == Reason::regressed_stamp);
  send(350000000); assert(check(t0 + 500ms).payload.gimbal_valid);
  cmd.header.stamp.sec = 0; cmd.header.stamp.nanosec = 0;
  state.receive_gimbal(cmd, ros, t0 + 504ms); assert(check(t0 + 504ms).gimbal == Reason::zero_stamp);
  cmd.header.stamp.sec = 100; cmd.header.stamp.nanosec = 1000000000U;
  state.receive_gimbal(cmd, ros, t0 + 505ms); assert(check(t0 + 505ms).gimbal == Reason::malformed_stamp);
  cmd.header.stamp.sec = 99; cmd.header.stamp.nanosec = 0;
  state.receive_gimbal(cmd, ros, t0 + 506ms); assert(check(t0 + 506ms).gimbal == Reason::old_stamp);
  cmd.header.stamp.sec = 101;
  state.receive_gimbal(cmd, ros, t0 + 507ms); assert(check(t0 + 507ms).gimbal == Reason::future_stamp);
  // Local ROS clock rollback quarantines the first input, then accepts increasing new stamps.
  ros = 50000000000LL; cmd.header.stamp.sec = 50;
  state.receive_gimbal(cmd, ros, t0 + 508ms); assert(check(t0 + 508ms).gimbal == Reason::clock_reset);
  ros += 10000000; cmd.header.stamp.nanosec = 10000000;
  state.receive_gimbal(cmd, ros, t0 + 509ms); assert(check(t0 + 509ms).payload.gimbal_valid);
  // Twist has no stamp; identical messages refresh only their own receive TTL.
  state.receive_nav(nav, t0 + 510ms); state.receive_nav(nav, t0 + 700ms);
  assert(check(t0 + 710ms).payload.nav_valid && !check(t0 + 710ms).payload.gimbal_valid);
  nav.angular.x = std::numeric_limits<double>::quiet_NaN(); state.receive_nav(nav, t0 + 711ms);
  assert(check(t0 + 711ms).nav == Reason::nonfinite && check(t0 + 711ms).payload.vx == 0);
  nav.angular.x = 0; nav.linear.x = 1e100; state.receive_nav(nav, t0 + 712ms);
  assert(check(t0 + 712ms).nav == Reason::float_overflow);
  robot_interfaces::msg::VisionTarget2D target;
  target.header.stamp = cmd.header.stamp; target.valid = true; target.target_id = 6;
  state.receive_target(target, ros, t0 + 713ms); assert(check(t0 + 713ms).target == Reason::ok);
  target.header.stamp.nanosec += 1; target.valid = false;
  state.receive_target(target, ros, t0 + 714ms); assert(check(t0 + 714ms).target == Reason::invalid);
  target.header.stamp.nanosec += 1; target.valid = true; target.target_id = 9;
  state.receive_target(target, ros, t0 + 715ms); assert(check(t0 + 715ms).target == Reason::target_id);
  std::cout << "state: independent TTL, no refresh by framing, invalid/disabled/NaN/Inf/overflow, "
    "zero/malformed/stale/future/repeated/regressed stamps, local clock reset, target validity PASS\n";
}
int main() { protocol_checks(); state_checks(); }
