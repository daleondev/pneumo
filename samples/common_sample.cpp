#include "pneumo/common.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <queue>
#include <string>

// NOLINTBEGIN

namespace
{
    auto decode_message_id(const std::array<std::byte, sizeof(std::uint32_t)>& payload)
      -> pnm::Result<std::uint32_t>
    {
        std::uint32_t value{ 0 };
        if (!pnm::utils::memory::copy(value, payload)) {
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        }
        return value;
    }

    auto print_memory_example() -> void
    {
        constexpr std::uint32_t message_id{ 0x1234ABCDU };

        std::array<std::byte, sizeof(message_id)> payload{};
        const auto copied = pnm::utils::memory::copy(payload, message_id);
        const auto decoded = decode_message_id(payload);

        std::cout << "Memory helpers\n";
        std::cout << "  copied: " << std::boolalpha << copied << '\n';
        std::cout << "  decoded message id: 0x" << std::hex << decoded.value() << std::dec << "\n\n";
    }

    auto print_queue_example() -> void
    {
        auto jobs = std::queue<std::string>{};

        pnm::utils::queue::push(jobs, std::string{ "decode header" });
        pnm::utils::queue::push(jobs, std::string{ "publish telemetry" });

        std::cout << "Queue helpers\n";
        while (const auto next_job = pnm::utils::queue::pop(jobs)) {
            std::cout << "  next job: " << *next_job << '\n';
        }
        std::cout << '\n';
    }

    auto print_bit_example() -> void
    {
        std::uint8_t flags{ 0 };
        pnm::utils::bit::set<0>(flags);
        pnm::utils::bit::set_masked_checked<2, 3>(flags, static_cast<std::uint8_t>(0b101));

        std::cout << "Bit helpers\n";
        std::cout << "  flags: 0b" << pnm::utils::bit::to_bitset(flags) << '\n';
        std::cout << "  ready bit set: " << pnm::utils::bit::check<0>(flags) << '\n';
        std::cout << "  mode field: " << static_cast<int>(pnm::utils::bit::get_masked<2, 3>(flags)) << "\n\n";
    }
}

auto main() -> int
{
    print_memory_example();
    print_queue_example();
    print_bit_example();

    return 0;
}

// NOLINTEND
