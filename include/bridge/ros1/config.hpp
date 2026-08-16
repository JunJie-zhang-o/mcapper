#pragma once

#include <cstddef>
#include <string>

namespace flightLogger
{

    struct Ros1TopicConfig
    {
        std::string topic;
        std::size_t pre_capacity{4096};
        std::size_t post_capacity{4096};

        void validate() const;
    };

}  // namespace flightLogger
