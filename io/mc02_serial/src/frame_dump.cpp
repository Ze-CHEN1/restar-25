// Standalone file decoder: no ROS or serial hardware required.
#include "mc02_serial_bridge/protocol.hpp"
#include <array>
#include <fstream>
#include <iostream>
int main(int argc, char ** argv)
{
  if (argc != 2) { std::cerr << "usage: mc02_frame_dump capture.bin\n"; return 2; }
  std::ifstream input(argv[1], std::ios::binary);
  if (!input) return 2;
  mc02_serial_bridge::StreamDecoder decoder;
  std::array<uint8_t, 257> chunk;
  size_t bytes = 0, count = 0;
  std::cout << "sequence,vx,vy,wz,yaw,pitch,nav_valid,gimbal_valid,control_enabled\n";
  while (input.read(reinterpret_cast<char *>(chunk.data()), chunk.size()) || input.gcount()) {
    bytes += static_cast<size_t>(input.gcount());
    for (const auto & f : decoder.feed(chunk.data(), static_cast<size_t>(input.gcount()))) {
      const auto & p = f.payload; ++count;
      std::cout << f.sequence << ',' << p.vx << ',' << p.vy << ',' << p.wz << ',' << p.yaw << ','
        << p.pitch << ',' << unsigned(p.nav_valid) << ',' << unsigned(p.gimbal_valid) << ','
        << unsigned(p.control_enabled) << '\n';
    }
  }
  std::cerr << "bytes=" << bytes << " decoded=" << count << " skipped_or_trailing="
    << bytes - count * mc02_serial_bridge::kFrameSize << '\n';
  return input.bad() || count == 0 || bytes != count * mc02_serial_bridge::kFrameSize ? 1 : 0;
}
