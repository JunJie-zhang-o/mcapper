#include <gtest/gtest.h>
#include "recorder.hpp"
#include "codec/ros2.hpp"
#include <std_msgs/msg/string.hpp>

using namespace flightLogger;

// Test case 1: Provide explicit schema definition in callback-like scenario
TEST(MessageDefinitionTest, ExplicitSchema) {
    // Simulate receiving a ROS2 message and constructing codec with raw .msg definition
    const std::string schema_def = "string data"; // simplified .msg content
    Ros2Codec<std_msgs::msg::String> codec(schema_def);
    auto schema = codec.schema();
    EXPECT_EQ(schema.name, "std_msgs/msg/String");
    EXPECT_EQ(schema.encoding, "ros2msg");
    // The raw definition should be stored in schema.data
    std::string stored_def(reinterpret_cast<const char*>(schema.data.data()), schema.data.size());
    EXPECT_EQ(stored_def, schema_def);
}

// Test case 2: Directly pass message type and derive the installed .msg definition
TEST(MessageDefinitionTest, ImplicitSchema) {
    Ros2Codec<std_msgs::msg::String> codec;
    auto schema = codec.schema();
    EXPECT_EQ(schema.name, "std_msgs/msg/String");
    EXPECT_EQ(schema.encoding, "ros2msg");
    EXPECT_FALSE(schema.data.empty());

    std::string stored_def(reinterpret_cast<const char*>(schema.data.data()), schema.data.size());
    EXPECT_NE(stored_def.find("string data"), std::string::npos);
}
