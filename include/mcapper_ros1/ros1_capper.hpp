#pragma once

#include <mcapper/capper.hpp>
#include <mcapper/capper_config.hpp>
#include <memory>
#include <ros/ros.h>
#include <string>
#include <vector>

namespace mcapper_ros1
{

    class Ros1Capper final : public mcapper::Capper
    {
    public:
        Ros1Capper(ros::NodeHandle& node_handle, mcapper::CapperConfig config);
        ~Ros1Capper() override;

        bool               start() override;
        bool               trigger() override;
        void               stop() override;
        const std::string& last_output_path() const noexcept override;

    private:
        struct Impl;

        std::unique_ptr<Impl> impl_;
    };

}  // namespace mcapper_ros1
