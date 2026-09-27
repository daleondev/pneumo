#include "pneumo/meta.hpp"

#include <cstdlib>
#include <string_view>

PNM_META_SOURCE_EMBED_CURRENT

namespace
{
    // Exercise non-ASCII bytes on targets where plain char is signed, too.
    constexpr auto MARKER = std::string_view{ "single-tu-source-embed-marker-50 °C" };
}

auto main() -> int
{
    const auto embedded = pnm::meta::source::detail::find_embedded_source(__FILE__);
    if (!embedded.has_value()) {
        return EXIT_FAILURE;
    }

    if (embedded->source_code.find(MARKER) == std::string_view::npos) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
