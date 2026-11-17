#include "mc02_serial_bridge/protocol.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace mc02_serial_bridge
{
namespace
{
void put_u16(std::array<uint8_t, kFrameSize> & out, size_t offset, uint16_t value)
{
  out[offset] = static_cast<uint8_t>(value & 0xff);
  out[offset + 1] = static_cast<uint8_t>(value >> 8);
}

uint16_t get_u16(const uint8_t * data, size_t offset)
{
  return static_cast<uint16_t>(data[offset]) |
         static_cast<uint16_t>(static_cast<uint16_t>(data[offset + 1]) << 8);
}

void put_f32(std::array<uint8_t, kFrameSize> & out, size_t offset, float value)
{
  uint32_t bits{};
  static_assert(sizeof(value) == 4 && std::numeric_limits<float>::is_iec559,
    "IEEE-754 binary32 required");
  std::memcpy(&bits, &value, sizeof(bits));
  for (size_t i = 0; i < 4; ++i) out[offset + i] = static_cast<uint8_t>(bits >> (i * 8));
}

float get_f32(const uint8_t * data, size_t offset)
{
  uint32_t bits{};
  for (size_t i = 0; i < 4; ++i) bits |= static_cast<uint32_t>(data[offset + i]) << (i * 8);
  float value{};
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool flags_valid(const ControlPayload & p)
{
  return p.nav_valid <= 1 && p.gimbal_valid <= 1 && p.control_enabled <= 1 && p.reserved == 0 &&
         (p.gimbal_valid == 0 || p.control_enabled == 1);
}
}  // namespace

uint16_t crc16(const uint8_t * data, size_t length)
{
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 1) ? static_cast<uint16_t>((crc >> 1) ^ 0x8408) : static_cast<uint16_t>(crc >> 1);
  }
  return crc;
}

std::array<uint8_t, kFrameSize> encode(const ControlPayload & p, uint16_t sequence)
{
  std::array<uint8_t, kFrameSize> out{};
  out[0] = 'S';
  out[1] = 'P';
  out[2] = kProtocolVersion;
  out[3] = kControlMessageId;
  put_u16(out, 4, sequence);
  put_u16(out, 6, kPayloadSize);
  put_f32(out, 8, p.vx);
  put_f32(out, 12, p.vy);
  put_f32(out, 16, p.wz);
  put_f32(out, 20, p.yaw);
  put_f32(out, 24, p.pitch);
  out[28] = p.nav_valid;
  out[29] = p.gimbal_valid;
  out[30] = p.control_enabled;
  out[31] = p.reserved;
  const uint16_t checksum = crc16(out.data(), kFrameSize - 2);
  put_u16(out, kFrameSize - 2, checksum);
  return out;
}

std::optional<DecodedFrame> decode(const uint8_t * data, size_t length)
{
  if (data == nullptr || length != kFrameSize || data[0] != 'S' || data[1] != 'P' ||
      data[2] != kProtocolVersion || data[3] != kControlMessageId ||
      get_u16(data, 6) != kPayloadSize || get_u16(data, kFrameSize - 2) != crc16(data, kFrameSize - 2))
    return std::nullopt;

  DecodedFrame result;
  result.sequence = get_u16(data, 4);
  auto & p = result.payload;
  p.vx = get_f32(data, 8);
  p.vy = get_f32(data, 12);
  p.wz = get_f32(data, 16);
  p.yaw = get_f32(data, 20);
  p.pitch = get_f32(data, 24);
  p.nav_valid = data[28];
  p.gimbal_valid = data[29];
  p.control_enabled = data[30];
  p.reserved = data[31];
  if (!flags_valid(p) || !std::isfinite(p.vx) || !std::isfinite(p.vy) || !std::isfinite(p.wz) ||
      !std::isfinite(p.yaw) || !std::isfinite(p.pitch) ||
      (p.nav_valid == 0 && (p.vx != 0 || p.vy != 0 || p.wz != 0)) ||
      (p.gimbal_valid == 0 && (p.yaw != 0 || p.pitch != 0)) ||
      (p.control_enabled == 0 && p.gimbal_valid != 0))
    return std::nullopt;
  return result;
}

std::vector<DecodedFrame> StreamDecoder::feed(const uint8_t * data, size_t length)
{
  std::vector<DecodedFrame> frames;
  if (data == nullptr) return frames;
  // Consume incrementally: even a large/noisy input cannot grow internal storage past 34 bytes.
  for (size_t i = 0; i < length; ++i) {
    buffer_.push_back(data[i]);
    while (!buffer_.empty()) {
      if (buffer_[0] != 'S' || (buffer_.size() >= 2 && buffer_[1] != 'P')) {
        buffer_.erase(buffer_.begin());
        continue;
      }
      if (buffer_.size() < kFrameSize) break;
      auto frame = decode(buffer_.data(), kFrameSize);
      if (frame) { frames.push_back(*frame); buffer_.clear(); }
      else buffer_.erase(buffer_.begin());
    }
  }
  return frames;
}
}  // namespace mc02_serial_bridge
