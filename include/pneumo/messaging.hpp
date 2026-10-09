#pragma once

#include "pneumo/meta.hpp"

namespace pnm::msg
{

    template<pnm::utils::memory::Serializable T>
    class Topic
    {
        Topic(std::string_view topic_name);
        ~Topic() = default;
    };

    class Bus
    {
    };

}