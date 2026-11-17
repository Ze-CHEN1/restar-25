cmake_minimum_required(VERSION 3.16)

find_package(yaml-cpp REQUIRED)

add_subdirectory(serial)

# 创建目标 io
add_library(io STATIC 
    hikrobot/hikrobot.cpp    
    mindvision/mindvision.cpp  
    usbcamera/usbcamera.cpp  
    camera.cpp
    cboard.cpp
    dm_imu/dm_imu.cpp
    gimbal/gimbal.cpp
)

# hikrobot
target_include_directories(io PUBLIC hikrobot/include)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  target_link_directories(io PUBLIC hikrobot/lib/amd64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  target_link_directories(io PUBLIC hikrobot/lib/arm64)
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_HOST_SYSTEM_PROCESSOR}!")
endif()

# mindvision
target_include_directories(io PUBLIC mindvision/include)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
  target_link_directories(io PUBLIC mindvision/lib/amd64)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
  target_link_directories(io PUBLIC mindvision/lib/arm64)
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_HOST_SYSTEM_PROCESSOR}!")
endif()

target_link_libraries(io MvCameraControl MVSDK usb-1.0 yaml-cpp serial)

# Dependencies are checked once by the root CMake, with explicit failure when enabled.
if(RESTAR_WITH_ROS2)
  target_sources(io PRIVATE ros2/publish2nav.cpp ros2/ros2.cpp ros2/subscribe2nav.cpp)
  ament_target_dependencies(io rclcpp std_msgs restar_msgs robot_interfaces)
endif()
