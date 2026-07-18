#include "multi_tu_probe.hpp"
#include "pneumo/meta.hpp"

#include <string_view>

PNM_META_SOURCE_EMBED_CURRENT

namespace source_embed_multi_tu_probe
{
    namespace
    {
        constexpr auto MARKER = std::string_view{ "multi-tu-second-source-embed-marker" };
    }

    auto second_file() -> std::string_view { return __FILE__; }

    auto second_marker() -> std::string_view { return MARKER; }
}