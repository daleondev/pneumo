#include "pneumo/meta.hpp"

#include <cstdlib>

auto main() -> int
{
    return pnm::meta::source::detail::find_embedded_source(__FILE__).has_value() ? EXIT_FAILURE
                                                                                 : EXIT_SUCCESS;
}
