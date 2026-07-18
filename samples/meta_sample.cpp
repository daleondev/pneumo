#include "pneumo/meta.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <variant>
#include <ratio>

#include <iostream>

// NOLINTBEGIN

enum class SampleState : std::uint8_t
{
    Idle = 0,
    Running = 2,
    Done = 4
};

struct SampleAggregate
{
    int id;
    double weight;
};

template<typename T, typename Ratio, bool IsBase = false>
struct SampleNestedTemplate
{
};

struct SampleNestedTypes
{
    using Index = int;
    using Ratio = std::ratio<2>;
    using TemplateAlias = SampleNestedTemplate<double, std::ratio<3>>;

  private:
    using Hidden = SampleNestedTemplate<float, std::ratio<5>>;
};

struct SampleDispatcher
{
    static constexpr auto triple(int value) -> int { return value * 3; }
};

namespace sample::detail
{
    struct Widget
    {
    };
}

namespace
{
    constexpr auto FIXED_STRING = pnm::meta::string::FixedString{ "sample" };
    static_assert(FIXED_STRING.size() == 6UZ);
    static_assert(static_cast<std::string_view>(FIXED_STRING) == "sample");

    using TupleA = std::tuple<int, double>;
    using TupleB = std::tuple<double, float, int>;

    static_assert(pnm::meta::tuple::Tuple<TupleA>);
    static_assert(!pnm::meta::tuple::Tuple<int>);

    static_assert(pnm::meta::tuple::ContainsType<TupleA, int>);
    static_assert(!pnm::meta::tuple::ContainsType<TupleA, float>);

    using Concatenated = pnm::meta::tuple::concat_types_t<TupleA, TupleB>;
    static_assert(std::same_as<Concatenated, std::tuple<int, double, double, float, int>>);

    using Unique = pnm::meta::tuple::unique_types_t<Concatenated>;
    static_assert(std::same_as<Unique, std::tuple<int, double, float>>);

    static_assert(pnm::meta::tuple::count<TupleA>() == 2UZ);
    static_assert(std::same_as<pnm::meta::tuple::at_t<0, TupleA>, int>);
    static_assert(std::same_as<pnm::meta::tuple::at_t<1, TupleA>, double>);

    using TupleAsVariant = pnm::meta::tuple::to_variant_t<TupleA>;
    static_assert(std::same_as<TupleAsVariant, std::variant<int, double>>);

    using CvrefTuple = std::tuple<const int&, volatile double&&, const std::string>;
    using CvrefRemoved = pnm::meta::tuple::remove_cvref_types_t<CvrefTuple>;
    static_assert(std::same_as<CvrefRemoved, std::tuple<int, double, std::string>>);

    constexpr auto REVERSE_FOR_EACH_CAN_BREAK = [] -> bool {
        auto visited{ 0 };
        pnm::meta::tuple::for_each<TupleB, pnm::meta::Iteration::Reverse>([&visited](auto index) {
            ++visited;
            if constexpr (index == 1) {
                return pnm::meta::Loop::Break;
            }
            else {
                return pnm::meta::Loop::Continue;
            }
        });
        return visited == 2;
    };
    static_assert(REVERSE_FOR_EACH_CAN_BREAK());

    constexpr auto EXPECTED_SUM{ 6 };

    constexpr auto FOR_EACH_SUM_VALID = [] -> bool {
        auto values = std::tuple{ 1, 2, 3 };
        auto sum{ 0 };
        pnm::meta::tuple::for_each_element([&sum](int value) { sum += value; }, values);
        return sum == EXPECTED_SUM;
    };

    static_assert(FOR_EACH_SUM_VALID());

    using VariantInput = std::variant<int, double, int, float>;

    static_assert(pnm::meta::variant::Variant<VariantInput>);
    static_assert(!pnm::meta::variant::Variant<int>);

    using VariantTuple = pnm::meta::variant::to_tuple_t<VariantInput>;
    static_assert(std::same_as<VariantTuple, std::tuple<int, double, int, float>>);

    using VariantUnique = pnm::meta::variant::unique_types_t<VariantInput>;
    static_assert(std::same_as<VariantUnique, std::variant<int, double, float>>);

    using VariantRef = pnm::meta::variant::to_reference_wrapper_t<std::variant<int, const double>>;
    static_assert(
      std::same_as<VariantRef,
                   std::variant<std::reference_wrapper<int>, std::reference_wrapper<const double>>>);

    constexpr auto VARIANT_REVERSE_FOR_EACH_CAN_BREAK = [] -> bool {
        auto visited{ 0 };
        pnm::meta::variant::for_each<VariantInput, pnm::meta::Iteration::Reverse>([&visited](auto index) {
            ++visited;
            if constexpr (index == 2) {
                return pnm::meta::Loop::Break;
            }
            else {
                return pnm::meta::Loop::Continue;
            }
        });
        return visited == 2;
    };
    static_assert(VARIANT_REVERSE_FOR_EACH_CAN_BREAK());

    static_assert(pnm::meta::type::name<sample::detail::Widget>() == "Widget");
    static_assert(pnm::meta::type::namespace_name<sample::detail::Widget>() == "sample::detail");
    static_assert(pnm::meta::type::namespaces<sample::detail::Widget>()[0] == "sample");
    static_assert(pnm::meta::type::namespaces<sample::detail::Widget>()[1] == "detail");
    static_assert(pnm::meta::type::StdType<std::string>);
    static_assert(!pnm::meta::type::StdType<sample::detail::Widget>);

    static_assert(pnm::meta::enumeration::ScopedEnum<SampleState>);
    static_assert(!pnm::meta::enumeration::ScopedEnum<int>);

    static_assert(pnm::meta::enumeration::count<SampleState>() == 3UZ);

    constexpr auto SAMPLE_ENUMERATORS = pnm::meta::enumeration::enumerators<SampleState>();
    static_assert(SAMPLE_ENUMERATORS[0] == SampleState::Idle);
    static_assert(SAMPLE_ENUMERATORS[1] == SampleState::Running);
    static_assert(SAMPLE_ENUMERATORS[2] == SampleState::Done);

    constexpr auto SAMPLE_UNDERLYING = pnm::meta::enumeration::underlying_enumerators<SampleState>();
    static_assert(std::same_as<decltype(SAMPLE_UNDERLYING), const std::array<std::uint8_t, 3>>);
    static_assert(SAMPLE_UNDERLYING[0] == 0);
    static_assert(SAMPLE_UNDERLYING[1] == 2);
    static_assert(SAMPLE_UNDERLYING[2] == 4);

    constexpr auto SAMPLE_ENUMERATOR_NAMES = pnm::meta::enumeration::enumerator_names<SampleState>();
    static_assert(SAMPLE_ENUMERATOR_NAMES[0] == "Idle");
    static_assert(SAMPLE_ENUMERATOR_NAMES[1] == "Running");
    static_assert(SAMPLE_ENUMERATOR_NAMES[2] == "Done");

    static_assert(pnm::meta::enumeration::name<SampleState>() == "SampleState");
    static_assert(pnm::meta::enumeration::enumerator_name(SampleState::Running) == "Running");

    static_assert(pnm::meta::structural::field_count<SampleAggregate>() == 2UZ);
    constexpr auto SAMPLE_FIELD_NAMES = pnm::meta::structural::field_names<SampleAggregate>();
    static_assert(SAMPLE_FIELD_NAMES[0] == "id");
    static_assert(SAMPLE_FIELD_NAMES[1] == "weight");

    static_assert(std::same_as<pnm::meta::structural::field_type_t<0, SampleAggregate>, int>);
    static_assert(std::same_as<pnm::meta::structural::field_type_t<1, SampleAggregate>, double>);
    static_assert(std::same_as<pnm::meta::structural::field_types_t<SampleAggregate>, std::tuple<int, double>>);

    constexpr auto SAMPLE_ID_VALUE = 7;
    constexpr auto SAMPLE_WEIGHT_VALUE = 1.5;

    constexpr auto SAMPLE_FIELD_GET_VALID = [] -> bool {
        auto value = SampleAggregate{ .id = SAMPLE_ID_VALUE, .weight = SAMPLE_WEIGHT_VALUE };
        return pnm::meta::structural::get<0>(value) == SAMPLE_ID_VALUE &&
               pnm::meta::structural::get<1>(value) == SAMPLE_WEIGHT_VALUE;
    };
    static_assert(SAMPLE_FIELD_GET_VALID());

    using NestedTypes = pnm::meta::structural::nested_types_t<SampleNestedTypes>;
    static_assert(
      std::same_as<NestedTypes,
                   std::tuple<int, std::ratio<2>, SampleNestedTemplate<double, std::ratio<3>>>>);
    static_assert(std::same_as<pnm::meta::structural::nested_type_t<0, SampleNestedTypes>, int>);
    static_assert(pnm::meta::structural::nested_type_name<0, SampleNestedTypes>() == "Index");
    constexpr auto SAMPLE_NESTED_TYPE_NAMES = pnm::meta::structural::nested_type_names<SampleNestedTypes>();
    static_assert(SAMPLE_NESTED_TYPE_NAMES[1] == "Ratio");
    static_assert(SAMPLE_NESTED_TYPE_NAMES[2] == "TemplateAlias");

    using AggregateInfo = pnm::meta::structural::Info<SampleAggregate>;
    using NestedInfo = pnm::meta::structural::Info<SampleNestedTypes>;
    static_assert(AggregateInfo::NAME == "SampleAggregate");
    static_assert(AggregateInfo::numMembers() == 2UZ);
    static_assert(AggregateInfo::MEMBER_NAMES[0] == "id");
    static_assert(NestedInfo::numNestedTypes() == 3UZ);
    static_assert(NestedInfo::NESTED_TYPE_NAMES[0] == "Index");

    static_assert(pnm::meta::structural::dispatch<SampleDispatcher, "triple">(7) == 21);
}

PNM_META_SOURCE_EMBED_CURRENT

auto main() -> int
{
    std::cout << *pnm::meta::source::excerpt(__FILE__, __LINE__, 1) << std::endl;

    using MyVariant = std::variant<int, double, int, float>;
    pnm::meta::variant::for_each<MyVariant>([](auto index) {
        using Type = std::variant_alternative_t<index, MyVariant>;
        constexpr auto type_name{ pnm::meta::type::name<Type>() };
        std::cout << "Variant alternative at index " << index << ": " << type_name << '\n';
    });

    std::cout << "Variant reverse iteration with break:\n";
    pnm::meta::variant::for_each<MyVariant, pnm::meta::Iteration::Reverse>([](auto index) {
        using Type = pnm::meta::variant::at_t<index, MyVariant>;
        constexpr auto type_name{ pnm::meta::type::name<Type>() };
        std::cout << "  index " << index << ": " << type_name << '\n';
        if constexpr (index == 2) {
            return pnm::meta::Loop::Break;
        }
        else {
            return pnm::meta::Loop::Continue;
        }
    });

    using MyTuple = std::tuple<int, double, std::string>;
    MyTuple tuple{ 42, 3.14, "Hello" };
    pnm::meta::tuple::for_each([&tuple](auto index) {
        using Type = std::tuple_element_t<index, MyTuple>;
        constexpr auto type_name{ pnm::meta::type::name<Type>() };
        auto value = std::get<index>(tuple);
        std::cout << "Tuple element at index " << index << ": type = " << type_name << ", value = " << value
                  << '\n';
    }, tuple);

    pnm::meta::tuple::for_each_element([](auto& val) {
        using Type = std::remove_cvref_t<decltype(val)>;
        constexpr auto type_name{ pnm::meta::type::name<Type>() };
        std::cout << "Tuple element: type = " << type_name << ", value = " << val << '\n';
    }, tuple);

    std::cout << "Tuple values in reverse until double:\n";
    pnm::meta::tuple::for_each_element<pnm::meta::Iteration::Reverse>([](const auto& val) {
        using Type = std::remove_cvref_t<decltype(val)>;
        constexpr auto type_name{ pnm::meta::type::name<Type>() };
        std::cout << "  type = " << type_name << ", value = " << val << '\n';
        if constexpr (std::same_as<Type, double>) {
            return pnm::meta::Loop::Break;
        }
        else {
            return pnm::meta::Loop::Continue;
        }
    }, tuple);

    constexpr auto nested_names = pnm::meta::structural::nested_type_names<SampleNestedTypes>();
    std::cout << "SampleNestedTypes exposes " << nested_names.size() << " public nested types:\n";
    for (auto name : nested_names) {
        std::cout << "  " << name << '\n';
    }

    return 0;
}

// NOLINTEND
