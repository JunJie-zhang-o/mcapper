#include <rclcpp/rclcpp.hpp>
#include <upperlimb_msgs/msg/uplimb_state.hpp>

#include "codec/ros2.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

template <typename MessageT>
std::string raw_message_definition_from_message_type() {
  return flightLogger::detail::raw_ros2_message_definition<MessageT>();
}

template <typename MessageT>
std::string raw_message_definition_from_callback_message(
    const std::shared_ptr<MessageT> & /*message*/) {
  return raw_message_definition_from_message_type<MessageT>();
}

std::string get_definition_after_receiving_ros2_message() {
  auto node = rclcpp::Node::make_shared("ros2_message_definition_main");
  auto definition_promise = std::make_shared<std::promise<std::string>>();
  auto definition_future = definition_promise->get_future();

  auto subscription = node->create_subscription<upperlimb_msgs::msg::UplimbState>(
      "message_definition_main_topic",
      rclcpp::QoS(10),
      [definition_promise](const upperlimb_msgs::msg::UplimbState::SharedPtr message) {
        definition_promise->set_value(raw_message_definition_from_callback_message(message));
      });

  auto publisher = node->create_publisher<upperlimb_msgs::msg::UplimbState>(
      "message_definition_main_topic", rclcpp::QoS(10));

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);

  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (publisher->get_subscription_count() == 0 &&
         std::chrono::steady_clock::now() < deadline) {
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  upperlimb_msgs::msg::UplimbState message;

  while (definition_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
         std::chrono::steady_clock::now() < deadline) {
    publisher->publish(message);
    executor.spin_some();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  if (definition_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    throw std::runtime_error("timed out waiting for the subscription callback");
  }

  return definition_future.get();
}

void print_definition(const std::string & title, const std::string & definition) {
  std::cout << "\n=== " << title << " ===\n";
  std::cout << definition << '\n';
}

void require_non_empty_definition(const std::string & definition) {
  if (definition.empty()) {
    throw std::runtime_error("raw message definition was empty");
  }
}

}  // namespace

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);

  try {
    const auto callback_definition = get_definition_after_receiving_ros2_message();
    print_definition("callback received upperlimb_msgs/msg/UplimbState", callback_definition);
    require_non_empty_definition(callback_definition);

    const auto explicit_type_definition =
      raw_message_definition_from_message_type<upperlimb_msgs::msg::UplimbState>();
    print_definition("explicit type upperlimb_msgs/msg/UplimbState", explicit_type_definition);
    require_non_empty_definition(explicit_type_definition);

    std::cout << "\nBoth ROS2-only message definition examples succeeded.\n";
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception & error) {
    std::cerr << "ros2_message_definition_main failed: " << error.what() << '\n';
    rclcpp::shutdown();
    return 1;
  }
}
