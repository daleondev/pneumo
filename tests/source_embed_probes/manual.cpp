#include "pneumo/meta.hpp"

#include <cstdlib>
#include <string_view>

// clang-format off
PNM_META_SOURCE_EMBED_BEGIN(__FILE__)
    'm', 'a', 'n', 'u', 'a', 'l', '-', 's', 'o', 'u', 'r', 'c', 'e'
PNM_META_SOURCE_EMBED_END;
// clang-format on

auto main() -> int
{
    constexpr auto marker = std::string_view{ "manual-source" };
    const auto embedded = pnm::meta::source::detail::find_embedded_source(__FILE__);
    return embedded && embedded->source_code == marker ? EXIT_SUCCESS : EXIT_FAILURE;
}
