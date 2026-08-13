#include <cassert>
#include <fstream>
#include <mcapper/capper_config.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

    void writeFile(const std::string& path, const std::string& text)
    {
        std::ofstream out(path);
        out << text;
    }

}  // namespace

int main()
{
    writeFile("capper_config_valid.yaml",
              "logger:\n"
              "  output_directory: logs\n"
              "  base_filename: ros1_capture\n"
              "  ring_buffer_capacity: 10000\n"
              "  pre_trigger_duration_ns: 5000000000\n"
              "  post_trigger_duration_ns: 0\n"
              "source:\n"
              "  type: ros1\n"
              "  topics:\n"
              "    - /camera/image_raw\n"
              "    - /imu/data\n");

    const auto config = mcapper::loadYamlConfig("capper_config_valid.yaml");
    assert(config.logger.output_directory == "logs");
    assert(config.logger.base_filename == "ros1_capture");
    assert(config.logger.ring_buffer_capacity == 10000);
    assert(config.logger.pre_trigger_duration_ns == 5'000'000'000ULL);
    assert(config.logger.post_trigger_duration_ns == 0);
    assert(config.source.type == mcapper::SourceType::Ros1);
    assert((config.source.topics == std::vector<std::string>{"/camera/image_raw", "/imu/data"}));

    writeFile("capper_config_empty_topics.yaml",
              "source:\n"
              "  type: ros1\n"
              "  topics: []\n");
    try
    {
        (void)mcapper::loadYamlConfig("capper_config_empty_topics.yaml");
        assert(false);
    }
    catch (const std::invalid_argument&)
    {
    }

    writeFile("capper_config_empty_topic.yaml",
              "source:\n"
              "  type: ros1\n"
              "  topics:\n"
              "    - ''\n");
    try
    {
        (void)mcapper::loadYamlConfig("capper_config_empty_topic.yaml");
        assert(false);
    }
    catch (const std::invalid_argument&)
    {
    }

    return 0;
}
