#include "pneumo/meta.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ratio>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <variant>
#include <vector>

enum class TestState : std::uint8_t
{
    Idle = 1,
    Running = 3,
    Stopped = 7
};

enum class ExtendedState : std::uint16_t
{
    Large = 1024,
    VeryLarge = 2048
};

struct FieldProbe
{
    int count;
    double ratio;
};

template<typename T, typename Ratio, bool IsBase = false>
struct NestedTemplateAliasProbe
{
};

template<typename T>
using NestedBaseAliasProbe = NestedTemplateAliasProbe<T, std::ratio<1>, true>;

struct NestedTypesProbe
{
    using Plain = int;
    using TemplateAlias = NestedTemplateAliasProbe<double, std::ratio<2>>;
    using BaseAlias = NestedBaseAliasProbe<float>;

  private:
    using Hidden = NestedTemplateAliasProbe<int, std::ratio<3>>;
};

struct DispatchProbe
{
    static constexpr auto triple(int value) -> int { return value * 3; }
};

class MethodProbe
{
  public:
    constexpr MethodProbe(int id, double ratio)
      : m_id(id)
      , m_ratio(ratio)
    {
    }

    constexpr auto id() const -> int { return m_id; }
    constexpr auto ratio() const -> double { return m_ratio; }
    constexpr auto nonConst() -> int { return m_id; }
    constexpr auto withArgument(int value) const -> int { return value; }
    constexpr auto returnsVoid() const -> void {}
    static constexpr auto staticValue() -> int { return 99; }
    template<typename T>
    constexpr auto templated(T value) const -> T
    {
        return value;
    }
    constexpr auto deleted() const -> int = delete;
    constexpr explicit operator bool() const { return m_id != 0; }

  private:
    constexpr auto hidden() const -> int { return m_id; }

    int m_id;
    double m_ratio;
};

namespace
{
    using TupleA = std::tuple<int, double>;
    using TupleB = std::tuple<double, float, int>;

    auto next_temp_source_path() -> std::filesystem::path
    {
        static std::atomic_size_t counter{ 0 };
        const auto suffix = counter.fetch_add(1, std::memory_order_relaxed);

        return std::filesystem::temp_directory_path() /
               ("pnm::meta-source-test-" + std::to_string(suffix) + ".cpp");
    }

    class ScopedTempSourceFile
    {
      public:
        explicit ScopedTempSourceFile(std::string_view contents)
          : m_path(next_temp_source_path())
        {
            overwrite(contents);
        }

        ScopedTempSourceFile(const ScopedTempSourceFile&) = delete;
        auto operator=(const ScopedTempSourceFile&) -> ScopedTempSourceFile& = delete;
        ScopedTempSourceFile(ScopedTempSourceFile&&) = delete;
        auto operator=(ScopedTempSourceFile&&) -> ScopedTempSourceFile& = delete;

        ~ScopedTempSourceFile()
        {
            std::error_code error;
            std::filesystem::remove(m_path, error);
        }

        auto path() const -> const std::filesystem::path& { return m_path; }

        auto string_path() const -> std::string { return m_path.string(); }

        auto overwrite(std::string_view contents) const -> void
        {
            auto stream = std::ofstream(m_path, std::ios::binary | std::ios::trunc);
            ASSERT_TRUE(stream.is_open());

            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            ASSERT_TRUE(stream.good());
        }

      private:
        std::filesystem::path m_path;
    };

    auto create_allocation_churn() -> void
    {
        auto allocations = std::vector<std::string>{};
        allocations.reserve(1024);

        for (std::size_t index = 0; index < 1024; ++index) {
            allocations.emplace_back(2048, static_cast<char>('a' + (index % 26)));
        }

        ASSERT_EQ(allocations.size(), 1024UZ);
    }
}

TEST(StringMetaTests, FixedStringStoresCompileTimeStringViews)
{
    constexpr auto value = pnm::meta::string::FixedString{ "sample" };

    static_assert(value.size() == 6UZ);
    static_assert(static_cast<std::string_view>(value) == "sample");
    SUCCEED();
}

TEST(TupleMetaTests, TupleConcept)
{
    static_assert(pnm::meta::tuple::Tuple<TupleA>);
    static_assert(!pnm::meta::tuple::Tuple<int>);
    SUCCEED();
}

TEST(TupleMetaTests, ContainsTypeConcept)
{
    static_assert(pnm::meta::tuple::ContainsType<TupleA, int>);
    static_assert(pnm::meta::tuple::ContainsType<TupleA, double>);
    static_assert(!pnm::meta::tuple::ContainsType<TupleA, float>);
    SUCCEED();
}

TEST(TupleMetaTests, ConcatTypes)
{
    using Result = pnm::meta::tuple::concat_types_t<TupleA, TupleB>;
    static_assert(std::same_as<Result, std::tuple<int, double, double, float, int>>);
    SUCCEED();
}

TEST(TupleMetaTests, UniqueTypes)
{
    using Input = std::tuple<int, double, int, float, double, int>;
    using Result = pnm::meta::tuple::unique_types_t<Input>;
    static_assert(std::same_as<Result, std::tuple<int, double, float>>);
    SUCCEED();
}

TEST(TupleMetaTests, CountHelper)
{
    static_assert(pnm::meta::tuple::count<TupleA>() == 2UZ);
    static_assert(pnm::meta::tuple::count<TupleB>() == 3UZ);
    SUCCEED();
}

TEST(TupleMetaTests, AtHelper)
{
    static_assert(std::same_as<pnm::meta::tuple::at_t<0, TupleA>, int>);
    static_assert(std::same_as<pnm::meta::tuple::at_t<1, TupleA>, double>);
    SUCCEED();
}

TEST(TupleMetaTests, ToVariant)
{
    using Result = pnm::meta::tuple::to_variant_t<TupleB>;
    static_assert(std::same_as<Result, std::variant<double, float, int>>);
    SUCCEED();
}

TEST(TupleMetaTests, RemoveCvrefTypes)
{
    using Input = std::tuple<const int&, volatile double&&, const std::string>;
    using Result = pnm::meta::tuple::remove_cvref_types_t<Input>;
    static_assert(std::same_as<Result, std::tuple<int, double, std::string>>);
    SUCCEED();
}

TEST(TupleMetaTests, ForEach)
{
    using MyTuple = std::tuple<int, double, std::string>;
    MyTuple tuple_values{ 1, 2.0, "3" };

    pnm::meta::tuple::for_each<MyTuple>([&tuple_values](auto index) {
        using Type = pnm::meta::tuple::at_t<index, MyTuple>;
        auto val{ std::get<index>(tuple_values) };

        if constexpr (index == 0) {
            static_assert(std::same_as<Type, int>);
            EXPECT_EQ(val, 1);
        }
        else if constexpr (index == 1) {
            static_assert(std::same_as<Type, double>);
            EXPECT_EQ(val, 2.0);
        }
        else if constexpr (index == 2) {
            static_assert(std::same_as<Type, std::string>);
            EXPECT_EQ(val, "3");
        }
        else {
            FAIL() << "Index out of range";
        }
    });

    pnm::meta::tuple::for_each_element([](auto& val) {
        using Type = std::remove_cvref_t<decltype(val)>;

        if constexpr (std::same_as<Type, int>) {
            EXPECT_EQ(val, 1);
            val = 10;
        }
        else if constexpr (std::same_as<Type, double>) {
            EXPECT_EQ(val, 2.0);
            val = 20.0;
        }
        else if constexpr (std::same_as<Type, std::string>) {
            EXPECT_EQ(val, "3");
            val = "30";
        }
        else {
            FAIL() << "Type not expected";
        }
    }, tuple_values);

    EXPECT_EQ(std::get<0>(tuple_values), 10);
    EXPECT_EQ(std::get<1>(tuple_values), 20.0);
    EXPECT_EQ(std::get<2>(tuple_values), "30");
}

TEST(TupleMetaTests, ForEachCanIterateReverseAndBreak)
{
    using MyTuple = std::tuple<int, double, std::string, float>;
    auto visited_indices = std::vector<size_t>{};

    pnm::meta::tuple::for_each<MyTuple, pnm::meta::Iteration::Reverse>([&visited_indices](auto index) {
        visited_indices.push_back(index);
        if constexpr (index == 1) {
            return pnm::meta::Loop::Break;
        }
        else {
            return pnm::meta::Loop::Continue;
        }
    });

    ASSERT_EQ(visited_indices.size(), 3UZ);
    EXPECT_EQ(visited_indices[0], 3UZ);
    EXPECT_EQ(visited_indices[1], 2UZ);
    EXPECT_EQ(visited_indices[2], 1UZ);
}

TEST(TupleMetaTests, ForEachElementCanIterateReverseAndBreak)
{
    auto values = std::tuple{ 1, 2, 3, 4 };
    auto visited_values = std::vector<int>{};

    pnm::meta::tuple::for_each_element<pnm::meta::Iteration::Reverse>([&visited_values](int value) {
        visited_values.push_back(value);
        return value == 2 ? pnm::meta::Loop::Break : pnm::meta::Loop::Continue;
    }, values);

    ASSERT_EQ(visited_values.size(), 3UZ);
    EXPECT_EQ(visited_values[0], 4);
    EXPECT_EQ(visited_values[1], 3);
    EXPECT_EQ(visited_values[2], 2);
}

TEST(VariantMetaTests, VariantConcept)
{
    using Input = std::variant<int, double>;
    static_assert(pnm::meta::variant::Variant<Input>);
    static_assert(!pnm::meta::variant::Variant<int>);
    SUCCEED();
}

TEST(VariantMetaTests, ToTuple)
{
    using Input = std::variant<int, double, int>;
    using Result = pnm::meta::variant::to_tuple_t<Input>;
    static_assert(std::same_as<Result, std::tuple<int, double, int>>);
    SUCCEED();
}

TEST(VariantMetaTests, UniqueTypes)
{
    using Input = std::variant<int, double, int, float, double>;
    using Result = pnm::meta::variant::unique_types_t<Input>;
    static_assert(std::same_as<Result, std::variant<int, double, float>>);
    SUCCEED();
}

TEST(VariantMetaTests, ToReferenceWrapper)
{
    using Input = std::variant<int, const double>;
    using Result = pnm::meta::variant::to_reference_wrapper_t<Input>;
    static_assert(
      std::same_as<Result, std::variant<std::reference_wrapper<int>, std::reference_wrapper<const double>>>);
    SUCCEED();
}

TEST(VariantMetaTests, ForEach)
{
    using MyVariant = std::variant<int, double, int, float>;
    pnm::meta::variant::for_each<MyVariant>([](auto index) {
        using Type = pnm::meta::variant::at_t<index, MyVariant>;
        if constexpr (index == 0) {
            static_assert(std::same_as<Type, int>);
        }
        else if constexpr (index == 1) {
            static_assert(std::same_as<Type, double>);
        }
        else if constexpr (index == 2) {
            static_assert(std::same_as<Type, int>);
        }
        else if constexpr (index == 3) {
            static_assert(std::same_as<Type, float>);
        }
        else {
            FAIL() << "Index out of range";
        }
    });
    SUCCEED();
}

TEST(VariantMetaTests, ForEachCanIterateReverseAndBreak)
{
    using MyVariant = std::variant<int, double, int, float>;
    auto visited_indices = std::vector<size_t>{};

    pnm::meta::variant::for_each<MyVariant, pnm::meta::Iteration::Reverse>([&visited_indices](auto index) {
        visited_indices.push_back(index);
        if constexpr (index == 2) {
            return pnm::meta::Loop::Break;
        }
        else {
            return pnm::meta::Loop::Continue;
        }
    });

    ASSERT_EQ(visited_indices.size(), 2UZ);
    EXPECT_EQ(visited_indices[0], 3UZ);
    EXPECT_EQ(visited_indices[1], 2UZ);
}

namespace just::a::test
{
    struct SampleType
    {
    };
}

TEST(TypeMetaTests, TypeName)
{
    static_assert(pnm::meta::type::name<just::a::test::SampleType>() == "SampleType");
    SUCCEED();
}

TEST(TypeMetaTests, PrimitiveTypeName)
{
    static_assert(pnm::meta::type::name<int>() == "int");
    static_assert(pnm::meta::type::name<double>() == "double");
    static_assert(pnm::meta::type::name<float>() == "float");
    SUCCEED();
}

TEST(TypeMetaTests, NamespaceName)
{
    static_assert(pnm::meta::type::namespace_name<just::a::test::SampleType>() == "just::a::test");
    SUCCEED();
}

TEST(TypeMetaTests, Namespaces)
{
    static_assert(pnm::meta::type::namespaces<just::a::test::SampleType>()[0] == "just");
    static_assert(pnm::meta::type::namespaces<just::a::test::SampleType>()[1] == "a");
    static_assert(pnm::meta::type::namespaces<just::a::test::SampleType>()[2] == "test");
    SUCCEED();
}

TEST(TypeMetaTests, StdTypeConcept)
{
    static_assert(pnm::meta::type::StdType<std::string>);
    static_assert(pnm::meta::type::StdType<std::vector<int>>);
    static_assert(!pnm::meta::type::StdType<just::a::test::SampleType>);
    SUCCEED();
}

TEST(EnumMetaTests, ScopedEnumConcept)
{
    static_assert(pnm::meta::enumeration::ScopedEnum<TestState>);
    static_assert(!pnm::meta::enumeration::ScopedEnum<int>);
    SUCCEED();
}

TEST(EnumMetaTests, ScopedEnumName)
{
    static_assert(pnm::meta::enumeration::name<TestState>() == "TestState");
    SUCCEED();
}

TEST(EnumMetaTests, NumEnumerators)
{
    static_assert(pnm::meta::enumeration::count<TestState>() == 3UZ);
    SUCCEED();
}

TEST(EnumMetaTests, Enumerators)
{
    constexpr auto values = pnm::meta::enumeration::enumerators<TestState>();
    static_assert(std::same_as<decltype(values), const std::array<TestState, 3>>);
    static_assert(values[0] == TestState::Idle);
    static_assert(values[1] == TestState::Running);
    static_assert(values[2] == TestState::Stopped);
    SUCCEED();
}

TEST(EnumMetaTests, EnumeratorNames)
{
    constexpr auto names = pnm::meta::enumeration::enumerator_names<TestState>();

    static_assert(std::same_as<decltype(names), const std::array<std::string_view, 3>>);
    static_assert(names[0] == "Idle");
    static_assert(names[1] == "Running");
    static_assert(names[2] == "Stopped");
    SUCCEED();
}

TEST(EnumMetaTests, UnderlyingEnumerators)
{
    constexpr auto values = pnm::meta::enumeration::underlying_enumerators<TestState>();
    static_assert(std::same_as<decltype(values), const std::array<std::uint8_t, 3>>);
    static_assert(values[0] == 1);
    static_assert(values[1] == 3);
    static_assert(values[2] == 7);
    SUCCEED();
}

TEST(EnumMetaTests, EnumValueNameParserCompilerFormats)
{
    constexpr auto parsed_name = pnm::meta::enumeration::enumerator_name<TestState::Running>();
    static_assert(parsed_name == "Running");
    SUCCEED();
}

TEST(EnumMetaTests, EnumeratorName)
{
    static_assert(pnm::meta::enumeration::enumerator_name(TestState::Running) == "Running");
    static_assert(pnm::meta::enumeration::enumerator_name(static_cast<TestState>(200)) == "");
    SUCCEED();
}

TEST(EnumMetaTests, EnumeratorsBeyondLegacyScanRange)
{
    static_assert(pnm::meta::enumeration::count<ExtendedState>() == 2UZ);
    static_assert(pnm::meta::enumeration::enumerator_name(ExtendedState::Large) == "Large");
    static_assert(pnm::meta::enumeration::enumerator_name(ExtendedState::VeryLarge) == "VeryLarge");
    static_assert(pnm::meta::enumeration::underlying_enumerators<ExtendedState>()[0] == 1024);
    static_assert(pnm::meta::enumeration::underlying_enumerators<ExtendedState>()[1] == 2048);
    SUCCEED();
}

TEST(StructMetaTests, NumFields)
{
    static_assert(pnm::meta::structural::field_count<FieldProbe>() == 2UZ);
    SUCCEED();
}

TEST(StructMetaTests, FieldNames)
{
    constexpr auto names = pnm::meta::structural::field_names<FieldProbe>();
    static_assert(std::same_as<decltype(names), const std::array<std::string_view, 2>>);
    static_assert(names[0] == "count");
    static_assert(names[1] == "ratio");
    SUCCEED();
}

TEST(StructMetaTests, FieldType)
{
    static_assert(std::same_as<pnm::meta::structural::field_type_t<0, FieldProbe>, int>);
    static_assert(std::same_as<pnm::meta::structural::field_type_t<1, FieldProbe>, double>);
    static_assert(std::same_as<pnm::meta::structural::field_types_t<FieldProbe>, std::tuple<int, double>>);
    SUCCEED();
}

TEST(StructMetaTests, NestedTypesIncludesPublicTemplateAliases)
{
    using Types = pnm::meta::structural::nested_types_t<NestedTypesProbe>;

    static_assert(std::same_as<Types,
                               std::tuple<int,
                                          NestedTemplateAliasProbe<double, std::ratio<2>>,
                                          NestedTemplateAliasProbe<float, std::ratio<1>, true>>>);
    static_assert(!pnm::meta::tuple::ContainsType<Types, NestedTemplateAliasProbe<int, std::ratio<3>>>);
    SUCCEED();
}

TEST(StructMetaTests, NestedTypeNames)
{
    static_assert(pnm::meta::structural::nested_type_count<NestedTypesProbe>() == 3UZ);

    constexpr auto names = pnm::meta::structural::nested_type_names<NestedTypesProbe>();

    static_assert(std::same_as<decltype(names), const std::array<std::string_view, 3>>);
    static_assert(pnm::meta::structural::nested_type_name<0, NestedTypesProbe>() == "Plain");
    static_assert(pnm::meta::structural::nested_type_name<1, NestedTypesProbe>() == "TemplateAlias");
    static_assert(pnm::meta::structural::nested_type_name<2, NestedTypesProbe>() == "BaseAlias");
    static_assert(names[0] == "Plain");
    static_assert(names[1] == "TemplateAlias");
    static_assert(names[2] == "BaseAlias");
    SUCCEED();
}

TEST(StructMetaTests, NestedTypeAt)
{
    static_assert(std::same_as<pnm::meta::structural::nested_type_t<0, NestedTypesProbe>, int>);
    static_assert(std::same_as<pnm::meta::structural::nested_type_t<1, NestedTypesProbe>,
                               NestedTemplateAliasProbe<double, std::ratio<2>>>);
    static_assert(std::same_as<pnm::meta::structural::nested_type_t<2, NestedTypesProbe>,
                               NestedTemplateAliasProbe<float, std::ratio<1>, true>>);
    SUCCEED();
}

TEST(StructMetaTests, InfoIncludesFieldsAndNestedTypes)
{
    using FieldInfo = pnm::meta::structural::Info<FieldProbe>;
    using NestedInfo = pnm::meta::structural::Info<NestedTypesProbe>;

    static_assert(FieldInfo::NAME == "FieldProbe");
    static_assert(std::same_as<FieldInfo::MemberTypes, std::tuple<int, double>>);
    static_assert(FieldInfo::numMembers() == 2UZ);
    static_assert(FieldInfo::MEMBER_NAMES[0] == "count");
    static_assert(FieldInfo::MEMBER_NAMES[1] == "ratio");

    static_assert(NestedInfo::numNestedTypes() == 3UZ);
    static_assert(
      std::same_as<NestedInfo::NestedTypes, pnm::meta::structural::nested_types_t<NestedTypesProbe>>);
    static_assert(NestedInfo::NESTED_TYPE_NAMES[0] == "Plain");
    static_assert(NestedInfo::NESTED_TYPE_NAMES[1] == "TemplateAlias");
    static_assert(NestedInfo::NESTED_TYPE_NAMES[2] == "BaseAlias");
    SUCCEED();
}

TEST(StructMetaTests, FieldGet)
{
    auto value = FieldProbe{ .count = 12, .ratio = 0.5 };
    EXPECT_EQ(pnm::meta::structural::get<0>(value), 12);
    EXPECT_DOUBLE_EQ(pnm::meta::structural::get<1>(value), 0.5);
}

TEST(StructMetaTests, PublicMethodNamesTypesAndTraits)
{
    static_assert(pnm::meta::structural::method_count<MethodProbe>() == 6UZ);

    constexpr auto names = pnm::meta::structural::method_names<MethodProbe>();

    static_assert(std::same_as<decltype(names), const std::array<std::string_view, 6>>);
    static_assert(pnm::meta::structural::method_name<0, MethodProbe>() == "id");
    static_assert(pnm::meta::structural::method_name<1, MethodProbe>() == "ratio");
    static_assert(pnm::meta::structural::method_name<2, MethodProbe>() == "nonConst");
    static_assert(pnm::meta::structural::method_name<3, MethodProbe>() == "withArgument");
    static_assert(pnm::meta::structural::method_name<4, MethodProbe>() == "returnsVoid");
    static_assert(pnm::meta::structural::method_name<5, MethodProbe>() == "staticValue");
    static_assert(names[0] == "id");
    static_assert(names[1] == "ratio");
    static_assert(names[2] == "nonConst");
    static_assert(names[3] == "withArgument");
    static_assert(names[4] == "returnsVoid");
    static_assert(names[5] == "staticValue");

    static_assert(std::same_as<pnm::meta::structural::method_type_t<0, MethodProbe>, int() const>);
    static_assert(std::same_as<pnm::meta::structural::method_type_t<3, MethodProbe>, int(int) const>);
    static_assert(std::same_as<pnm::meta::structural::method_type_t<5, MethodProbe>, int()>);

    static_assert(pnm::meta::structural::is_const_method<0, MethodProbe>());
    static_assert(!pnm::meta::structural::is_const_method<2, MethodProbe>());
    static_assert(!pnm::meta::structural::is_static_method<0, MethodProbe>());
    static_assert(pnm::meta::structural::is_static_method<5, MethodProbe>());
    SUCCEED();
}

TEST(StructMetaTests, PublicMethodInvoke)
{
    auto value = MethodProbe{ 7, 0.25 };
    const auto const_value = MethodProbe{ 9, 0.5 };

    EXPECT_EQ(pnm::meta::structural::invoke_method<0>(const_value), 9);
    EXPECT_DOUBLE_EQ(pnm::meta::structural::invoke_method<1>(const_value), 0.5);
    EXPECT_EQ(pnm::meta::structural::invoke_method<2>(value), 7);
    EXPECT_EQ(pnm::meta::structural::invoke_method<3>(const_value, 12), 12);
    EXPECT_EQ((pnm::meta::structural::invoke_static_method<5, MethodProbe>()), 99);
}

TEST(StructMetaTests, PublicConstGetterSubsetUsesMethods)
{
    static_assert(pnm::meta::structural::getter_count<MethodProbe>() == 2UZ);

    constexpr auto names = pnm::meta::structural::getter_names<MethodProbe>();

    static_assert(std::same_as<decltype(names), const std::array<std::string_view, 2>>);
    static_assert(pnm::meta::structural::getter_name<0, MethodProbe>() == "id");
    static_assert(pnm::meta::structural::getter_name<1, MethodProbe>() == "ratio");
    static_assert(names[0] == "id");
    static_assert(names[1] == "ratio");
    SUCCEED();
}

TEST(StructMetaTests, PublicConstGetterInvoke)
{
    const auto value = MethodProbe{ 7, 0.25 };

    EXPECT_EQ(pnm::meta::structural::invoke_getter<0>(value), 7);
    EXPECT_DOUBLE_EQ(pnm::meta::structural::invoke_getter<1>(value), 0.25);
}

TEST(StructMetaTests, InfoIncludesMethodCandidates)
{
    using Info = pnm::meta::structural::Info<MethodProbe>;

    static_assert(Info::numMethods() == 6UZ);
    static_assert(
      std::same_as<Info::MethodTypes,
                   std::tuple<int() const, double() const, int(), int(int) const, void() const, int()>>);
    static_assert(Info::METHOD_NAMES[0] == "id");
    static_assert(Info::METHOD_NAMES[1] == "ratio");
    static_assert(Info::METHOD_NAMES[2] == "nonConst");
    static_assert(Info::METHOD_NAMES[3] == "withArgument");
    static_assert(Info::METHOD_NAMES[4] == "returnsVoid");
    static_assert(Info::METHOD_NAMES[5] == "staticValue");
    SUCCEED();
}

TEST(StructMetaTests, DispatchInvokesStaticMemberByName)
{
    static_assert(pnm::meta::structural::dispatch<DispatchProbe, "triple">(7) == 21);
    EXPECT_EQ((pnm::meta::structural::dispatch<DispatchProbe, "triple">(5)), 15);
}

TEST(StaticRangeMetaTests, CreatesInclusiveRangeWithReflectedMembers)
{
    constexpr auto value = pnm::meta::structural::range<9, 11>();
    using Type = decltype(value);

    static_assert(pnm::meta::structural::field_count<Type>() == 3UZ);
    static_assert(pnm::meta::structural::field_names<Type>() ==
                  std::array<std::string_view, 3>{ "_9", "_10", "_11" });
    static_assert(std::same_as<pnm::meta::structural::field_types_t<Type>, std::tuple<int, int, int>>);
    static_assert(pnm::meta::structural::get<0>(value) == 9);
    static_assert(pnm::meta::structural::get<1>(value) == 10);
    static_assert(pnm::meta::structural::get<2>(value) == 11);
    SUCCEED();
}

TEST(StaticRangeMetaTests, SingleArgumentCreatesZeroBasedRange)
{
    constexpr auto value = pnm::meta::structural::range<3>();
    using Type = decltype(value);

    static_assert(pnm::meta::structural::field_names<Type>() ==
                  std::array<std::string_view, 3>{ "_0", "_1", "_2" });
    static_assert(pnm::meta::structural::get<0>(value) == 0);
    static_assert(pnm::meta::structural::get<1>(value) == 1);
    static_assert(pnm::meta::structural::get<2>(value) == 2);
    SUCCEED();
}

TEST(StaticRangeMetaTests, PreservesRequestedIntegralType)
{
    constexpr auto value = pnm::meta::structural::range<4, 6, std::uint16_t>();
    using Type = decltype(value);

    static_assert(std::same_as<pnm::meta::structural::field_types_t<Type>,
                               std::tuple<std::uint16_t, std::uint16_t, std::uint16_t>>);
    static_assert(
      std::same_as<std::remove_cvref_t<decltype(pnm::meta::structural::get<0>(value))>, std::uint16_t>);
    static_assert(pnm::meta::structural::get<0>(value) == 4);
    static_assert(pnm::meta::structural::get<1>(value) == 5);
    static_assert(pnm::meta::structural::get<2>(value) == 6);
    SUCCEED();
}

TEST(StaticRangeMetaTests, SupportsSingleValueInclusiveRange)
{
    constexpr auto value = pnm::meta::structural::range<7, 7>();
    using Type = decltype(value);

    static_assert(pnm::meta::structural::field_count<Type>() == 1UZ);
    static_assert(pnm::meta::structural::field_name<0, Type>() == "_7");
    static_assert(pnm::meta::structural::get<0>(value) == 7);
    SUCCEED();
}

PNM_META_SOURCE_EMBED_CURRENT

TEST(SourceMetaTests, SelfEmbedHelperRegistersSourceFile)
{
    const auto embedded = pnm::meta::source::detail::Registry::instance()[__FILE__];
    ASSERT_TRUE(embedded.has_value());
    EXPECT_EQ(std::string_view(embedded->file_name), std::string_view(__FILE__));
    EXPECT_NE(embedded->source_code.find("TEST STRING"), std::string_view::npos);
}

TEST(SourceMetaTests, LoadSourceReadsRuntimeFileIntoStableStorage)
{
    constexpr auto expected = std::string_view{ "alpha\nbeta\ngamma\n" };
    const auto temp_source = ScopedTempSourceFile(expected);

    const auto first = pnm::meta::source::detail::load_source(temp_source.string_path());
    ASSERT_TRUE(first.has_value());

    const auto* const first_data = first->data();
    EXPECT_EQ(*first, expected);

    create_allocation_churn();

    EXPECT_EQ(first->data(), first_data);
    EXPECT_EQ(*first, expected);
}

TEST(SourceMetaTests, LoadSourceCachesFirstReadAcrossFileMutation)
{
    constexpr auto original = std::string_view{ "before\nmutation\n" };
    constexpr auto updated = std::string_view{ "after\nmutation\n" };
    const auto temp_source = ScopedTempSourceFile(original);

    const auto first = pnm::meta::source::detail::load_source(temp_source.string_path());
    ASSERT_TRUE(first.has_value());

    const auto* const first_data = first->data();
    EXPECT_EQ(*first, original);

    temp_source.overwrite(updated);

    const auto second = pnm::meta::source::detail::load_source(temp_source.string_path());
    ASSERT_TRUE(second.has_value());

    EXPECT_EQ(second->data(), first_data);
    EXPECT_EQ(*second, original);
    EXPECT_NE(*second, updated);
}

TEST(SourceMetaTests, LoadSourceReturnsNulloptForMissingFile)
{
    const auto missing_path = next_temp_source_path();
    std::filesystem::remove(missing_path);

    EXPECT_FALSE(pnm::meta::source::detail::load_source(missing_path.string()).has_value());
}

TEST(SourceMetaTests, LoadSourceHandlesConcurrentFirstLoad)
{
    constexpr auto expected = std::string_view{ "thread-0\nthread-1\nthread-2\n" };
    constexpr auto thread_count = 8UZ;
    const auto temp_source = ScopedTempSourceFile(expected);

    auto loaded_sources = std::vector<std::optional<std::string_view>>(thread_count);
    auto workers = std::vector<std::thread>{};
    workers.reserve(thread_count);
    std::barrier start_gate(static_cast<std::ptrdiff_t>(thread_count));

    for (std::size_t index = 0; index < thread_count; ++index) {
        workers.emplace_back([&loaded_sources, &start_gate, &temp_source, index]() {
            start_gate.arrive_and_wait();
            loaded_sources[index] = *pnm::meta::source::detail::load_source(temp_source.string_path());
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    ASSERT_TRUE(loaded_sources.front().has_value());
    const auto* const expected_data = loaded_sources.front()->data();

    for (const auto& loaded_source : loaded_sources) {
        ASSERT_TRUE(loaded_source.has_value());
        EXPECT_EQ(*loaded_source, expected);
        EXPECT_EQ(loaded_source->data(), expected_data);
    }
}

TEST(SourceMetaTests, LoadSourceReturnsEmbeddedFileWhenAlreadyRegistered)
{
    const auto embedded = pnm::meta::source::detail::Registry::instance()[__FILE__];
    ASSERT_TRUE(embedded.has_value());

    const auto loaded = pnm::meta::source::detail::load_source(__FILE__);
    ASSERT_TRUE(loaded.has_value());

    EXPECT_EQ(*loaded, embedded->source_code);
    EXPECT_EQ(loaded->data(), embedded->source_code.data());
}

TEST(SourceMetaTests, ExcerptReturnsRequestedLineWithContext)
{
    constexpr auto source = std::string_view{ "alpha\nbeta\ngamma\n" };
    const auto temp_source = ScopedTempSourceFile(source);

    const auto excerpt = pnm::meta::source::excerpt(temp_source.string_path(), 2, 1);

    ASSERT_TRUE(excerpt.has_value());
    EXPECT_NE(excerpt->find("  1 | alpha"), std::string::npos);
    EXPECT_NE(excerpt->find("> 2 | beta"), std::string::npos);
    EXPECT_NE(excerpt->find("  3 | gamma"), std::string::npos);
}

TEST(SourceMetaTests, ExcerptRejectsOutOfRangeLine)
{
    constexpr auto source = std::string_view{ "alpha\nbeta\n" };
    const auto temp_source = ScopedTempSourceFile(source);

    const auto excerpt = pnm::meta::source::excerpt(temp_source.string_path(), 4);

    EXPECT_FALSE(excerpt.has_value());
}
