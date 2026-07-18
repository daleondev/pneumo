#pragma once

#include <string_view>

namespace source_embed_multi_tu_probe
{
    auto first_file() -> std::string_view;
    auto second_file() -> std::string_view;
    auto first_marker() -> std::string_view;
    auto second_marker() -> std::string_view;
}