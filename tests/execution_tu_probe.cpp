#include "pneumo/coroutines.hpp"

auto execution_tu_probe() -> int
{
    namespace ex = pnm::coro::execution;
    auto result{ ex::sync_wait(ex::on(ex::get_parallel_scheduler(), ex::just(7))) };
    return std::get<0>(*result);
}
