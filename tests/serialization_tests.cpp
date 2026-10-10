#include "pneumo/common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace serialization_tests
{
    // A one-byte encoding deliberately differs from the object's representation.
    struct EncodedValue
    {
        std::uint32_t value;
    };

    struct Payload
    {
        std::vector<std::byte> bytes;
    };

    struct PointerValue
    {
        std::uint32_t* value;
    };

    struct ReferenceEnvelope
    {
        Payload& payload;
    };

    struct ReferenceBase : ReferenceEnvelope
    {
    };

    struct IncompleteAdapter : Payload
    {
    };

    struct InvalidAdapter : Payload
    {
    };

    struct AutoMessage
    {
        double value;
        Payload payload;
        EncodedValue code;
    };

    struct NestedAutoMessage
    {
        AutoMessage message;
        std::array<Payload, 2> additional;
        Payload values[2];
    };

    struct NonTrivialValue
    {
        double value{};
        ~NonTrivialValue() {} // Deliberately non-trivial, despite having a plain value member.
    };

    struct TrivialEnvelope
    {
        std::uint8_t tag;
        EncodedValue code;
    };

    class PrivateAutoMessage : private Payload
    {
      public:
        PrivateAutoMessage() = default;
        explicit PrivateAutoMessage(Payload payload, double value)
          : Payload{ std::move(payload) }
          , m_value{ value }
        {
        }
        auto payload() const -> const Payload& { return *this; }
        auto value() const -> double { return m_value; }

      private:
        double m_value{};
    };

    struct BitFieldAutoMessage
    {
        unsigned enabled : 1;
        unsigned : 3;
        unsigned count : 4;
        Payload payload;
    };

    struct UnsafeAutoMessage
    {
        Payload payload;
        int* pointer;
    };

    struct ReferenceAutoMessage
    {
        Payload payload;
        Payload& reference;
    };

    struct ConstAutoMessage
    {
        Payload payload;
        const double value;
    };

    struct ExplicitAutoMessage : AutoMessage
    {
    };

    struct OversizedPayload
    {
    };
}

namespace pnm::utils::memory
{
    template<>
    struct SerializationAdapter<serialization_tests::EncodedValue>
    {
        static auto bufferSize(const serialization_tests::EncodedValue&) -> std::size_t { return 1; }

        static auto serialize(const serialization_tests::EncodedValue& src, std::span<std::byte> dest) -> void
        {
            if (src.value > 255U) {
                throw std::out_of_range{ "Value does not fit in one byte" };
            }
            dest.front() = static_cast<std::byte>(src.value);
        }

        static auto deserialize(std::span<const std::byte> src, serialization_tests::EncodedValue& dest)
          -> void
        {
            if (src.size() != 1) {
                throw std::invalid_argument{ "Expected one byte" };
            }
            dest.value = std::to_integer<std::uint32_t>(src.front());
        }
    };

    template<>
    struct SerializationAdapter<serialization_tests::Payload>
    {
        static auto bufferSize(const serialization_tests::Payload& src) -> std::size_t
        {
            return src.bytes.size();
        }

        static auto serialize(const serialization_tests::Payload& src, std::span<std::byte> dest) -> void
        {
            std::ranges::copy(src.bytes, dest.begin());
        }

        static auto deserialize(std::span<const std::byte> src, serialization_tests::Payload& dest) -> void
        {
            dest.bytes.assign(src.begin(), src.end());
        }
    };

    template<>
    struct SerializationAdapter<serialization_tests::PointerValue>
    {
        static auto bufferSize(const serialization_tests::PointerValue&) -> std::size_t { return 1; }

        static auto serialize(const serialization_tests::PointerValue& src, std::span<std::byte> dest) -> void
        {
            SerializationAdapter<serialization_tests::EncodedValue>::serialize({ *src.value }, dest);
        }

        static auto deserialize(std::span<const std::byte> src, serialization_tests::PointerValue& dest)
          -> void
        {
            serialization_tests::EncodedValue decoded{};
            SerializationAdapter<serialization_tests::EncodedValue>::deserialize(src, decoded);
            *dest.value = decoded.value;
        }
    };

    template<>
    struct SerializationAdapter<serialization_tests::IncompleteAdapter>
    {
        static auto serialize(const serialization_tests::IncompleteAdapter&, std::span<std::byte>) -> void;
        static auto deserialize(std::span<const std::byte>, serialization_tests::IncompleteAdapter&) -> void;
        // Missing bufferSize must prevent this from being treated as an adapter.
    };

    template<>
    struct SerializationAdapter<serialization_tests::InvalidAdapter>
    {
        static auto bufferSize(const serialization_tests::InvalidAdapter&) -> std::size_t;
        static auto serialize(const serialization_tests::InvalidAdapter&, std::span<std::byte>) -> bool;
        static auto deserialize(std::span<const std::byte>, serialization_tests::InvalidAdapter&) -> void;
    };

    template<>
    struct SerializationAdapter<serialization_tests::ExplicitAutoMessage>
    {
        static auto bufferSize(const serialization_tests::ExplicitAutoMessage&) -> std::size_t { return 1; }
        static auto serialize(const serialization_tests::ExplicitAutoMessage& src, std::span<std::byte> dest)
          -> void
        {
            SerializationAdapter<serialization_tests::EncodedValue>::serialize(src.code, dest);
        }
        static auto deserialize(std::span<const std::byte> src,
                                serialization_tests::ExplicitAutoMessage& dest) -> void
        {
            SerializationAdapter<serialization_tests::EncodedValue>::deserialize(src, dest.code);
        }
    };

    template<>
    struct SerializationAdapter<serialization_tests::OversizedPayload>
    {
        static auto bufferSize(const serialization_tests::OversizedPayload&) -> std::size_t
        {
            return std::numeric_limits<std::size_t>::max();
        }
        static auto serialize(const serialization_tests::OversizedPayload&, std::span<std::byte>) -> void
        {
            ADD_FAILURE() << "Size overflow must be rejected before calling the adapter";
        }
        static auto deserialize(std::span<const std::byte>, serialization_tests::OversizedPayload&) -> void {}
    };
}

namespace
{
    namespace memory = pnm::utils::memory;
    using namespace serialization_tests;

    template<typename Destination, typename Source>
    concept CanCopy =
      requires(Destination& destination, const Source& source) { memory::copy(destination, source); };

    static_assert(memory::Serializable<Payload>);
    static_assert(memory::Serializable<const Payload>);
    static_assert(memory::Serializable<EncodedValue>);
    static_assert(!memory::Serializable<IncompleteAdapter>);
    static_assert(!memory::Serializable<InvalidAdapter>);
    static_assert(std::is_trivially_copyable_v<ReferenceEnvelope>);
    static_assert(CanCopy<EncodedValue, Payload>);
    static_assert(!CanCopy<std::uint32_t, std::vector<int>>);
    static_assert(!CanCopy<std::vector<int>, std::uint32_t>);
    static_assert(!std::is_trivially_copyable_v<AutoMessage>);
    static_assert(memory::detail::HasSerializationAdapter<AutoMessage>);
    static_assert(memory::detail::HasSerializationAdapter<NonTrivialValue>);
    static_assert(memory::detail::HasSerializationAdapter<NestedAutoMessage>);
    static_assert(memory::Serializable<AutoMessage>);
    static_assert(memory::Serializable<const AutoMessage>);
    static_assert(memory::Serializable<PrivateAutoMessage>);
    static_assert(!memory::detail::HasSerializationAdapter<TrivialEnvelope>);
    static_assert(!memory::Serializable<UnsafeAutoMessage>);
    static_assert(!memory::Serializable<ReferenceAutoMessage>);
    static_assert(!memory::Serializable<ConstAutoMessage>);
}

TEST(CommonSerializationTests, RawValueRoundTripsThroughOwnedBuffer)
{
    constexpr std::uint32_t source{ 0x1234ABCDU };
    const auto bytes = memory::serialize(source);
    const auto expected = std::bit_cast<std::array<std::byte, sizeof(source)>>(source);

    EXPECT_EQ(bytes.size(), sizeof(source));
    EXPECT_TRUE(std::ranges::equal(bytes, expected));

    std::uint32_t restored{};
    ASSERT_TRUE(memory::deserialize(bytes, restored));
    EXPECT_EQ(restored, source);
}

TEST(CommonSerializationTests, RawSerializationSupportsExactAndLargerBuffers)
{
    constexpr std::uint32_t source{ 0x1234ABCDU };
    const auto expected = std::bit_cast<std::array<std::byte, sizeof(source)>>(source);
    std::array<std::byte, sizeof(source) + 2> bytes;
    bytes.fill(std::byte{ 0xAA });

    for (const auto size : { sizeof(source), bytes.size() }) {
        ASSERT_TRUE(memory::serialize(source, std::span{ bytes }.first(size)));
        EXPECT_TRUE(std::ranges::equal(std::span{ bytes }.first(sizeof(source)), expected));
        EXPECT_EQ(bytes[sizeof(source)], std::byte{ 0xAA });
        EXPECT_EQ(bytes.back(), std::byte{ 0xAA });
    }
}

TEST(CommonSerializationTests, RawSerializationRejectsInsufficientBuffersWithoutWriting)
{
    constexpr std::uint32_t source{ 0x1234ABCDU };
    std::array<std::byte, sizeof(source)> bytes;
    bytes.fill(std::byte{ 0xAA });
    const auto original = bytes;

    EXPECT_FALSE(memory::serialize(source, {}));
    EXPECT_FALSE(memory::serialize(source, std::span{ bytes }.first(sizeof(source) - 1)));
    EXPECT_EQ(bytes, original);
}

TEST(CommonSerializationTests, RawDeserializationRejectsIncorrectSizesWithoutWriting)
{
    const std::array<std::byte, sizeof(std::uint32_t) + 1> bytes{};
    constexpr std::uint32_t original{ 0x1234ABCDU };

    for (const auto size : { 0UZ, sizeof(original) - 1, bytes.size() }) {
        SCOPED_TRACE(size);
        auto destination = original;
        EXPECT_FALSE(memory::deserialize(std::span{ bytes }.first(size), destination));
        EXPECT_EQ(destination, original);
    }
}

TEST(CommonSerializationTests, RawArraysRoundTrip)
{
    constexpr std::uint16_t source[]{ 1, 20, 300 };
    std::uint16_t destination[3]{};

    const auto bytes = memory::serialize(source);
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_TRUE(std::ranges::equal(destination, source));
}

TEST(CommonSerializationTests, CopyRejectsSmallerSourceWithoutWriting)
{
    constexpr std::uint16_t source{ 0x1234U };
    constexpr std::uint32_t original{ 0xAABBCCDDU };
    auto destination = original;

    EXPECT_FALSE(memory::copy(destination, source));
    EXPECT_EQ(destination, original);
}

TEST(CommonSerializationTests, AdapterOverridesRawRepresentation)
{
    const EncodedValue source{ 42 };
    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes, (std::vector{ std::byte{ 42 } }));

    EncodedValue destination{ 99 };
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.value, source.value);
}

TEST(CommonSerializationTests, AdapterSerializationSupportsExactAndLargerBuffers)
{
    const EncodedValue source{ 42 };
    std::array<std::byte, 3> bytes{ std::byte{ 0xAA }, std::byte{ 0xAA }, std::byte{ 0xAA } };

    for (const auto size : { 1UZ, bytes.size() }) {
        ASSERT_TRUE(memory::serialize(source, std::span{ bytes }.first(size)));
        EXPECT_EQ(bytes, (std::array{ std::byte{ 42 }, std::byte{ 0xAA }, std::byte{ 0xAA } }));
    }
}

TEST(CommonSerializationTests, AdapterSerializationRejectsInsufficientBufferWithoutWriting)
{
    const Payload source{ { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } } };
    std::array<std::byte, 2> bytes{ std::byte{ 0xAA }, std::byte{ 0xAA } };
    const auto original = bytes;

    EXPECT_FALSE(memory::serialize(source, bytes));
    EXPECT_FALSE(memory::serialize(source, std::span{ bytes }.first(0)));
    EXPECT_FALSE(memory::serialize(EncodedValue{ 42 }, {}));
    EXPECT_EQ(bytes, original);
}

TEST(CommonSerializationTests, AdapterSerializationPropagatesErrors)
{
    const EncodedValue source{ 256 };
    std::array<std::byte, 1> bytes{ std::byte{ 0xAA } };

    EXPECT_THROW(memory::serialize(source), std::out_of_range);
    EXPECT_THROW(memory::serialize(source, bytes), std::out_of_range);
    EXPECT_EQ(bytes.front(), std::byte{ 0xAA });
}

TEST(CommonSerializationTests, AdapterDeserializationPropagatesValidationErrors)
{
    EncodedValue destination{ 42 };
    const std::array<std::byte, 2> oversized{};

    EXPECT_THROW(memory::deserialize({}, destination), std::invalid_argument);
    EXPECT_THROW(memory::deserialize(oversized, destination), std::invalid_argument);
    EXPECT_EQ(destination.value, 42U);
}

TEST(CommonSerializationTests, VariableSizeAdapterRestoresIntoEmptyDestination)
{
    const Payload source{ { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } } };
    Payload destination;

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes, source.bytes);
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.bytes, source.bytes);
}

TEST(CommonSerializationTests, VariableSizeAdapterReplacesLargerDestination)
{
    const Payload source{ { std::byte{ 42 } } };
    Payload destination{ { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } } };

    ASSERT_TRUE(memory::deserialize(memory::serialize(source), destination));
    EXPECT_EQ(destination.bytes, source.bytes);
}

TEST(CommonSerializationTests, VariableSizeAdapterSupportsEmptyPayload)
{
    const Payload source;
    Payload destination{ { std::byte{ 42 } } };

    const auto bytes = memory::serialize(source);
    EXPECT_TRUE(bytes.empty());
    EXPECT_TRUE(memory::serialize(source, {}));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_TRUE(destination.bytes.empty());
}

TEST(CommonSerializationTests, VariableSizeAdapterCopyRestoresIntoEmptyDestination)
{
    const Payload source{ { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } } };
    Payload destination;

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.bytes, source.bytes);
}

TEST(CommonSerializationTests, AdapterCopyUsesEncodedRepresentation)
{
    const EncodedValue source{ 42 };
    std::array<std::byte, 1> bytes{};
    EncodedValue destination{};

    ASSERT_TRUE(memory::copy(bytes, source));
    EXPECT_EQ(bytes.front(), std::byte{ 42 });
    ASSERT_TRUE(memory::copy(destination, bytes));
    EXPECT_EQ(destination.value, source.value);
}

TEST(CommonSerializationTests, AdapterAcceptsPointerContainingObject)
{
    std::uint32_t source_value{ 42 };
    std::uint32_t destination_value{};
    const PointerValue source{ &source_value };
    PointerValue destination{ &destination_value };

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes, (std::vector{ std::byte{ 42 } }));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.value, &destination_value);
    EXPECT_EQ(destination_value, source_value);
}

TEST(CommonSerializationTests, MemberAdapterDoesNotEnableRawCopyOfReferences)
{
    EXPECT_FALSE(memory::Serializable<ReferenceEnvelope>);
    EXPECT_FALSE(memory::Serializable<ReferenceBase>);
    EXPECT_FALSE(memory::Serializable<ReferenceEnvelope[2]>);
    EXPECT_FALSE((memory::Serializable<std::array<ReferenceEnvelope, 2>>));
}

TEST(CommonSerializationTests, GeneratedAdapterSerializesMixedMembers)
{
    const AutoMessage source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } } }, { 42 } };
    AutoMessage destination{};
    using Adapter = memory::SerializationAdapter<AutoMessage>;

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes.size(), sizeof(double) + 2 * sizeof(std::size_t) + source.payload.bytes.size() + 1);
    EXPECT_EQ(bytes.size(), Adapter::bufferSize(source));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.value, source.value);
    EXPECT_EQ(destination.payload.bytes, source.payload.bytes);
    EXPECT_EQ(destination.code.value, source.code.value);
}

TEST(CommonSerializationTests, GeneratedAdapterSupportsNestedAdaptersAndArrays)
{
    const NestedAutoMessage source{ { 7.25, { { std::byte{ 9 } } }, { 42 } },
                                    { Payload{}, Payload{ { std::byte{ 1 }, std::byte{ 2 } } } },
                                    { Payload{ { std::byte{ 3 } } }, Payload{} } };
    NestedAutoMessage destination{};

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.message.value, source.message.value);
    EXPECT_EQ(destination.message.payload.bytes, source.message.payload.bytes);
    EXPECT_EQ(destination.message.code.value, source.message.code.value);
    for (std::size_t i{}; i < 2; ++i) {
        EXPECT_EQ(destination.additional[i].bytes, source.additional[i].bytes);
        EXPECT_EQ(destination.values[i].bytes, source.values[i].bytes);
    }
}

TEST(CommonSerializationTests, GeneratedAdapterWorksDirectlyForNonTrivialClassWithPlainMember)
{
    const NonTrivialValue source{ 12.5 };
    NonTrivialValue destination;
    using Adapter = memory::SerializationAdapter<NonTrivialValue>;
    std::array<std::byte, sizeof(double)> bytes{};

    EXPECT_EQ(Adapter::bufferSize(source), sizeof(double));
    Adapter::serialize(source, bytes);
    Adapter::deserialize(bytes, destination);
    EXPECT_EQ(destination.value, source.value);
}

TEST(CommonSerializationTests, GeneratedAdapterSupportsEmptyNonTrivialClass)
{
    struct Empty
    {
        ~Empty() {}
    };
    Empty source;
    Empty destination;

    static_assert(memory::detail::HasSerializationAdapter<Empty>);
    EXPECT_TRUE(memory::serialize(source).empty());
    EXPECT_TRUE(memory::serialize(source, {}));
    EXPECT_TRUE(memory::deserialize({}, destination));
}

TEST(CommonSerializationTests, TriviallyCopyableClassKeepsRawRepresentation)
{
    const TrivialEnvelope source{ 7, { 42 } };
    TrivialEnvelope destination{};

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes.size(), sizeof(source));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.tag, source.tag);
    EXPECT_EQ(destination.code.value, source.code.value);
}

TEST(CommonSerializationTests, ExplicitAdapterOverridesGeneratedFallback)
{
    const ExplicitAutoMessage source{ { 21.5, { { std::byte{ 1 }, std::byte{ 2 } } }, { 42 } } };
    ExplicitAutoMessage destination{};

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes, (std::vector{ std::byte{ 42 } }));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.code.value, source.code.value);
    EXPECT_EQ(destination.value, 0.0);
    EXPECT_TRUE(destination.payload.bytes.empty());
}

TEST(CommonSerializationTests, GeneratedAdapterHandlesPrivateBaseAndMembers)
{
    const PrivateAutoMessage source{ { { std::byte{ 42 } } }, 21.5 };
    PrivateAutoMessage destination;

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.payload().bytes, source.payload().bytes);
    EXPECT_EQ(destination.value(), source.value());
}

TEST(CommonSerializationTests, GeneratedAdapterHandlesBitFields)
{
    const BitFieldAutoMessage source{ 1, 13, { { std::byte{ 42 } } } };
    BitFieldAutoMessage destination{};

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.enabled, source.enabled);
    EXPECT_EQ(destination.count, source.count);
    EXPECT_EQ(destination.payload.bytes, source.payload.bytes);
}

TEST(CommonSerializationTests, GeneratedAdapterChecksOutputCapacity)
{
    const AutoMessage source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 } } }, { 42 } };
    const auto expected = memory::serialize(source);
    std::vector<std::byte> bytes(expected.size() + 2, std::byte{ 0xAA });
    const auto original = bytes;
    using Adapter = memory::SerializationAdapter<AutoMessage>;
    auto too_small = std::span{ bytes }.first(expected.size() - 1);

    EXPECT_FALSE(memory::serialize(source, too_small));
    EXPECT_THROW(Adapter::serialize(source, too_small), std::length_error);
    EXPECT_EQ(bytes, original);
    ASSERT_TRUE(memory::serialize(source, bytes));
    EXPECT_TRUE(std::ranges::equal(std::span{ bytes }.first(expected.size()), expected));
    EXPECT_EQ(bytes[expected.size()], std::byte{ 0xAA });
    EXPECT_EQ(bytes.back(), std::byte{ 0xAA });
}

TEST(CommonSerializationTests, GeneratedAdapterRejectsTruncatedAndTrailingInput)
{
    const AutoMessage source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 } } }, { 42 } };
    auto bytes = memory::serialize(source);

    for (std::size_t size{}; size < bytes.size(); ++size) {
        SCOPED_TRACE(size);
        AutoMessage destination{};
        EXPECT_THROW(memory::deserialize(std::span{ bytes }.first(size), destination), std::invalid_argument);
    }
    bytes.push_back(std::byte{});
    AutoMessage destination{};
    EXPECT_THROW(memory::deserialize(bytes, destination), std::invalid_argument);
}

TEST(CommonSerializationTests, GeneratedAdapterRejectsOversizedMemberLength)
{
    const AutoMessage source{ 21.5, { { std::byte{ 1 } } }, { 42 } };
    auto bytes = memory::serialize(source);
    const auto invalid_size = std::numeric_limits<std::size_t>::max();
    ASSERT_TRUE(
      memory::serialize(invalid_size, std::span{ bytes }.subspan(sizeof(double), sizeof(invalid_size))));
    AutoMessage destination{};

    EXPECT_THROW(memory::deserialize(bytes, destination), std::invalid_argument);
    EXPECT_TRUE(destination.payload.bytes.empty());
}

TEST(CommonSerializationTests, GeneratedAdapterRejectsSizeOverflow)
{
    struct Message
    {
        NonTrivialValue value;
        OversizedPayload payload;
    };
    const Message source{};
    std::array<std::byte, 1> bytes{ std::byte{ 0xAA } };

    EXPECT_THROW(memory::serialize(source), std::length_error);
    EXPECT_THROW(memory::serialize(source, bytes), std::length_error);
    EXPECT_EQ(bytes.front(), std::byte{ 0xAA });
}
