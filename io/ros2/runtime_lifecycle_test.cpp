#include "runtime.hpp"
#include "publish2nav.hpp"
#include <iostream>

// Immediate stop exercises the race between thread start and executor cancellation.
int main()
{
  try {
    for (int i = 0; i < 30; ++i) {
      auto first = std::make_unique<io::RosRuntime>();
      io::RosRuntime second;
      auto a_options = first->node_options();
      a_options.arguments({"--ros-args", "-r", "__ns:=/lifecycle_first"});
      auto a = std::make_shared<io::Publish2Nav>(a_options);
      auto b_options = second.node_options();
      b_options.arguments({"--ros-args", "-r", "__ns:=/lifecycle_second"});
      auto b = std::make_shared<io::Publish2Nav>(b_options);
      first->add(a);
      second.add(b);
      first->start();
      second.start();
      first->stop();
      a.reset();
      first.reset();  // Shut down the first context while the second is still alive.
      b->send_command(io::Command{true, false, 0.1, -0.2});
      second.stop();
      b.reset();
    }
    std::cout << "PASS 30 lifecycle cycles: immediate stop, thread join, independent contexts" << std::endl;
    return 0;
  } catch (const std::exception & e) {
    std::cerr << "FAIL " << e.what() << std::endl;
    return 1;
  }
}
