#include "multi_tu_probe.hpp"
#include "pneumo/meta.hpp"

#include <cstdlib>
#include <string_view>

namespace
{
    auto registry_contains(std::string_view file_name, std::string_view marker) -> bool
    {
        if (const auto embedded = pnm::meta::source::detail::Registry::instance()[file_name]) {
            return embedded->source_code.find(marker) != std::string_view::npos;
        }

        return false;
    }
}

auto main() -> int
{
    if (!registry_contains(source_embed_multi_tu_probe::first_file(),
                           source_embed_multi_tu_probe::first_marker())) {
        return EXIT_FAILURE;
    }

    if (!registry_contains(source_embed_multi_tu_probe::second_file(),
                           source_embed_multi_tu_probe::second_marker())) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}