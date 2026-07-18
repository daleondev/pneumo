#include "pneumo/common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <mutex>
#include <queue>
#include <system_error>
#include <type_traits>
#include <vector>

namespace
{
    static_assert(std::same_as<pnm::Result<int>, std::expected<int, std::error_code>>);
    static_assert(pnm::utils::bit::size<std::uint8_t>() == 8);
    static_assert(pnm::utils::bit::bit_mask<5, std::uint8_t>() == std::uint8_t{ 0b00100000 });
    static_assert(pnm::utils::bit::get<5, std::uint8_t>() == std::uint8_t{ 0b00100000 });
    static_assert(pnm::utils::bit::mask<2, 3, std::uint8_t>() == std::uint8_t{ 0b00011100 });
    static_assert(pnm::utils::bit::mask<0, 0, std::uint8_t>() == std::uint8_t{ 0 });
    static_assert(pnm::utils::bit::mask<0, 8, std::uint8_t>() == std::uint8_t{ 0xFFU });
    static_assert(pnm::utils::bit::fits_mask<2, 3>(std::uint8_t{ 0b111 }));
    static_assert(!pnm::utils::bit::fits_mask<2, 3>(std::uint8_t{ 0b1000 }));
    static_assert(pnm::utils::bit::get_masked<2, 3>(std::uint8_t{ 0b00010100 }) == std::uint8_t{ 0b101 });
    static_assert(pnm::utils::bit::to_bitset(std::uint8_t{ 0b101 }).test(0));
    static_assert(!pnm::utils::bit::to_bitset(std::uint8_t{ 0b101 }).test(1));
    static_assert(pnm::utils::bit::to_bitset(std::uint8_t{ 0b101 }).test(2));
}

TEST(CommonResultTests, ResultAliasSupportsValuesAndErrors)
{
    const auto ok_result = pnm::Result<int>{ 42 };
    const auto err_result =
      pnm::Result<int>{ std::unexpected(std::make_error_code(std::errc::invalid_argument)) };

    ASSERT_TRUE(ok_result.has_value());
    EXPECT_EQ(*ok_result, 42);

    ASSERT_FALSE(err_result.has_value());
    EXPECT_EQ(err_result.error(), std::make_error_code(std::errc::invalid_argument));
}

TEST(CommonMemoryTests, CopyBetweenSerializableValues)
{
    constexpr std::uint32_t source{ 0xDEADBEEFU };
    std::uint32_t destination{ 0 };

    EXPECT_TRUE(pnm::utils::memory::copy(destination, source));
    EXPECT_EQ(destination, source);
}

TEST(CommonMemoryTests, CopyBetweenValueAndByteBufferRoundTrips)
{
    constexpr std::uint32_t source{ 0x1234ABCDU };
    std::array<std::byte, sizeof(source)> bytes{};
    std::uint32_t restored{ 0 };

    EXPECT_TRUE(pnm::utils::memory::copy(bytes, source));
    EXPECT_TRUE(pnm::utils::memory::copy(restored, bytes));
    EXPECT_EQ(restored, source);
}

TEST(CommonMemoryTests, CopyRejectsMismatchedSizes)
{
    constexpr std::uint32_t source{ 0x12345678U };
    std::array<std::byte, sizeof(source) - 1> too_small{};

    EXPECT_FALSE(pnm::utils::memory::copy(too_small, source));
}

TEST(CommonMemoryTests, CopyArray)
{
    constexpr std::array<std::uint16_t, 4> source{ 0x1111U, 0x2222U, 0x3333U, 0x4444U };
    std::array<std::uint16_t, 4> destination{};

    EXPECT_TRUE(pnm::utils::memory::copy(destination, source));
    EXPECT_EQ(destination, source);
}

TEST(CommonMemoryTests, CopyArrayDifferentSizesFails)
{
    constexpr std::array<std::uint16_t, 4> source{ 0x1111U, 0x2222U, 0x3333U, 0x4444U };
    std::array<std::uint16_t, 3> destination{};

    EXPECT_FALSE(pnm::utils::memory::copy(destination, source));
}

TEST(CommonQueueTests, PushAndPopWorkWithStdQueueAndLock)
{
    auto queue = std::queue<int>{};
    auto mutex = std::mutex{};

    pnm::utils::queue::push(queue, 10, mutex);
    pnm::utils::queue::push(queue, 20, mutex);

    const auto first = pnm::utils::queue::pop(queue, mutex);
    const auto second = pnm::utils::queue::pop(queue, mutex);
    const auto empty = pnm::utils::queue::pop(queue, mutex);

    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, 10);

    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, 20);

    EXPECT_FALSE(empty.has_value());
}

TEST(CommonBitTests, SetResetAndCheckSingleBits)
{
    std::uint8_t value{ 0 };

    pnm::utils::bit::set<1>(value);
    pnm::utils::bit::set<5>(value);
    pnm::utils::bit::toggle<7>(value);

    EXPECT_TRUE(pnm::utils::bit::check<1>(value));
    EXPECT_TRUE(pnm::utils::bit::check<5>(value));
    EXPECT_TRUE(pnm::utils::bit::check<7>(value));

    pnm::utils::bit::reset<1>(value);
    pnm::utils::bit::toggle<7>(value);

    EXPECT_FALSE(pnm::utils::bit::check<1>(value));
    EXPECT_TRUE(pnm::utils::bit::check<5>(value));
    EXPECT_FALSE(pnm::utils::bit::check<7>(value));
}

TEST(CommonBitTests, MaskedHelpersOperateOnBitRanges)
{
    std::uint8_t value{ 0 };

    pnm::utils::bit::set_masked<2, 3>(value, static_cast<std::uint8_t>(0b101));

    constexpr auto mask = pnm::utils::bit::mask<2, 3, std::uint8_t>();
    constexpr auto legacy_mask = pnm::utils::bit::create_mask<2, 3, std::uint8_t>();
    const auto field = pnm::utils::bit::get_masked<2, 3>(value);
    EXPECT_EQ(mask, static_cast<std::uint8_t>(0b00011100));
    EXPECT_EQ(legacy_mask, mask);
    EXPECT_EQ(field, static_cast<std::uint8_t>(0b101));

    const auto bits = pnm::utils::bit::bits(value);
    EXPECT_FALSE(bits[0]);
    EXPECT_FALSE(bits[1]);
    EXPECT_TRUE(bits[2]);
    EXPECT_FALSE(bits[3]);
    EXPECT_TRUE(bits[4]);
}

TEST(CommonBitTests, CheckedMaskedSetRejectsOversizedValues)
{
    std::uint8_t value{ 0b11110000 };

    EXPECT_FALSE((pnm::utils::bit::set_masked_checked<2, 3>(value, static_cast<std::uint8_t>(0b1000))));
    EXPECT_EQ(value, static_cast<std::uint8_t>(0b11110000));

    EXPECT_TRUE((pnm::utils::bit::set_masked_checked<2, 3>(value, static_cast<std::uint8_t>(0b011))));
    EXPECT_EQ(value, static_cast<std::uint8_t>(0b11101100));
}

TEST(CommonBitTests, BitsetViewPreservesBitPositions)
{
    const auto view = pnm::utils::bit::to_bitset(std::uint8_t{ 0b10100001 });

    static_assert(std::same_as<decltype(view), const std::bitset<pnm::utils::bit::size<std::uint8_t>()>>);
    EXPECT_TRUE(view.test(0));
    EXPECT_FALSE(view.test(1));
    EXPECT_TRUE(view.test(5));
    EXPECT_TRUE(view.test(7));
}
