#include <csignal>
#include <iostream>
#include <mcapper/capper_config.hpp>
#include <mcapper_ros1/ros1_capper.hpp>
#include <memory>
#include <ros/ros.h>

namespace
{

    std::unique_ptr<mcapper_ros1::Ros1Capper> g_capper;

    void sigintHandler(int)
    {
        if (g_capper)
        {
            g_capper->trigger();
            g_capper->stop();
            std::cout << "[INFO] Saved MCAP to: " << g_capper->last_output_path() << std::endl;
        }
        ros::shutdown();
    }

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <config.yaml>" << std::endl;
        return 1;
    }

    ros::init(argc, argv, "shape_shifter_mcap_test_node", ros::init_options::AnonymousName | ros::init_options::NoSigintHandler);
    ros::NodeHandle node_handle;

    auto config = mcapper::loadYamlConfig(argv[1]);
    g_capper    = std::make_unique<mcapper_ros1::Ros1Capper>(node_handle, std::move(config));
    g_capper->start();

    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigintHandler);

    ros::spin();

    if (g_capper)
    {
        g_capper->stop();
    }

    return 0;
}
