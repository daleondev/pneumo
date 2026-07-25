#pragma once

#include "common.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <deque>
#include <filesystem>
#include <flat_map>
#include <fstream>
#include <functional>
#include <iomanip>
#include <meta>
#include <mutex>
#include <optional>
#include <ranges>
#include <source_location>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

namespace pnm::meta
{
    enum class Loop : uint8_t
    {
        Continue,
        Break
    };

    enum class Iteration : uint8_t
    {
        Forward,
        Reverse
    };

    namespace detail
    {
        // NOLINTBEGIN(readability-identifier-naming)
        template<typename T, typename... Ts>
        struct contains : std::disjunction<std::is_same<T, Ts>...>
        {
        };

        template<typename T, typename... Ts>
        inline constexpr bool contains_v = contains<T, Ts...>::value;
        // NOLINTEND(readability-identifier-naming)
    }

    namespace string
    {
        template<typename T>
        concept Character =
          std::same_as<std::remove_cv_t<T>, char> || std::same_as<std::remove_cv_t<T>, char8_t> ||
          std::same_as<std::remove_cv_t<T>, char16_t> || std::same_as<std::remove_cv_t<T>, char32_t> ||
          std::same_as<std::remove_cv_t<T>, wchar_t>;

        // NOLINTBEGIN
        template<size_t Size, Character Char = char>
        struct FixedString
        {
            constexpr FixedString(std::array<Char, Size>&& arr) { std::ranges::move(arr, data.begin()); }
            constexpr FixedString(const Char* str) { std::copy_n(str, Size, data.begin()); }
            constexpr auto operator<=>(const FixedString&) const = default;
            constexpr operator std::basic_string_view<Char>() const { return { data.data(), Size }; }
            constexpr auto size() const { return Size; }

            template<typename Self>
                requires std::is_lvalue_reference_v<Self>
            constexpr auto begin(this Self&& self)
            {
                return std::forward<Self>(self).data.begin();
            }

            template<typename Self>
                requires std::is_lvalue_reference_v<Self>
            constexpr auto end(this Self&& self)
            {
                auto offset{ self.size() };
                return std::forward<Self>(self).data.begin() + offset;
            }

            std::array<Char, Size + 1uz> data{};
        };
        template<size_t Size, Character Char = char>
        FixedString(std::array<Char, Size>&& arr) -> FixedString<Size, Char>;
        template<size_t Capacity, size_t Size = Capacity - 1, Character Char = char>
        FixedString(const Char (&str)[Capacity]) -> FixedString<Size, Char>;
        // NOLINTEND

        template<const auto& SV>
        static constexpr auto fixed_from_sv()
        {
            using Char = typename std::remove_cvref_t<decltype(SV)>::value_type;
            return FixedString<SV.size(), Char>(SV.data());
        }
    }

    namespace tuple
    {
        namespace detail
        {
            // NOLINTBEGIN(readability-identifier-naming)
            template<typename T>
            struct is_tuple : std::false_type
            {
            };

            template<typename... Ts>
            struct is_tuple<std::tuple<Ts...>> : std::true_type
            {
            };

            template<typename T>
            inline constexpr bool is_tuple_v = is_tuple<std::remove_cvref_t<T>>::value;

            template<typename Tuple, typename T>
            struct tuple_contains_type;

            template<typename T, typename... Ts>
            struct tuple_contains_type<std::tuple<Ts...>, T>
              : std::bool_constant<
                  pnm::meta::detail::contains_v<std::remove_cvref_t<T>, std::remove_cvref_t<Ts>...>>
            {
            };

            template<typename Tuple, typename T>
            inline constexpr bool tuple_contains_type_v =
              tuple_contains_type<std::remove_cvref_t<Tuple>, std::remove_cvref_t<T>>::value;

            template<typename First, typename Second>
            struct tuple_concat;

            template<typename... FirstTs, typename... SecondTs>
            struct tuple_concat<std::tuple<FirstTs...>, std::tuple<SecondTs...>>
            {
                using type = std::tuple<FirstTs..., SecondTs...>;
            };

            template<typename T, typename Tuple>
            struct tuple_append;

            template<typename T, typename... Ts>
            struct tuple_append<T, std::tuple<Ts...>>
            {
                using type = std::tuple<Ts..., T>;
            };

            template<typename Accumulator, typename Input>
            struct tuple_unique_impl;

            template<typename Accumulator>
            struct tuple_unique_impl<Accumulator, std::tuple<>>
            {
                using type = Accumulator;
            };

            template<typename Accumulator, typename Head, typename... Tail>
            struct tuple_unique_impl<Accumulator, std::tuple<Head, Tail...>>
            {
                using next_accumulator = std::conditional_t<tuple_contains_type_v<Accumulator, Head>,
                                                            Accumulator,
                                                            typename tuple_append<Head, Accumulator>::type>;
                using type = typename tuple_unique_impl<next_accumulator, std::tuple<Tail...>>::type;
            };

            template<typename Tuple>
            struct tuple_unique;

            template<typename... Ts>
            struct tuple_unique<std::tuple<Ts...>>
            {
                using type = typename tuple_unique_impl<std::tuple<>, std::tuple<Ts...>>::type;
            };

            template<typename Tuple>
            struct tuple_to_variant;

            template<typename... Ts>
            struct tuple_to_variant<std::tuple<Ts...>>
            {
                using type = std::variant<Ts...>;
            };

            template<typename Tuple>
            struct tuple_remove_cvref;

            template<typename... Ts>
            struct tuple_remove_cvref<std::tuple<Ts...>>
            {
                using type = std::tuple<std::remove_cvref_t<Ts>...>;
            };

            template<typename First, typename Second>
                requires is_tuple_v<First> && is_tuple_v<Second>
            consteval auto concat_types() -> std::type_identity<
              typename tuple_concat<std::remove_cvref_t<First>, std::remove_cvref_t<Second>>::type>
            {
                return {};
            }

            template<typename T>
                requires is_tuple_v<T>
            consteval auto unique_types()
              -> std::type_identity<typename tuple_unique<std::remove_cvref_t<T>>::type>
            {
                return {};
            }

            template<typename T>
                requires is_tuple_v<T>
            consteval auto to_variant()
              -> std::type_identity<typename tuple_to_variant<std::remove_cvref_t<T>>::type>
            {
                return {};
            }

            template<typename T>
                requires is_tuple_v<T>
            consteval auto remove_cvref_types()
              -> std::type_identity<typename tuple_remove_cvref<std::remove_cvref_t<T>>::type>
            {
                return {};
            }

            template<typename Fn, typename T, size_t... Is>
            consteval auto for_each_fn(std::index_sequence<Is...> indices) -> bool
            {
                static_cast<void>(indices);
                return (std::invocable<Fn&, std::integral_constant<size_t, Is>> && ...);
            }

            template<typename Fn, typename T>
            inline constexpr bool for_each_fn_v =
              for_each_fn<Fn, T>(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{});

            template<typename Fn, typename T, size_t... Is>
            consteval auto for_each_element_fn(std::index_sequence<Is...> indices) -> bool
            {
                static_cast<void>(indices);
                return (std::invocable<Fn&, decltype(std::get<Is>(std::declval<T>()))> && ...);
            }

            template<typename Fn, typename T>
            inline constexpr bool for_each_element_fn_v = for_each_element_fn<Fn, T>(
              std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<T>>>{});
            // NOLINTEND(readability-identifier-naming)
        }

        template<typename T>
        concept Tuple = detail::is_tuple_v<T>;

        template<typename T, typename U>
        concept ContainsType = Tuple<T> && detail::tuple_contains_type_v<T, U>;

        template<typename Fn, typename T>
        concept ForEachFn = Tuple<T> && detail::for_each_fn_v<Fn, T>;

        template<typename Fn, typename T>
        concept ForEachElementFn = Tuple<T> && detail::for_each_element_fn_v<Fn, T>;

        template<Tuple T>
        consteval auto count() -> size_t
        {
            return std::tuple_size_v<std::remove_cvref_t<T>>;
        }

        template<size_t Index, Tuple T>
        using at_t = std::tuple_element_t<Index, std::remove_cvref_t<T>>;

        template<Tuple First, Tuple Second>
        using concat_types_t = typename decltype(detail::concat_types<First, Second>())::type;

        template<Tuple T>
        using unique_types_t = typename decltype(detail::unique_types<T>())::type;

        template<Tuple T>
        using to_variant_t = typename decltype(detail::to_variant<T>())::type;

        template<Tuple T>
        using remove_cvref_types_t = typename decltype(detail::remove_cvref_types<T>())::type;

        template<Tuple T, Iteration Dir = Iteration::Forward, typename Fn>
            requires ForEachFn<Fn, T>
        constexpr auto for_each(Fn&& fn) -> void
        {
            auto&& callable = std::forward<Fn>(fn);
            [&callable]<size_t... Is>(std::index_sequence<Is...>) {
                (... && [&callable]<size_t I>() -> bool {
                    constexpr auto i{
                        std::integral_constant<size_t, Dir == Iteration::Reverse ? count<T>() - 1 - I : I>{}
                    };

                    using Ret = std::invoke_result_t<Fn, decltype(i)>;
                    if constexpr (std::is_void_v<Ret>) {
                        std::invoke(callable, i);
                        return true;
                    }
                    else if constexpr (std::same_as<Ret, Loop>) {
                        auto ret{ std::invoke(callable, i) };
                        return static_cast<Loop>(ret) == Loop::Continue;
                    }
                    else {
                        static_assert(false, "for_each fn must return 'void' or 'Loop' control token.");
                    }
                }.template operator()<Is>());
            }(std::make_index_sequence<count<T>()>{});
        }

        template<typename Fn, Tuple T>
        // NOLINTNEXTLINE(readability-named-parameter)
        constexpr auto for_each(Fn&& fn, T&&) -> void
        {
            for_each<T>(std::forward<Fn>(fn));
        }

        template<Iteration Dir = Iteration::Forward, typename Fn, Tuple T>
            requires ForEachElementFn<Fn, T>
        // NOLINTNEXTLINE(readability-identifier-naming)
        constexpr auto for_each_element(Fn&& fn, T&& tp) -> void
        {
            auto&& callable = std::forward<Fn>(fn);
            auto&& tuple = std::forward<T>(tp);

            [&callable, &tuple]<size_t... Is>(std::index_sequence<Is...>) {
                // NOLINTNEXTLINE(bugprone-use-after-move)
                (... && [&callable, &tuple]<size_t I>() -> bool {
                    constexpr auto i{ Dir == Iteration::Reverse ? count<T>() - 1 - I : I };

                    using Ret = std::invoke_result_t<Fn, at_t<i, T>&>;
                    if constexpr (std::is_void_v<Ret>) {
                        std::invoke(callable, std::get<i>(std::forward<decltype(tuple)>(tuple)));
                        return true;
                    }
                    else if constexpr (std::same_as<Ret, Loop>) {
                        auto ret{ std::invoke(callable, std::get<i>(std::forward<decltype(tuple)>(tuple))) };
                        return static_cast<Loop>(ret) == Loop::Continue;
                    }
                    else {
                        static_assert(false, "for_each fn must return 'void' or 'Loop' control token.");
                    }
                }.template operator()<Is>());
            }(std::make_index_sequence<count<T>()>{});
        }
    }

    namespace variant
    {
        namespace detail
        {
            // NOLINTBEGIN(readability-identifier-naming)
            template<typename T>
            struct is_variant : std::false_type
            {
            };

            template<typename... Ts>
            struct is_variant<std::variant<Ts...>> : std::true_type
            {
            };

            template<typename T>
            inline constexpr bool is_variant_v = is_variant<std::remove_cvref_t<T>>::value;

            template<typename Variant>
            struct variant_to_tuple;

            template<typename... Ts>
            struct variant_to_tuple<std::variant<Ts...>>
            {
                using type = std::tuple<Ts...>;
            };

            template<typename Variant>
            struct variant_unique;

            template<typename... Ts>
            struct variant_unique<std::variant<Ts...>>
            {
                using raw_tuple = std::tuple<Ts...>;
                using unique_tuple = pnm::meta::tuple::unique_types_t<raw_tuple>;
                using type = pnm::meta::tuple::to_variant_t<unique_tuple>;
            };

            template<typename Variant>
            struct variant_to_reference_wrapper;

            template<typename... Ts>
            struct variant_to_reference_wrapper<std::variant<Ts...>>
            {
                using type = std::variant<std::reference_wrapper<Ts>...>;
            };

            template<typename T>
                requires is_variant_v<T>
            consteval auto to_tuple()
              -> std::type_identity<typename variant_to_tuple<std::remove_cvref_t<T>>::type>
            {
                return {};
            }

            template<typename T>
                requires is_variant_v<T>
            consteval auto unique_types()
              -> std::type_identity<typename variant_unique<std::remove_cvref_t<T>>::type>
            {
                return {};
            }

            template<typename T>
                requires is_variant_v<T>
            consteval auto to_reference_wrapper()
              -> std::type_identity<typename variant_to_reference_wrapper<std::remove_cvref_t<T>>::type>
            {
                return {};
            }

            template<typename Fn, typename T, size_t... Is>
            consteval auto for_each_fn(std::index_sequence<Is...> indices) -> bool
            {
                static_cast<void>(indices);
                return (std::invocable<Fn&, std::integral_constant<size_t, Is>> && ...);
            }

            template<typename Fn, typename T>
            inline constexpr bool for_each_fn_v =
              for_each_fn<Fn, T>(std::make_index_sequence<std::variant_size_v<std::remove_cvref_t<T>>>{});
            // NOLINTEND(readability-identifier-naming)
        }

        template<typename T>
        concept Variant = detail::is_variant_v<T>;

        template<typename Fn, typename T>
        concept ForEachFn = Variant<T> && detail::for_each_fn_v<Fn, T>;

        template<Variant T>
        consteval auto count() -> size_t
        {
            return std::variant_size_v<std::remove_cvref_t<T>>;
        }

        template<size_t Index, Variant T>
        using at_t = std::variant_alternative_t<Index, std::remove_cvref_t<T>>;

        template<Variant T>
        using to_tuple_t = typename decltype(detail::to_tuple<T>())::type;

        template<Variant T>
        using unique_types_t = typename decltype(detail::unique_types<T>())::type;

        template<Variant T>
        using to_reference_wrapper_t = typename decltype(detail::to_reference_wrapper<T>())::type;

        template<Variant T, Iteration Dir = Iteration::Forward, typename Fn>
            requires ForEachFn<Fn, T>
        constexpr auto for_each(Fn&& fn) -> void
        {
            auto&& callable = std::forward<Fn>(fn);
            [&callable]<size_t... Is>(std::index_sequence<Is...>) {
                (... && [&callable]<size_t I>() -> bool {
                    constexpr auto i{
                        std::integral_constant<size_t, Dir == Iteration::Reverse ? count<T>() - 1 - I : I>{}
                    };

                    using Ret = std::invoke_result_t<Fn, decltype(i)>;
                    if constexpr (std::is_void_v<Ret>) {
                        std::invoke(callable, i);
                        return true;
                    }
                    else if constexpr (std::same_as<Ret, Loop>) {
                        auto ret{ std::invoke(callable, i) };
                        return static_cast<Loop>(ret) == Loop::Continue;
                    }
                    else {
                        static_assert(false, "for_each fn must return 'void' or 'Loop' control token.");
                    }
                }.template operator()<Is>());
            }(std::make_index_sequence<count<T>()>{});
        }

        template<typename Fn, Variant T>
        // NOLINTNEXTLINE(readability-named-parameter)
        constexpr auto for_each(Fn&& fn, T&&) -> void
        {
            for_each<T>(std::forward<Fn>(fn));
        }
    }

    namespace type
    {
        namespace detail
        {
            template<typename T>
            consteval auto has_identifier() -> bool
            {
                return std::meta::has_identifier(^^T);
            }

            template<typename T>
            consteval auto namespace_name_len() -> size_t
            {
                auto type_meta{ ^^T };
                auto len{ 0UZ };
                while (std::meta::has_identifier(std::meta::parent_of(type_meta))) {
                    type_meta = std::meta::parent_of(type_meta);
                    len += std::meta::identifier_of(type_meta).length();
                    len += 2; // for "::"
                }
                len -= 2; // remove the last "::" added in the loop
                return len;
            }

            template<typename T>
            consteval auto num_namespaces() -> size_t
            {
                auto type_meta{ ^^T };
                auto num{ 0UZ };
                while (std::meta::has_identifier(std::meta::parent_of(type_meta))) {
                    type_meta = std::meta::parent_of(type_meta);
                    num += 1;
                }
                return num;
            }
        }

        template<typename T>
        concept HasIdentifier = detail::has_identifier<T>();

        template<typename T>
        constexpr auto name() -> std::string_view
        {
            if constexpr (HasIdentifier<T>) {
                return std::meta::identifier_of(^^T);
            }
            else {
                return std::meta::display_string_of(^^T);
            }
        }

        template<typename T>
        constexpr auto namespace_name() -> pnm::meta::string::FixedString<detail::namespace_name_len<T>()>
        {
            std::array<char, detail::namespace_name_len<T>()> ns{};
            auto curr{ ^^T };
            auto parent{ std::meta::parent_of(curr) };
            auto it{ ns.end() };
            while (std::meta::has_identifier(parent)) {
                curr = parent;
                parent = std::meta::parent_of(curr);
                it = std::ranges::copy_backward(std::meta::identifier_of(curr), it).out;
                if (std::meta::has_identifier(parent)) {
                    using namespace std::literals;
                    it = std::ranges::copy_backward("::"sv, it).out;
                }
            }
            return ns;
        }

        template<typename T>
        constexpr auto namespaces() -> std::array<std::string_view, detail::num_namespaces<T>()>
        {
            std::array<std::string_view, detail::num_namespaces<T>()> ns{};
            auto curr{ ^^T };
            auto parent{ std::meta::parent_of(curr) };
            auto index{ detail::num_namespaces<T>() };
            while (std::meta::has_identifier(parent)) {
                curr = parent;
                parent = std::meta::parent_of(curr);
                ns[--index] = std::meta::identifier_of(curr);
            }
            return ns;
        }

        namespace detail
        {
            template<typename T>
            consteval auto is_std_type() -> bool
            {
                if constexpr (num_namespaces<T>() == 0) {
                    return false;
                }
                return namespaces<T>().front() == "std";
            }
        }

        template<typename T>
        concept StdType = detail::is_std_type<T>();
    }

    namespace enumeration
    {
        namespace detail
        {
            template<typename Enum>
            consteval auto enumerator_reflections()
            {
                return std::define_static_array(std::meta::enumerators_of(^^Enum));
            }
        }

        template<typename T>
        concept ScopedEnum = std::is_scoped_enum_v<std::remove_cvref_t<T>>;

        template<ScopedEnum Enum>
        constexpr auto name() -> std::string_view
        {
            return type::name<std::remove_cvref_t<Enum>>();
        }

        template<ScopedEnum Enum>
        constexpr auto count()
        {
            return detail::enumerator_reflections<Enum>().size();
        }

        template<ScopedEnum Enum>
        constexpr auto enumerators()
        {
            constexpr auto enumerator_reflections{ detail::enumerator_reflections<Enum>() };
            return [enumerator_reflections]<size_t... Is>(std::index_sequence<Is...>)
              ->std::array<Enum, sizeof...(Is)>
            {
                return std::array<Enum, sizeof...(Is)>{[:enumerator_reflections[Is]:]... };
            }
            (std::make_index_sequence<enumerator_reflections.size()>{});
        }

        template<ScopedEnum Enum>
        constexpr auto enumerator_names() -> std::array<std::string_view, count<Enum>()>
        {
            constexpr auto enumerator_reflections{ detail::enumerator_reflections<Enum>() };
            return [enumerator_reflections]<size_t... Is>(std::index_sequence<Is...>)
              ->std::array<std::string_view, sizeof...(Is)>
            {
                return std::array<std::string_view, sizeof...(Is)>{ std::meta::identifier_of(
                  enumerator_reflections[Is])... };
            }
            (std::make_index_sequence<enumerator_reflections.size()>{});
        }

        template<ScopedEnum Enum>
        constexpr auto underlying_enumerators()
        {
            constexpr auto actual_enumerators{ enumerators<Enum>() };
            return [actual_enumerators]<size_t... Is>(std::index_sequence<Is...>)
              ->std::array<std::underlying_type_t<Enum>, sizeof...(Is)>
            {
                return std::array<std::underlying_type_t<Enum>, sizeof...(Is)>{ std::to_underlying(
                  actual_enumerators[Is])... };
            }
            (std::make_index_sequence<actual_enumerators.size()>{});
        }

        template<auto Enumerator>
        constexpr auto enumerator_name() -> std::string_view
        {
            using Enum = std::remove_cvref_t<decltype(Enumerator)>;
            // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
            template for (constexpr auto e : detail::enumerator_reflections<Enum>())
            {
                if constexpr (Enumerator == [:e:]) {
                    return std::meta::identifier_of(e);
                }
            }
            return "";
        }

        template<ScopedEnum Enum>
        constexpr auto enumerator_name(Enum enumerator) -> std::string_view
        {
            constexpr auto actual_enumerators{ enumerators<Enum>() };
            constexpr auto names{ enumerator_names<Enum>() };
            for (auto i{ 0UZ }; i < actual_enumerators.size(); ++i) {
                if (actual_enumerators[i] == enumerator) {
                    return names[i];
                }
            }
            return "";
        }

        template<ScopedEnum Enum>
        constexpr std::optional<Enum> from_string(std::string_view name)
        {
            // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
            template for (constexpr auto e : detail::enumerator_reflections<Enum>())
            {
                if (name == std::meta::identifier_of(e)) {
                    return [:e:];
                }
            }
            return std::nullopt;
        }
    }

    namespace structural
    {
        namespace detail
        {
            template<typename T>
                requires std::is_class_v<T>
            consteval auto field_reflections()
            {
                constexpr auto access_ctx{ std::meta::access_context::current() };
                std::vector<std::meta::info> data_members{};
                for (const auto member :
                     std::meta::nonstatic_data_members_of(^^std::remove_cvref_t<T>, access_ctx)) {
                    if (std::meta::is_public(member)) {
                        data_members.push_back(member);
                    }
                }
                return std::define_static_array(data_members);
            }

            template<typename T>
                requires std::is_class_v<T>
            consteval auto nested_type_reflections()
            {
                constexpr auto access_ctx{ std::meta::access_context::current() };
                std::vector<std::meta::info> type_members{};
#if defined(__clang__) && defined(_LIBCPP_VERSION)
                // libc++ maps type-alias declarations to type entities before returning from members_of.
                // Clang currently loses access metadata on aliases to class-template specializations there.
                using Iterator =
                  std::meta::__range_of_infos::iterator<std::meta::__range_of_infos::front_member_of_fn,
                                                        std::meta::__range_of_infos::next_member_of_fn,
                                                        std::meta::__range_of_infos::map_identity_fn>;
                using Range = std::meta::__range_of_infos::range<Iterator>;
                for (const auto member_decl : Range(^^std::remove_cvref_t<T>)) {
                    const auto member{ std::meta::__range_of_infos::map_decl_to_entity_fn{}(member_decl) };
                    if (std::meta::is_type(member) && std::meta::is_public(member_decl) &&
                        std::meta::is_accessible(member_decl, access_ctx)) {
                        type_members.push_back(member);
                    }
                }
#else
                for (const auto member : std::meta::members_of(^^std::remove_cvref_t<T>, access_ctx)) {
                    if (std::meta::is_type(member) && std::meta::is_public(member)) {
                        type_members.push_back(member);
                    }
                }
#endif

                return std::define_static_array(type_members);
            }

            template<typename T>
                requires std::is_class_v<T>
            consteval auto method_reflections()
            {
                constexpr auto access_ctx{ std::meta::access_context::current() };
                std::vector<std::meta::info> methods{};

                for (const auto member : std::meta::members_of(^^std::remove_cvref_t<T>, access_ctx)) {
                    if (std::meta::is_function(member) && !std::meta::is_constructor(member) &&
                        !std::meta::is_destructor(member) && !std::meta::is_function_template(member) &&
                        !std::meta::is_deleted(member) && std::meta::has_identifier(member) &&
                        std::meta::is_public(member)) {
                        methods.push_back(member);
                    }
                }

                return std::define_static_array(methods);
            }

            template<std::meta::info Method>
            consteval auto is_const_method() -> bool
            {
                return std::meta::is_const(std::meta::type_of(Method));
            }

            template<typename T, std::meta::info Method>
            consteval auto is_getter() -> bool
            {
                using Type = std::remove_cvref_t<T>;

                if constexpr (std::meta::is_static_member(Method) || !is_const_method<Method>()) {
                    return false;
                }
                else {
                    constexpr auto method_type{ std::meta::type_of(Method) };
                    using MethodType = typename[:method_type:];
                    using MemberPointer = MethodType Type::* const;
                    if constexpr (!std::is_invocable_v<MemberPointer, const Type&>) {
                        return false;
                    }
                    else {
                        using ReturnType = std::invoke_result_t<MemberPointer, const Type&>;
                        return !std::is_void_v<ReturnType>;
                    }
                }
            }

            template<typename T>
                requires std::is_class_v<T>
            consteval auto getter_reflections()
            {
                static constexpr auto methods{ method_reflections<std::remove_cvref_t<T>>() };

                std::vector<std::meta::info> getters{};
                // NOLINTNEXTLINE(bugprone-reserved-identifier,readability-identifier-naming)
                template for (constexpr auto method : methods)
                {
                    if constexpr (is_getter<T, method>()) {
                        getters.push_back(method);
                    }
                }
                return std::define_static_array(getters);
            }

            template<size_t Index, typename T>
            consteval auto field_type()
            {
                constexpr auto fields{ field_reflections<T>() };
                static_assert(Index < fields.size());
                return std::type_identity<typename[:std::meta::type_of(fields[Index]):]>{};
            }

            template<typename T>
            consteval auto field_types()
            {
                return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                    return std::type_identity<
                      std::tuple<typename decltype(detail::field_type<Is, T>())::type...>>{};
                }(std::make_index_sequence<field_reflections<T>().size()>{});
            }

            template<size_t Index, typename T>
            consteval auto nested_type()
            {
                constexpr auto types{ nested_type_reflections<T>() };
                static_assert(Index < types.size());
                return std::type_identity<typename[:types[Index]:]>{};
            }

            template<typename T>
            consteval auto nested_types()
            {
                return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                    return std::type_identity<
                      std::tuple<typename decltype(detail::nested_type<Is, T>())::type...>>{};
                }(std::make_index_sequence<nested_type_reflections<T>().size()>{});
            }

            template<size_t Index, typename T>
            consteval auto method_type()
            {
                constexpr auto methods{ method_reflections<T>() };
                static_assert(Index < methods.size());
                return std::type_identity<typename[:std::meta::type_of(methods[Index]):]>{};
            }

            template<typename T>
            consteval auto method_types()
            {
                return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                    return std::type_identity<
                      std::tuple<typename decltype(detail::method_type<Is, T>())::type...>>{};
                }(std::make_index_sequence<method_reflections<std::remove_cvref_t<T>>().size()>{});
            }
        }

        template<typename T>
        concept Class = std::is_class_v<std::remove_cvref_t<T>>;

        template<Class T>
        constexpr auto field_count() -> size_t
        {
            return detail::field_reflections<std::remove_cvref_t<T>>().size();
        }

        template<size_t Index, typename T>
        consteval auto field_name()
        {
            constexpr auto fields{ detail::field_reflections<T>() };
            static_assert(Index < fields.size());
            return std::meta::identifier_of(fields[Index]);
        }

        template<Class T>
        constexpr auto field_names()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::array<std::string_view, sizeof...(Is)>{ field_name<Is, T>()... };
            }(std::make_index_sequence<detail::field_reflections<T>().size()>{});
        }

        template<size_t Index, Class T>
        using field_type_t = typename decltype(detail::field_type<Index, T>())::type;

        template<Class T>
        using field_types_t = typename decltype(detail::field_types<T>())::type;

        template<size_t Index, typename T>
        consteval auto nested_type_name()
        {
            constexpr auto types{ detail::nested_type_reflections<T>() };
            static_assert(Index < types.size());
            return std::meta::identifier_of(types[Index]);
        }

        template<Class T>
        constexpr auto nested_type_names()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::array<std::string_view, sizeof...(Is)>{ nested_type_name<Is, T>()... };
            }(std::make_index_sequence<detail::nested_type_reflections<T>().size()>{});
        }

        template<size_t Index, typename T>
        using nested_type_t = typename decltype(detail::nested_type<Index, T>())::type;

        template<typename T>
        using nested_types_t = typename decltype(detail::nested_types<T>())::type;

        template<Class T>
        constexpr auto nested_type_count() -> size_t
        {
            return detail::nested_type_reflections<std::remove_cvref_t<T>>().size();
        }

        template<Class T>
        constexpr auto method_count() -> size_t
        {
            return detail::method_reflections<std::remove_cvref_t<T>>().size();
        }

        template<size_t Index, typename T>
        consteval auto method_name()
        {
            constexpr auto methods{ detail::method_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < methods.size());
            return std::meta::identifier_of(methods[Index]);
        }

        template<Class T>
        constexpr auto method_names()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::array<std::string_view, sizeof...(Is)>{ method_name<Is, T>()... };
            }(std::make_index_sequence<detail::method_reflections<std::remove_cvref_t<T>>().size()>{});
        }

        template<size_t Index, Class T>
        using method_type_t = typename decltype(detail::method_type<Index, T>())::type;

        template<Class T>
        using method_types_t = typename decltype(detail::method_types<T>())::type;

        template<size_t Index, typename T>
        consteval auto is_static_method() -> bool
        {
            constexpr auto methods{ detail::method_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < methods.size());
            return std::meta::is_static_member(methods[Index]);
        }

        template<size_t Index, typename T>
        consteval auto is_const_method() -> bool
        {
            constexpr auto methods{ detail::method_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < methods.size());
            return detail::is_const_method<methods[Index]>();
        }

        template<size_t Index, Class T, typename... Args>
        constexpr auto invoke_method(T&& value, Args&&... args) -> decltype(auto)
        {
            constexpr auto methods{ detail::method_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < methods.size());
            static_assert(!std::meta::is_static_member(methods[Index]));
            return (std::forward<T>(value).[:methods[Index]:](std::forward<Args>(args)...));
        }

        template<size_t Index, Class T, typename... Args>
        constexpr auto invoke_static_method(Args&&... args) -> decltype(auto)
        {
            constexpr auto methods{ detail::method_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < methods.size());
            static_assert(std::meta::is_static_member(methods[Index]));
            return [:methods[Index]:](std::forward<Args>(args)...);
        }

        template<Class T>
        constexpr auto getter_count() -> size_t
        {
            return detail::getter_reflections<std::remove_cvref_t<T>>().size();
        }

        template<size_t Index, typename T>
        consteval auto getter_name()
        {
            constexpr auto getters{ detail::getter_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < getters.size());
            return std::meta::identifier_of(getters[Index]);
        }

        template<Class T>
        constexpr auto getter_names()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::array<std::string_view, sizeof...(Is)>{ getter_name<Is, T>()... };
            }(std::make_index_sequence<detail::getter_reflections<std::remove_cvref_t<T>>().size()>{});
        }

        template<size_t Index, Class T>
        constexpr auto invoke_getter(const T& value) -> decltype(auto)
        {
            constexpr auto getters{ detail::getter_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < getters.size());
            return (value.[:getters[Index]:]());
        }

        template<size_t Index, Class T>
        constexpr auto get(T& value) -> decltype(auto)
        {
            constexpr auto members{ detail::field_reflections<std::remove_cvref_t<T>>() };
            static_assert(Index < members.size());
            return (std::forward<T>(value).[:members[Index]:]);
        }

        template<Class T>
        struct Info
        {
            using Type = std::remove_cvref_t<T>;
            static constexpr std::string_view NAME{ type::name<T>() };
            using MemberTypes = field_types_t<T>;
            static constexpr std::array MEMBER_NAMES{ field_names<T>() };
            static consteval auto numMembers() -> size_t { return MEMBER_NAMES.size(); }
            using NestedTypes = nested_types_t<T>;
            static constexpr std::array NESTED_TYPE_NAMES{ nested_type_names<T>() };
            static consteval auto numNestedTypes() -> size_t { return NESTED_TYPE_NAMES.size(); }
            using MethodTypes = method_types_t<T>;
            static constexpr std::array METHOD_NAMES{ method_names<T>() };
            static consteval auto numMethods() -> size_t { return METHOD_NAMES.size(); }
        };

        template<Class T, pnm::meta::string::FixedString Identifier>
        constexpr auto dispatch(auto value)
        {
            constexpr auto target_method{ []() consteval {
                constexpr auto class_meta{ ^^T };
                const auto members{ std::meta::members_of(class_meta, std::meta::access_context::current()) };

                for (const auto member : members) {
                    if (std::meta::is_function(member) && std::meta::is_static_member(member) &&
                        std::meta::identifier_of(member) == static_cast<std::string_view>(Identifier)) {
                        return member;
                    }
                }
                throw std::logic_error("Method not found");
            }() };
            return [:target_method:](value);
        }
    }

    namespace source
    {
        namespace detail
        {
            struct EmbeddedSource
            {
                std::string_view file_name;
                std::string_view source_code;
            };

            static_assert(std::is_trivially_copyable_v<EmbeddedSource>);
            static_assert(std::is_trivially_destructible_v<EmbeddedSource>);

            // NOLINTBEGIN(bugprone-reserved-identifier,readability-identifier-naming)
            extern "C" {
            extern const EmbeddedSource __start_pnm_embedded_sources[] __attribute__((weak));
            extern const EmbeddedSource __stop_pnm_embedded_sources[] __attribute__((weak));
            }
            // NOLINTEND(bugprone-reserved-identifier,readability-identifier-naming)

            static auto find_embedded_source(std::string_view file_name) noexcept
              -> std::optional<EmbeddedSource>
            {
                if (std::addressof(__start_pnm_embedded_sources) == nullptr ||
                    std::addressof(__stop_pnm_embedded_sources) == nullptr) {
                    return std::nullopt;
                }

                std::span sources{ &__start_pnm_embedded_sources[0], &__stop_pnm_embedded_sources[0] };
                auto it{ std::ranges::find(sources, file_name, &EmbeddedSource::file_name) };

                if (it != sources.end()) {
                    return *it;
                }
                return std::nullopt;
            }

            class Registry
            {
              public:
                using RegistryMap = std::flat_map<std::string_view, EmbeddedSource, std::less<>>;

                static auto instance() -> Registry&
                {
                    static Registry registry;
                    return registry;
                }

                auto embed(EmbeddedSource source) -> bool
                {
                    std::scoped_lock lock(m_mutex);
                    auto [_, inserted] = m_entries.emplace(source.file_name, source);
                    return inserted;
                }

                auto embed(std::string file_name, std::string source_code) -> bool
                {
                    std::scoped_lock lock(m_mutex);

                    if (m_entries.contains(std::string_view(file_name))) {
                        return false;
                    }

                    auto& stored_name = m_strings.emplace_back(std::move(file_name));
                    auto& stored_code = m_strings.emplace_back(std::move(source_code));

                    auto [_, inserted] = m_entries.emplace(
                      stored_name, EmbeddedSource{ .file_name = stored_name, .source_code = stored_code });
                    return inserted;
                }

                auto operator[](std::string_view file_name) const -> std::optional<EmbeddedSource>
                {
                    if (auto embedded{ find_embedded_source(file_name) }) {
                        return embedded;
                    }

                    std::scoped_lock lock(m_mutex);
                    if (auto it{ m_entries.find(file_name) }; it != m_entries.end()) {
                        return it->second;
                    }
                    return std::nullopt;
                }

              private:
                Registry() = default;

                mutable std::mutex m_mutex;
                RegistryMap m_entries;
                std::deque<std::string> m_strings;
            };

            static auto read_file(const std::filesystem::path& file_path) -> Result<std::string>
            {
                std::ifstream stream(file_path, std::ios::in | std::ios::binary);
                if (!stream) {
                    return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
                }

                std::ostringstream buffer;
                buffer << stream.rdbuf();
                return buffer.str();
            }

            static auto load_source(std::string_view source_path) -> Result<std::string_view>
            {
                auto& registry{ Registry::instance() };

                if (auto cached{ registry[source_path] }) {
                    return cached->source_code;
                }

                if (auto embedded{ find_embedded_source(source_path) }) {
                    registry.embed(*embedded);
                    if (auto cached{ registry[source_path] }) {
                        return cached->source_code;
                    }
                }

                if (std::filesystem::exists(source_path) && std::filesystem::is_regular_file(source_path)) {
                    auto file_content{ read_file(source_path) };
                    if (!file_content) {
                        return std::unexpected(file_content.error());
                    }
                    registry.embed(std::string(source_path), std::move(*file_content));
                    if (auto cached{ registry[source_path] }) {
                        return cached->source_code;
                    }
                }

                return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
            }

            static consteval auto current_source_code()
            {
#line 1 __BASE_FILE__
                return string::FixedString{ std::to_array<char>({
#embed __BASE_FILE__ // NOLINT(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
                  , 0U }) };
            }
        }

        static auto excerpt(std::string_view file_name, uint32_t line, size_t context_size = 0)
          -> Result<std::string>
        {
            auto source_code{ detail::load_source(file_name) };
            if (!source_code) {
                return std::unexpected(source_code.error());
            }

            auto lines{ *source_code | std::views::split('\n') | std::views::transform([](auto&& s) {
                std::string_view sv(s);
                if (sv.ends_with('\r')) {
                    sv.remove_suffix(1);
                }
                return sv;
            }) | std::ranges::to<std::vector<std::string_view>>() };

            if (lines.empty()) {
                return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
            }

            if (line > lines.size() && line != 0) {
                return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            }

            auto start_line{ 1U };
            auto end_line{ std::min<uint32_t>(lines.size(), context_size * 2 + 1) };

            if (line > 0 && line <= lines.size()) {
                start_line = line > context_size ? line - context_size : 1;
                end_line = std::min<uint32_t>(lines.size(), line + context_size);
            }

            auto line_number_width{ static_cast<int>(std::to_string(end_line).size()) };
            std::ostringstream snippet;

            for (auto current_line{ start_line }; current_line <= end_line; ++current_line) {
                const bool is_target_line = line == current_line;
                snippet << (is_target_line ? ">" : " ") << " " << std::setw(line_number_width) << current_line
                        << " | " << lines[current_line - 1] << '\n';
            }

            return snippet.str();
        }
    }
}

// clang-format off
#define PNM_META_SOURCE_EMBED_CURRENT                                                               \
    namespace                                                                                       \
    {                                                                                               \
        static constexpr auto PNM_META_EMBEDDED_SOURCE_CODE{                                        \
            pnm::meta::source::detail::current_source_code()                                              \
        };                                                                                          \
        [[maybe_unused, gnu::used, gnu::retain, gnu::section("pnm_embedded_sources")]]              \
        static constinit const pnm::meta::source::detail::EmbeddedSource PNM_META_EMBEDDED_SOURCE{  \
            .file_name = __BASE_FILE__,                                                             \
            .source_code = PNM_META_EMBEDDED_SOURCE_CODE                                            \
        };                                                                                          \
    }

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PNM_META_SOURCE_EMBED_BEGIN(file_name)                                                      \
    namespace                                                                                       \
    {                                                                                               \
        static constexpr auto FILE_NAME{ file_name };                                               \
        static constexpr auto PNM_META_EMBEDDED_SOURCE_DATA{ std::to_array<char>({

#define PNM_META_SOURCE_EMBED_END                                                                   \
            , '\0' }) };                                                                            \
        [[maybe_unused, gnu::used, gnu::retain, gnu::section("pnm_embedded_sources")]]              \
        static constinit const pnm::meta::source::detail::EmbeddedSource PNM_META_EMBEDDED_SOURCE{  \
            .file_name = FILE_NAME,                                                                 \
            .source_code = std::string_view(PNM_META_EMBEDDED_SOURCE_DATA.data(),                   \
            PNM_META_EMBEDDED_SOURCE_DATA.size() - 1)                                               \
        };                                                                                          \
    }
// clang-format on

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
