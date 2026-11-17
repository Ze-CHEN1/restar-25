#include "mc02_serial_bridge/protocol.hpp"
#include "crc.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <vector>
int main()
{
  using mc02_serial_bridge::crc16;
  const std::array<uint8_t,9> check{'1','2','3','4','5','6','7','8','9'};
  assert(crc16(nullptr, 0) == 0xffff);
  assert(crc16(check.data(), check.size()) == 0x6f91);
  const std::array<uint8_t,2> sp{'S','P'};
  assert(crc16(sp.data(), sp.size()) == 0x5ba2);
  std::vector<uint8_t> sequence;
  for (int i=0; i<256; ++i) sequence.push_back(static_cast<uint8_t>(i));
  assert(crc16(sequence.data(), sequence.size()) == 0xcfc3);
  std::vector<uint8_t> data(4096);
  uint32_t seed = 0x12345678;
  for (auto & b : data) { seed = seed * 1664525U + 1013904223U; b = seed >> 24; }
  for (size_t len=0; len<=data.size(); ++len)
    assert(crc16(data.data(), len) == tools::get_crc16(data.data(), len));
  auto frame = mc02_serial_bridge::encode({.1F,-.2F,.3F,.25F,-.1F,1,1,1,0},0x1234);
  assert(tools::check_crc16(frame.data(), frame.size()));
  std::cout << "Original Restar tools/crc.cpp versus bitwise CRC: 4097 deterministic prefixes PASS; "
    "empty=ffff 123456789=6f91 SP=5ba2 00..ff=cfc3 golden=4385 (wire 85 43)\n";
}
