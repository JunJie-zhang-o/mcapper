#include "bridge/ros1/config.hpp"

#include <stdexcept>

namespace flightLogger
{

    void Ros1TopicConfig::validate() const
    {
        if (topic.empty())
        {
            throw std::invalid_argument("source target is empty");
        }
        if (topic.find(':') != std::string::npos)
        {
            throw std::invalid_argument("source topic must not contain ':'");
        }
        if (pre_capacity == 0)
        {
            throw std::invalid_argument("invalid positive integer value for --source pre capacity");
        }
        if (post_capacity == 0)
        {
            throw std::invalid_argument("invalid positive integer value for --source post capacity");
        }
    }

}  // namespace flightLogger
