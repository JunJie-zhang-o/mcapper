#pragma once

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace flightLogger
{

    using SerializedPayload = std::vector<std::byte>;

    template <typename T>
    class ISerializer
    {
    public:
        virtual ~ISerializer() = default;

        virtual SerializedPayload serialize(const T& data) = 0;
    };

    template <typename T>
    class FunctionSerializer final : public ISerializer<T>
    {
    public:
        using Function = std::function<SerializedPayload(const T&)>;

        explicit FunctionSerializer(Function serialize) : serialize_(std::move(serialize))
        {
            if (!this->serialize_) throw std::invalid_argument("serializer function is empty");
        }

        SerializedPayload serialize(const T& data) override
        {
            return this->serialize_(data);
        }

    private:
        Function serialize_;
    };

}  // namespace flightLogger
