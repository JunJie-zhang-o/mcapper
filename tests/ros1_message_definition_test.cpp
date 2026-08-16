#include <gtest/gtest.h>

#include <std_msgs/String.h>

#include "codec/ros1.hpp"

using namespace flightLogger;

TEST(Ros1MessageDefinitionTest, ExplicitTypeSchema)
{
    Ros1Codec<std_msgs::String> codec;
    const auto                  schema = codec.schema();

    EXPECT_EQ(schema.name, "std_msgs/String");
    EXPECT_EQ(schema.encoding, "ros1msg");
    EXPECT_FALSE(schema.data.empty());

    const std::string stored_def(reinterpret_cast<const char*>(schema.data.data()), schema.data.size());
    EXPECT_NE(stored_def.find("string data"), std::string::npos);
}

TEST(Ros1MessageDefinitionTest, DetailHelpers)
{
    EXPECT_EQ(detail::ros1_message_type<std_msgs::String>(), "std_msgs/String");
    EXPECT_FALSE(detail::raw_ros1_message_definition<std_msgs::String>().empty());
    EXPECT_FALSE(detail::ros1_message_md5<std_msgs::String>().empty());
}
