#ifndef MC02_SERIAL_BRIDGE__PROTOCOL_HPP_
#define MC02_SERIAL_BRIDGE__PROTOCOL_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mc02_serial_bridge
{
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kControlMessageId = 1;
constexpr size_t kPayloadSize = 24;
constexpr size_t kFrameSize = 34;

struct ControlPayload
{
  float vx{};
  float vy{};
  float wz{};
  float yaw{};
  float pitch{};
  uint8_t nav_valid{};
  uint8_t gimbal_valid{};
  uint8_t control_enabled{};
  uint8_t reserved{};
};

struct DecodedFrame
{
  uint16_t sequence{};
  ControlPayload payload;
};

uint16_t crc16(const uint8_t * data, size_t length);
std::array<uint8_t, kFrameSize> encode(const ControlPayload & payload, uint16_t sequence);
std::optional<DecodedFrame> decode(const uint8_t * data, size_t length);

class StreamDecoder
{
public:
  std::vector<DecodedFrame> feed(const uint8_t * data, size_t length);
  size_t buffered_bytes() const { return buffer_.size(); }

private:
  std::vector<uint8_t> buffer_;
};
}  // namespace mc02_serial_bridge

#endif  // MC02_SERIAL_BRIDGE__PROTOCOL_HPP_
