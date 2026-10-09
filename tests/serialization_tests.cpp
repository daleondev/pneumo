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

    struct IncompleteAdapter
    {
        std::vector<std::byte> bytes;
    };

    struct InvalidAdapter
    {
        std::vector<std::byte> bytes;
    };

    struct MixedMessage
    {
        double value;
        Payload payload;
        std::uint32_t sequence;
    };

    struct AdaptedBaseMessage : Payload
    {
        double value;
    };

    struct OversizedPayload
    {
    };

    struct NestedMessage
    {
        MixedMessage message;
        std::array<Payload, 2> additional;
        EncodedValue code;
    };

    class PrivateMessage : private MixedMessage
    {
      public:
        PrivateMessage() = default;
        explicit PrivateMessage(MixedMessage message, EncodedValue code)
          : MixedMessage{ std::move(message) }
          , m_code{ code }
        {
        }
        auto message() const -> const MixedMessage& { return *this; }
        auto code() const -> std::uint32_t { return m_code.value; }

      private:
        EncodedValue m_code{};
    };

    struct PaddedMessage
    {
        std::uint8_t tag;
        double value;
    };

    struct BitFieldMessage
    {
        unsigned enabled : 1;
        unsigned : 3;
        unsigned count : 4;
        Payload payload;
    };

    struct ConstMember
    {
        const double value;
    };

    struct UnsafeMixedMessage
    {
        Payload payload;
        int* pointer;
    };

    union AdaptedUnion {
        EncodedValue code;
        std::uint32_t raw;
    };
}

namespace pnm::utils::memory
{
    template<>
    struct SerializationAdapter<serialization_tests::OversizedPayload>
    {
        static auto bufferSize(const serialization_tests::OversizedPayload&) -> std::size_t
        {
            return std::numeric_limits<std::size_t>::max();
        }
        static auto serialize(const serialization_tests::OversizedPayload&, std::span<std::byte>) -> void
        {
            ADD_FAILURE() << "Overflow must be rejected before calling the adapter";
        }
        static auto deserialize(std::span<const std::byte>, serialization_tests::OversizedPayload&) -> void {}
    };

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
}

namespace
{
    namespace memory = pnm::utils::memory;
    using namespace serialization_tests;

    template<typename Destination, typename Source>
    concept CanCopy =
      requires(Destination& destination, const Source& source) { memory::copy(destination, source); };

    template<typename T>
    concept CanDeserialize =
      requires(T& destination, std::span<const std::byte> bytes) { memory::deserialize(bytes, destination); };

    static_assert(memory::Serializable<Payload>);
    static_assert(memory::Serializable<const Payload>);
    static_assert(memory::Serializable<EncodedValue>);
    static_assert(!memory::Serializable<IncompleteAdapter>);
    static_assert(!memory::Serializable<InvalidAdapter>);
    static_assert(std::is_trivially_copyable_v<ReferenceEnvelope>);
    static_assert(CanCopy<EncodedValue, Payload>);
    static_assert(!CanCopy<std::uint32_t, std::vector<int>>);
    static_assert(!CanCopy<std::vector<int>, std::uint32_t>);
    static_assert(!CanCopy<const Payload, Payload>);
    static_assert(!CanDeserialize<const Payload>);
    static_assert(CanDeserialize<Payload>);
    static_assert(memory::Serializable<MixedMessage>);
    static_assert(memory::Serializable<const MixedMessage>);
    static_assert(memory::Serializable<NestedMessage>);
    static_assert(memory::Serializable<PrivateMessage>);
    static_assert(memory::Serializable<BitFieldMessage>);
    static_assert(!memory::Serializable<ConstMember>);
    static_assert(!memory::Serializable<UnsafeMixedMessage>);
    static_assert(!memory::Serializable<AdaptedUnion>);
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

TEST(CommonSerializationTests, MixedMembersRoundTripIndividually)
{
    const MixedMessage source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } } }, 42 };
    MixedMessage destination{};

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes.size(),
              sizeof(double) + sizeof(std::size_t) + source.payload.bytes.size() + sizeof(std::uint32_t));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.value, source.value);
    EXPECT_EQ(destination.payload.bytes, source.payload.bytes);
    EXPECT_EQ(destination.sequence, source.sequence);
}

TEST(CommonSerializationTests, NestedMembersAndVariableSizeArraysRoundTrip)
{
    const NestedMessage source{ { 7.25, { { std::byte{ 9 } } }, 12 },
                                { Payload{}, Payload{ { std::byte{ 1 }, std::byte{ 2 } } } },
                                { 42 } };
    NestedMessage destination{};

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.message.value, source.message.value);
    EXPECT_EQ(destination.message.payload.bytes, source.message.payload.bytes);
    EXPECT_EQ(destination.message.sequence, source.message.sequence);
    EXPECT_TRUE(destination.additional[0].bytes.empty());
    EXPECT_EQ(destination.additional[1].bytes, source.additional[1].bytes);
    EXPECT_EQ(destination.code.value, 42U);
}

TEST(CommonSerializationTests, ArraysOfAdapterBackedObjectsRoundTrip)
{
    const Payload source[]{ { { std::byte{ 1 } } }, {}, { { std::byte{ 2 }, std::byte{ 3 } } } };
    Payload destination[3];

    ASSERT_TRUE(memory::deserialize(memory::serialize(source), destination));
    for (std::size_t i{}; i < 3; ++i) {
        EXPECT_EQ(destination[i].bytes, source[i].bytes);
    }
}

TEST(CommonSerializationTests, NestedAdapterOverridesTrivialObjectRepresentation)
{
    struct Message
    {
        double value;
        EncodedValue code;
    };
    const Message source{ 2.5, { 42 } };
    Message destination{};

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes.size(), sizeof(double) + sizeof(std::size_t) + 1);
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.value, source.value);
    EXPECT_EQ(destination.code.value, 42U);
}

TEST(CommonSerializationTests, PrivateBaseAndPrivateMembersRoundTrip)
{
    const PrivateMessage source{ { 12.5, { { std::byte{ 42 } } }, 17 }, { 9 } };
    PrivateMessage destination;

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.message().value, source.message().value);
    EXPECT_EQ(destination.message().payload.bytes, source.message().payload.bytes);
    EXPECT_EQ(destination.message().sequence, source.message().sequence);
    EXPECT_EQ(destination.code(), source.code());
}

TEST(CommonSerializationTests, BaseClassAdapterIsApplied)
{
    const AdaptedBaseMessage source{ { { std::byte{ 1 }, std::byte{ 2 } } }, 12.5 };
    AdaptedBaseMessage destination{};

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes.size(), sizeof(std::size_t) + source.bytes.size() + sizeof(double));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.bytes, source.bytes);
    EXPECT_EQ(destination.value, source.value);
}

TEST(CommonSerializationTests, NestedAdapterSizeOverflowIsRejectedBeforeWriting)
{
    struct Message
    {
        double value;
        OversizedPayload payload;
    };
    const Message source{};
    std::array<std::byte, 1> bytes{ std::byte{ 0xAA } };

    EXPECT_THROW(memory::serialize(source), std::length_error);
    EXPECT_THROW(memory::serialize(source, bytes), std::length_error);
    EXPECT_EQ(bytes.front(), std::byte{ 0xAA });
}

TEST(CommonSerializationTests, ClassPaddingIsNotSerialized)
{
    const PaddedMessage source{ 7, 21.5 };
    PaddedMessage destination{};

    const auto bytes = memory::serialize(source);
    EXPECT_EQ(bytes.size(), sizeof(source.tag) + sizeof(source.value));
    ASSERT_TRUE(memory::deserialize(bytes, destination));
    EXPECT_EQ(destination.tag, source.tag);
    EXPECT_EQ(destination.value, source.value);
}

TEST(CommonSerializationTests, BitFieldsAndAdaptedMemberRoundTrip)
{
    const BitFieldMessage source{ 1, 13, { { std::byte{ 42 } } } };
    BitFieldMessage destination{};

    ASSERT_TRUE(memory::copy(destination, source));
    EXPECT_EQ(destination.enabled, source.enabled);
    EXPECT_EQ(destination.count, source.count);
    EXPECT_EQ(destination.payload.bytes, source.payload.bytes);
}

TEST(CommonSerializationTests, EmptyObjectsAndArraysHaveEmptyEncoding)
{
    struct Empty
    {
    };
    Empty object;
    std::array<Payload, 0> array;

    EXPECT_TRUE(memory::serialize(object).empty());
    EXPECT_TRUE(memory::serialize(array).empty());
    EXPECT_TRUE(memory::serialize(object, {}));
    EXPECT_TRUE(memory::deserialize({}, object));
    EXPECT_TRUE(memory::deserialize({}, array));
}

TEST(CommonSerializationTests, MixedSerializationChecksCapacityBeforeWriting)
{
    const MixedMessage source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 } } }, 42 };
    const auto expected = memory::serialize(source);
    std::vector<std::byte> bytes(expected.size() + 2, std::byte{ 0xAA });
    const auto original = bytes;

    EXPECT_FALSE(memory::serialize(source, std::span{ bytes }.first(expected.size() - 1)));
    EXPECT_EQ(bytes, original);
    ASSERT_TRUE(memory::serialize(source, bytes));
    EXPECT_TRUE(std::ranges::equal(std::span{ bytes }.first(expected.size()), expected));
    EXPECT_EQ(bytes[expected.size()], std::byte{ 0xAA });
    EXPECT_EQ(bytes.back(), std::byte{ 0xAA });
}

TEST(CommonSerializationTests, MixedDeserializationRejectsTruncatedAndTrailingData)
{
    const MixedMessage source{ 21.5, { { std::byte{ 1 }, std::byte{ 2 } } }, 42 };
    auto bytes = memory::serialize(source);

    for (std::size_t size{}; size < bytes.size(); ++size) {
        SCOPED_TRACE(size);
        MixedMessage destination{};
        EXPECT_FALSE(memory::deserialize(std::span{ bytes }.first(size), destination));
    }
    bytes.push_back(std::byte{});
    MixedMessage destination{};
    EXPECT_FALSE(memory::deserialize(bytes, destination));
}

TEST(CommonSerializationTests, MixedDeserializationRejectsOversizedMemberLength)
{
    const MixedMessage source{ 21.5, { { std::byte{ 1 } } }, 42 };
    auto bytes = memory::serialize(source);
    const auto invalid_size = std::numeric_limits<std::size_t>::max();
    ASSERT_TRUE(
      memory::serialize(invalid_size, std::span{ bytes }.subspan(sizeof(double), sizeof(invalid_size))));
    MixedMessage destination{};

    EXPECT_FALSE(memory::deserialize(bytes, destination));
    EXPECT_TRUE(destination.payload.bytes.empty());
}
