#include "pneumo/meta.hpp"

#include <cstdlib>
#include <string_view>

PNM_META_SOURCE_EMBED_CURRENT

namespace
{
    constexpr auto MARKER = std::string_view{ "single-tu-source-embed-marker" };
}

auto main() -> int
{
    const auto embedded = pnm::meta::source::detail::Registry::instance()[__FILE__];
    if (!embedded.has_value()) {
        return EXIT_FAILURE;
    }

    if (embedded->source_code.find(MARKER) == std::string_view::npos) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}