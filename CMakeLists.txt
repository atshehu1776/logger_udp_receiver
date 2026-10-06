cmake_minimum_required(VERSION 3.8)

project(logger_udp_receiver)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)

add_executable(logger_udp_receiver
  src/logger_udp_receiver.cpp
)

ament_target_dependencies(
  logger_udp_receiver
  rclcpp
  std_msgs
  sensor_msgs
)

install(
  TARGETS
    logger_udp_receiver
  DESTINATION
    lib/${PROJECT_NAME}
)

install(
  DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
