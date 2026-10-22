// SPDX-License-Identifier: Apache-2.0
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/u_int32.hpp>

int main(int argc, char **argv)
{
  using namespace std::chrono_literals;
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions options;
  options.start_parameter_services(false).start_parameter_event_publisher(false).enable_rosout(
      false);
  options.parameter_overrides({rclcpp::Parameter("enable_type_description_service", false)});
  auto node = std::make_shared<rclcpp::Node>("c6_example_peer", options);
  const auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).best_effort().durability_volatile();
  auto publisher = node->create_publisher<std_msgs::msg::UInt32>("/c6/command", qos);
  std::array<bool, 11> received{};
  unsigned count = 0;
  auto subscription = node->create_subscription<std_msgs::msg::UInt32>(
      "/c6/state", qos, [&](std_msgs::msg::UInt32::ConstSharedPtr message) {
        const auto value = message->data;
        if (value >= 1 && value <= 10 && !received[value]) {
          received[value] = true;
          ++count;
          std::printf("state=%u\n", value);
        }
      });
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  const auto deadline = std::chrono::steady_clock::now() + 60s;
  auto next_send = std::chrono::steady_clock::now();
  while (rclcpp::ok() && count < 10 && std::chrono::steady_clock::now() < deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_send) {
      unsigned value = 1;
      while (value <= 10 && received[value])
        ++value;
      std_msgs::msg::UInt32 command;
      command.data = value;
      publisher->publish(command);
      next_send = now + 500ms;
    }
    executor.spin_once(100ms);
  }
  executor.remove_node(node);
  subscription.reset();
  publisher.reset();
  node.reset();
  rclcpp::shutdown();
  if (count != 10) {
    std::fprintf(stderr, "FAIL: received %u/10 values; check addresses, Wi-Fi and QoS\n", count);
    return 1;
  }
  std::puts(
      "PASS: ten distinct values returned. Functional smoke only; no latency or lifecycle claim.");
  return 0;
}
