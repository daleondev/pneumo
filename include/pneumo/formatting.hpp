#pragma once

#include "meta.hpp"

#ifdef PFMT_ENABLE_JSON
#include <glaze/json.hpp>
#endif
#ifdef PFMT_ENABLE_YAML
#include <glaze/yaml.hpp>
#endif
#ifdef PFMT_ENABLE_TOML
#include <glaze/toml.hpp>
#endif

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <sstream>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

namespace pnm::fmt
{
#ifdef PFMT_ENABLE_JSON
    static constexpr bool IS_JSON_ENABLED{ true };
#else
    static constexpr bool IS_JSON_ENABLED{ false };
#endif
#ifdef PFMT_ENABLE_YAML
    static constexpr bool IS_YAML_ENABLED{ true };
#else
    static constexpr bool IS_YAML_ENABLED{ false };
#endif
#ifdef PFMT_ENABLE_TOML
    static constexpr bool IS_TOML_ENABLED{ true };
#else
    static constexpr bool IS_TOML_ENABLED{ false };
#endif

    namespace detail
    {
        using namespace std::literals;

        // ---------- Constexpr data types ----------

        template<typename T, size_t Capacity>
        struct FixedVector
        {
            std::array<T, Capacity> data{};
            size_t size{ 0UZ };

            constexpr FixedVector() = default;

            template<size_t Size>
            // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
            constexpr FixedVector(std::array<T, Size>&& arr)
              : size{ Size }
            {
                static_assert(Size <= Capacity, "Input array exceeds Vector capacity!");
                std::ranges::move(arr | std::views::take(size), data.begin());
            }

            constexpr auto add(this auto& self, T val) -> void
            {
                if (self.size >= Capacity) {
                    throw std::out_of_range("Vector is full!");
                }
                self.data[self.size++] = std::move(val);
            }

            constexpr auto at(this const auto& self, size_t index) -> const T&
            {
                if (index >= self.size) {
                    throw std::out_of_range("Index out of range!");
                }
                return self.data[index];
            }

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
                auto offset{ self.size };
                return std::forward<Self>(self).data.begin() + offset;
            }
        };

        template<typename Key, typename Value, size_t Capacity>
        struct FixedMap
        {
            std::array<std::pair<Key, Value>, Capacity> data{};
            size_t size{ 0UZ };

            constexpr FixedMap() = default;

            // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
            constexpr FixedMap(std::array<std::pair<Key, Value>, Capacity>&& arr)
              : size{ Capacity }
            {
                std::ranges::move(arr, data.begin());
            }

            constexpr void insert(this auto& self, std::pair<Key, Value> new_element)
            {
                auto it{ std::ranges::find_if(self, [&new_element](const auto& element) -> bool {
                    return element.first == new_element.first;
                }) };

                if (it != self.end()) {
                    it->second = std::move(new_element.second);
                    return;
                }

                if (self.size >= Capacity) {
                    throw std::out_of_range("Map is full!");
                }

                self.data[self.size++] = std::move(new_element);
            }

            constexpr auto at(this const auto& self, const Key& key) -> std::optional<Value>
            {
                auto it{ std::ranges::find_if(
                  self, [&key](const auto& pair) -> bool { return pair.first == key; }) };

                if (it != self.end()) {
                    return it->second;
                }

                return std::nullopt;
            }

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
                auto offset{ self.size };
                return std::forward<Self>(self).data.begin() + offset;
            }
        };
        template<typename Key, typename Value, size_t Size>
        FixedMap(std::array<std::pair<Key, Value>, Size>&& arr) -> FixedMap<Key, Value, Size>;

        // ---------- Helpers ----------

        template<typename First, typename Second, size_t N>
        consteval auto is_array_of_pairs_unique(std::array<std::pair<First, Second>, N> arr) -> bool
        {
            if constexpr (std::same_as<First, Second>) {
                std::ranges::for_each(
                  arr | std::views::filter([](const auto& val) -> bool { return val.second < val.first; }),
                  [](auto& val) -> void { std::swap(val.first, val.second); });
            }
            std::ranges::sort(arr);
            return std::ranges::adjacent_find(arr) == arr.end();
        }

        // ---------- General Concepts ----------

        template<typename T>
        concept VoidPtr = std::is_pointer_v<std::remove_cvref_t<T>> &&
                          std::is_void_v<std::remove_pointer_t<std::remove_cvref_t<T>>>;

        template<typename T>
        concept CharPtr =
          std::is_pointer_v<std::remove_cvref_t<T>> &&
          std::is_same_v<std::remove_cvref_t<std::remove_pointer_t<std::remove_cvref_t<T>>>, char>;

        template<typename T>
        concept ValuePtr = std::is_pointer_v<std::remove_cvref_t<T>> && !CharPtr<T> && !VoidPtr<T> &&
                           std::formattable<typename std::remove_pointer_t<std::remove_cvref_t<T>>, char>;

        template<typename T>
        concept SmartPtr = requires(std::remove_cvref_t<T> p) {
            typename std::remove_cvref_t<T>::element_type;
            { p.get() } -> std::convertible_to<const void*>;
            requires !std::is_aggregate_v<std::remove_cvref_t<T>>;
            requires std::formattable<typename std::remove_cvref_t<T>::element_type, char>;
        };

        template<typename T>
        concept Streamable =
          std::is_class_v<std::remove_cvref_t<T>> &&
          requires(std::ostream& os, const T& t) {
              { os << t } -> std::convertible_to<std::ostream&>;
          } && !pnm::meta::type::StdType<std::remove_cvref_t<T>> &&
          !std::is_pointer_v<std::remove_cvref_t<T>> && !std::is_fundamental_v<std::remove_cvref_t<T>> &&
          !std::is_array_v<std::remove_cvref_t<T>> && !SmartPtr<T>;

        template<typename T>
        concept HasToString = std::is_class_v<std::remove_cvref_t<T>> && requires(const T& t) {
            requires(requires {
                { t.to_string() } -> std::convertible_to<std::string_view>;
            } || requires {
                { t.toString() } -> std::convertible_to<std::string_view>;
            } || requires {
                { to_string(t) } -> std::convertible_to<std::string_view>;
            } || requires {
                { toString(t) } -> std::convertible_to<std::string_view>;
            });
        } && !pnm::meta::type::StdType<std::remove_cvref_t<T>>;

        template<typename T>
        struct is_array : std::false_type // NOLINT(readability-identifier-naming)
        {
        };

        template<typename T, size_t N>
        struct is_array<std::array<T, N>> : std::true_type // NOLINT(readability-identifier-naming)
        {
        };

        template<typename A, typename T>
        concept ArrayOf = is_array<std::remove_cvref_t<A>>::value &&
                          std::convertible_to<typename std::remove_cvref_t<A>::value_type, T>;

        // ---------- Adapter ----------

        template<typename T>
        struct get_class_type;

        template<typename MemberType, typename ClassType>
        struct get_class_type<MemberType ClassType::*>
        {
            using type = ClassType;
        };

        template<typename T>
        using get_class_type_t = typename get_class_type<T>::type;

        template<typename T>
        struct remove_member_pointer;

        template<typename Data, typename Class>
        struct remove_member_pointer<Data Class::*>
        {
            using type = Data;
        };

        template<typename T>
        using remove_member_pointer_t = typename remove_member_pointer<T>::type;
    }

    template<typename T>
    struct Adapter
    {
        using Fields = std::tuple<>;
    };

    template<pnm::meta::string::FixedString Name, auto Value>
        requires std::is_member_pointer_v<decltype(Value)>
    struct Field
    {
        using Type = std::conditional_t<
          std::is_member_object_pointer_v<decltype(Value)>,
          detail::remove_member_pointer_t<decltype(Value)>,
          std::invoke_result_t<decltype(Value), detail::get_class_type_t<decltype(Value)>&>>;
        static constexpr std::string_view NAME = Name;
        static constexpr auto VALUE = Value;
    };

    namespace detail
    {
        template<typename T>
        concept HasAdapter =
          std::is_class_v<std::remove_cvref_t<T>> &&
          (pnm::meta::tuple::count<typename Adapter<std::remove_cvref_t<T>>::Fields>() > 0);

        template<HasAdapter T>
        consteval auto adapter_names()
        {
            using Type = std::remove_cvref_t<T>;
            return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::array<std::string_view, sizeof...(Is)>{
                    pnm::meta::tuple::at_t<Is, typename Adapter<Type>::Fields>::NAME...
                };
            }(std::make_index_sequence<pnm::meta::tuple::count<typename Adapter<Type>::Fields>()>{});
        }

        template<HasAdapter T>
        consteval auto adapter_types()
        {
            using Type = std::remove_cvref_t<T>;
            return []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::type_identity<
                  std::tuple<typename pnm::meta::tuple::at_t<Is, typename Adapter<Type>::Fields>::Type...>>{};
            }(std::make_index_sequence<pnm::meta::tuple::count<typename Adapter<Type>::Fields>()>{});
        }

        template<HasAdapter T>
        using adapter_types_t = typename decltype(adapter_types<T>())::type;

        template<HasAdapter T>
        struct AdapterInfo
        {
            using Type = std::remove_cvref_t<T>;
            static constexpr std::string_view NAME{ pnm::meta::type::name<T>() };
            using MemberTypes = adapter_types_t<T>;
            static constexpr std::array MEMBER_NAMES{ adapter_names<T>() };
            static consteval auto numMembers() -> size_t { return MEMBER_NAMES.size(); };
        };

        // ---------- Reflectable ----------

        template<typename T>
        concept Reflectable =
          std::is_class_v<std::remove_cvref_t<T>> && std::is_aggregate_v<std::remove_cvref_t<T>> &&
          !pnm::meta::type::StdType<std::remove_cvref_t<T>> && !HasAdapter<T> && !Streamable<T> &&
          !HasToString<T> && (pnm::meta::structural::field_count<std::remove_cvref_t<T>>() > 0);

        template<Reflectable T>
        using ReflectableInfo = pnm::meta::structural::Info<T>;

        // ---------- Formatting ----------

        template<typename T>
        concept FormatInfo = requires {
            typename T::Type;
            { T::NAME } -> std::convertible_to<std::string_view>;
            typename T::MemberTypes;
            { T::MEMBER_NAMES } -> ArrayOf<std::string_view>;
            { T::numMembers() } -> std::convertible_to<size_t>;
        };

        template<FormatInfo Info>
        consteval auto class_format_size() -> size_t
        {
            auto size{ 0UZ };
            size += std::size("[ "sv);
            size += Info::NAME.size();
            size += std::size(": {{ "sv);

            for (auto i{ 0UZ }; i < Info::numMembers(); ++i) {
                size += Info::MEMBER_NAMES[i].size();
                size += std::size(": {}"sv);
                if (i < Info::numMembers() - 1) {
                    size += std::size(", "sv);
                }
            }

            size += std::size(" }} ]"sv);
            return size;
        }

        template<FormatInfo Info>
        consteval auto class_format()
        {
            std::array<char, class_format_size<Info>()> fmt{};

            auto iter{ fmt.begin() };
            auto append = [&](std::string_view s) -> void {
                for (char c : s) {
                    *iter++ = c;
                }
            };

            append("[ ");
            append(Info::NAME);
            append(": {{ ");

            for (auto i{ 0UZ }; i < Info::numMembers(); ++i) {
                append(Info::MEMBER_NAMES[i]);
                append(": {}");
                if (i < Info::numMembers() - 1) {
                    append(", ");
                }
            }

            append(" }} ]");
            return pnm::meta::string::FixedString(std::move(fmt));
        }

        static constexpr std::string_view PRETTY_INDENT{ "  " };

        template<FormatInfo Info, size_t Level = 0>
        consteval auto class_pretty_format_size() -> size_t
        {
            auto size{ 0UZ };
            if constexpr (Level == 0) {
                size += Info::NAME.size();
                size += std::size(": {{\n"sv);
            }
            else {
                size += std::size("{{\n"sv);
            }

            [&]<size_t... Is>(std::index_sequence<Is...>) -> void {
                ([&](auto i) -> void {
                    using MemberType = pnm::meta::tuple::at_t<i, typename Info::MemberTypes>;

                    size += (Level + 1) * PRETTY_INDENT.size();
                    size += Info::MEMBER_NAMES[i].size();
                    if constexpr (HasAdapter<MemberType>) {
                        size += std::size(": "sv);
                        size += class_pretty_format_size<AdapterInfo<MemberType>, Level + 1>();
                    }
                    else if constexpr (Reflectable<MemberType>) {
                        size += std::size(": "sv);
                        size += class_pretty_format_size<ReflectableInfo<MemberType>, Level + 1>();
                    }
                    else {
                        size += std::size(": {}"sv);
                    }
                    if constexpr (i < Info::numMembers() - 1) {
                        size += std::size(","sv);
                    }
                    size += std::size("\n"sv);
                }(std::integral_constant<size_t, Is>{}), ...);
            }(std::make_index_sequence<Info::numMembers()>{});

            size += Level * PRETTY_INDENT.size();
            size += std::size("}}"sv);
            return size;
        }

        template<FormatInfo Info, size_t Level = 0>
        consteval auto class_pretty_format()
        {
            std::array<char, class_pretty_format_size<Info, Level>()> fmt{};

            auto iter{ fmt.begin() };
            auto append = [&](std::string_view s) -> void {
                for (char c : s) {
                    *iter++ = c;
                }
            };

            if constexpr (Level == 0) {
                append(Info::NAME);
                append(": {{\n");
            }
            else {
                append("{{\n");
            }

            [&]<size_t... Is>(std::index_sequence<Is...>) -> void {
                ([&](auto i) -> void {
                    using MemberType = pnm::meta::tuple::at_t<i, typename Info::MemberTypes>;

                    for (auto j{ 0UZ }; j < (Level + 1); ++j) {
                        append(PRETTY_INDENT);
                    }
                    append(Info::MEMBER_NAMES[i]);

                    if constexpr (HasAdapter<MemberType>) {
                        append(": ");
                        append(class_pretty_format<AdapterInfo<MemberType>, Level + 1>());
                    }
                    else if constexpr (Reflectable<MemberType>) {
                        append(": ");
                        append(class_pretty_format<ReflectableInfo<MemberType>, Level + 1>());
                    }
                    else {
                        append(": {}");
                    }

                    if constexpr (i < Info::numMembers() - 1) {
                        append(",");
                    }
                    append("\n");
                }(std::integral_constant<size_t, Is>{}), ...);
            }(std::make_index_sequence<Info::numMembers()>{});

            for (auto i{ 0UZ }; i < Level; ++i) {
                append(PRETTY_INDENT);
            }
            append("}}");
            return pnm::meta::string::FixedString(std::move(fmt));
        }

        // ---------- Format arguments ----------

        template<typename T>
        constexpr auto check_arg(T&& field) -> decltype(auto)
        {
            if constexpr (std::formattable<T, char>) {
                return std::forward<T>(field);
            }
            else {
                return "-";
            }
        };

        template<typename... Args>
        constexpr auto make_args_tuple(Args&&... args)
        {
            using Tuple = std::tuple<
              std::conditional_t<std::is_lvalue_reference_v<Args>, Args, std::remove_reference_t<Args>>...>;
            return Tuple(std::forward<Args>(args)...);
        };

        template<typename T>
        constexpr auto make_flat_args_tuple(T&& val)
        {
            using Type = std::remove_cvref_t<T>;
            if constexpr (detail::HasAdapter<Type>) {
                using Fields = typename Adapter<Type>::Fields;
                return [&]<size_t... Is>(std::index_sequence<Is...>) -> auto {
                    return std::tuple_cat(
                      make_flat_args_tuple(std::invoke(pnm::meta::tuple::at_t<Is, Fields>::VALUE, val))...);
                }(std::make_index_sequence<pnm::meta::tuple::count<Fields>()>{});
            }
            else if constexpr (detail::Reflectable<Type>) {
                return [&]<size_t... Is>(std::index_sequence<Is...>) -> auto {
                    return std::tuple_cat(make_flat_args_tuple(pnm::meta::structural::get<Is>(val))...);
                }(std::make_index_sequence<pnm::meta::structural::field_count<Type>()>{});
            }
            else {
                return detail::make_args_tuple(detail::check_arg(std::forward<T>(val)));
            }
        };

        // ---------- Format Specs ----------

        enum class FmtSpecs : char
        {
            Verbose = 'v',
            Pretty = 'p',
            Json = 'j',
            Yaml = 'y',
            Toml = 't'
        };
        static constexpr auto NUM_FMT_SPECS{ pnm::meta::enumeration::count<FmtSpecs>() };

        static constexpr std::array COMPATIBLE_FMT_SPEC_PAIRS{
            std::make_pair(FmtSpecs::Verbose, FmtSpecs::Pretty),
            std::make_pair(FmtSpecs::Pretty, FmtSpecs::Json)
        };

        static_assert(is_array_of_pairs_unique(COMPATIBLE_FMT_SPEC_PAIRS),
                      "Compatible format specifier pairs not unique");

        consteval auto generate_incompatible_specs()
        {
            FixedMap<FmtSpecs, FixedVector<FmtSpecs, NUM_FMT_SPECS - 1>, NUM_FMT_SPECS> incompatible_specs;

            constexpr auto specs{ pnm::meta::enumeration::enumerators<FmtSpecs>() };
            for (auto spec : specs) {
                FixedVector<FmtSpecs, NUM_FMT_SPECS - 1> incompatible{};

                auto incompatible_view{ specs | std::views::filter([spec](auto other) -> bool {
                    if (spec == other) {
                        return false;
                    }
                    return std::ranges::none_of(COMPATIBLE_FMT_SPEC_PAIRS, [&](const auto& pair) -> bool {
                        return (pair.first == spec && pair.second == other) ||
                               (pair.first == other && pair.second == spec);
                    });
                }) };

                std::ranges::for_each(incompatible_view, [&](auto val) -> void { incompatible.add(val); });

                incompatible_specs.insert(std::make_pair(spec, incompatible));
            }

            return incompatible_specs;
        }

        static constexpr auto FMT_INCOMPATIBEL_SPECS{ generate_incompatible_specs() };

        // ---------- Glaze Support ----------

        static constexpr std::array GLAZE_FMT_SPECS{ FmtSpecs::Json, FmtSpecs::Yaml, FmtSpecs::Toml };

#ifdef PFMT_ENABLE_GLAZE
        enum class GlazeFormat : uint32_t // NOLINT(performance-enum-size)
        {
            Json = glz::JSON,
            Yaml = glz::YAML,
            Toml = glz::TOML
        };

        template<typename T, GlazeFormat Fmt>
        consteval auto is_type_glaze_serializable() -> bool;

        template<FormatInfo Info, GlazeFormat Fmt>
        consteval auto is_info_glaze_serializable() -> bool
        {
            using MemberTypes = typename Info::MemberTypes;
            return []<size_t... Is>(std::index_sequence<Is...>) -> bool {
                return (is_type_glaze_serializable<std::tuple_element_t<Is, MemberTypes>, Fmt>() && ...);
            }(std::make_index_sequence<Info::numMembers()>{});
        }

        template<typename T, GlazeFormat Fmt>
        consteval auto is_type_glaze_serializable() -> bool
        {
            if constexpr (HasAdapter<T>) {
                return is_info_glaze_serializable<AdapterInfo<T>, Fmt>();
            }
            else if constexpr (Reflectable<T>) {
                return is_info_glaze_serializable<ReflectableInfo<T>, Fmt>();
            }
            else {
                return glz::write_supported<T, std::to_underlying(Fmt)>;
            }
        }

        template<typename T, GlazeFormat Fmt>
        concept GlazeSerializable = is_type_glaze_serializable<T, Fmt>();

        template<typename Field>
        consteval auto glaze_field_value() -> decltype(auto)
        {
            if constexpr (std::is_member_function_pointer_v<decltype(Field::VALUE)>) {
                return glz::custom<nullptr, Field::VALUE>;
            }
            else {
                return Field::VALUE;
            }
        }

        template<typename T>
        struct GlazeAdapter
        {
            using Fields = typename Adapter<T>::Fields;
            // NOLINTNEXTLINE(readability-identifier-naming)
            static constexpr auto value = []<size_t... Is>(std::index_sequence<Is...>) -> auto {
                return std::apply(
                  [](auto&&... args) -> auto { return glz::object(std::forward<decltype(args)>(args)...); },
                  std::tuple_cat(std::make_tuple(std::tuple_element_t<Is, Fields>::NAME,
                                                 glaze_field_value<std::tuple_element_t<Is, Fields>>())...));
            }(std::make_index_sequence<std::tuple_size_v<Fields>>{});
        };

        template<typename T>
        concept HasGlazeMeta = requires { glz::meta<std::remove_cvref_t<T>>::value; };
#endif

        // ---------- Format Opts ----------

        struct FmtOpts
        {
            bool verbose;
            bool pretty;
            bool json;
            bool yaml;
            bool toml;

            constexpr auto operator==(const FmtOpts&) const -> bool = default;
            constexpr operator bool(this const auto& self) { return self != FmtOpts{}; }
        };

        // clang-format off
        static constexpr FixedMap FMT_SPECS_TO_OPTS{std::array{ 
            std::make_pair(FmtSpecs::Verbose,   &FmtOpts::verbose),
            std::make_pair(FmtSpecs::Pretty,    &FmtOpts::pretty),
            std::make_pair(FmtSpecs::Json,      &FmtOpts::json),
            std::make_pair(FmtSpecs::Yaml,      &FmtOpts::yaml),
            std::make_pair(FmtSpecs::Toml,      &FmtOpts::toml) 
        }};
        // clang-format on

        template<FmtOpts AllowedOpts, typename Ctx>
        constexpr auto parse_fmt_opts(Ctx& ctx, FmtOpts& active_opts) -> Ctx::iterator
        {
            auto it{ ctx.begin() };

            std::vector<FmtSpecs> incompatibel_specs{};
            while (it != ctx.end()) {
                auto spec_char{ *it };
                if (spec_char == '}') {
                    return it;
                }

                auto spec{ static_cast<FmtSpecs>(spec_char) };
                if (std::ranges::contains(incompatibel_specs, spec)) {
                    throw std::format_error("Invalid format specifier");
                }

                auto opt{ FMT_SPECS_TO_OPTS.at(spec) };
                if (!opt.has_value() || !(AllowedOpts.*opt.value())) {
                    throw std::format_error("Invalid format specifier");
                }
                active_opts.*opt.value() = true;

                if (auto inc_specs{ FMT_INCOMPATIBEL_SPECS.at(spec) }; inc_specs.has_value()) {
                    incompatibel_specs.insert(incompatibel_specs.end(), inc_specs->begin(), inc_specs->end());
                }

                ++it;
            }

            return it;
        }

        template<FormatInfo Info, typename Ctx, typename T>
        auto handle_class_opts(Ctx& ctx, const T& t, const FmtOpts& fmt_opts)
          -> std::optional<typename Ctx::iterator>
        {
#ifdef PFMT_ENABLE_JSON
            if (fmt_opts.json) {
                if constexpr (GlazeSerializable<T, GlazeFormat::Json>) {

                    auto json_str{ (fmt_opts.pretty ? glz::write<glz::opts{ .prettify = true }>(t)
                                                    : glz::write_json(t))
                                     .value_or("JSON Error") };
                    return std::format_to(ctx.out(), "{}", json_str);
                }
                else {
                    throw std::format_error("Failed to format json");
                }
            }
#endif
#ifdef PFMT_ENABLE_YAML
            if (fmt_opts.yaml) {
                if constexpr (GlazeSerializable<T, GlazeFormat::Yaml> && HasGlazeMeta<T>) {

                    auto yaml_str{ glz::write_yaml(t).value_or("YAML Error") };
                    return std::format_to(ctx.out(), "{}", yaml_str);
                }
                else {
                    throw std::format_error("Failed to format yaml");
                }
            }
#endif
#ifdef PFMT_ENABLE_TOML
            if (fmt_opts.toml) {
                if constexpr (GlazeSerializable<T, GlazeFormat::Toml>) {

                    auto toml_str{ glz::write_toml(t).value_or("TOML Error") };
                    return std::format_to(ctx.out(), "{}", toml_str);
                }
                else {
                    throw std::format_error("Failed to format toml");
                }
            }
#endif
            if (fmt_opts.pretty) {
                auto args_tuple{ fmt::detail::make_flat_args_tuple(t) };
                static constexpr auto fmt{ fmt::detail::class_pretty_format<Info>() };
                return std::apply([&ctx](const auto&... args) -> Ctx::iterator {
                    return std::format_to(ctx.out(), fmt, args...);
                }, args_tuple);
            }

            return std::nullopt;
        }
    }
}

#ifdef PFMT_ENABLE_GLAZE
template<pnm::fmt::detail::HasAdapter T>
struct glz::meta<T> : pnm::fmt::detail::GlazeAdapter<T>
{
};
#endif

template<pnm::fmt::detail::HasAdapter T>
struct std::formatter<T>
{
    using Info = pnm::fmt::detail::AdapterInfo<T>;

    // clang-format off
    static constexpr pnm::fmt::detail::FmtOpts ALLOWED_FMT_OPTS{
        .verbose = true,
        .pretty = true,
        .json = pnm::fmt::IS_JSON_ENABLED,
        .yaml = pnm::fmt::IS_YAML_ENABLED,
        .toml = pnm::fmt::IS_TOML_ENABLED
    };
    // clang-format on

    pnm::fmt::detail::FmtOpts fmt_opts{};

    template<typename Ctx>
    constexpr auto parse(Ctx& ctx) -> Ctx::iterator
    {
        auto it{ pnm::fmt::detail::parse_fmt_opts<ALLOWED_FMT_OPTS>(ctx, fmt_opts) };
#ifdef PFMT_ENABLE_GLAZE
        if (fmt_opts.json && !pnm::fmt::detail::GlazeSerializable<T, pnm::fmt::detail::GlazeFormat::Json>) {
            throw std::format_error("Formatting not possible: Json");
        }
        if (fmt_opts.yaml && (!pnm::fmt::detail::GlazeSerializable<T, pnm::fmt::detail::GlazeFormat::Yaml> ||
                              !pnm::fmt::detail::HasGlazeMeta<T>)) {
            throw std::format_error("Formatting not possible: Yaml");
        }
        if (fmt_opts.toml && !pnm::fmt::detail::GlazeSerializable<T, pnm::fmt::detail::GlazeFormat::Toml>) {
            throw std::format_error("Formatting not possible: Toml");
        }
#endif
        return it;
    }

    template<typename Ctx>
    auto format(const T& t, Ctx& ctx) const -> Ctx::iterator
    {
        if (fmt_opts) {
            if (auto it{ pnm::fmt::detail::handle_class_opts<Info>(ctx, t, fmt_opts) }; it.has_value()) {
                return it.value();
            }
        }

        auto args_tuple{ [&]<size_t... Is>(std::index_sequence<Is...>) -> auto {
            using Fields = typename pnm::fmt::Adapter<std::remove_cvref_t<T>>::Fields;
            return pnm::fmt::detail::make_args_tuple(
              pnm::fmt::detail::check_arg(std::invoke(std::tuple_element_t<Is, Fields>::VALUE, t))...);
        }(std::make_index_sequence<Info::numMembers()>{}) };

        static constexpr auto fmt{ pnm::fmt::detail::class_format<Info>() };
        return std::apply([&ctx](const auto&... args) -> Ctx::iterator {
            return std::format_to(ctx.out(), fmt, args...);
        }, args_tuple);
    }
};

template<pnm::fmt::detail::Reflectable T>
struct std::formatter<T>
{
    static_assert(
      requires { T{}; },
      "Type T contains reference members or other non-value-initializable members, which are not "
      "supported "
      "for automatic formatting. Consider removing references or providing default initializers.");

    using Info = pnm::fmt::detail::ReflectableInfo<T>;

    // clang-format off
    static constexpr pnm::fmt::detail::FmtOpts ALLOWED_FMT_OPTS{ 
        .verbose = true,
        .pretty = true,
        .json = pnm::fmt::IS_JSON_ENABLED,
        .yaml = pnm::fmt::IS_YAML_ENABLED,
        .toml = pnm::fmt::IS_TOML_ENABLED
    };
    // clang-format on

    pnm::fmt::detail::FmtOpts fmt_opts{};

    template<typename Ctx>
    constexpr auto parse(Ctx& ctx) -> Ctx::iterator
    {
        auto it{ pnm::fmt::detail::parse_fmt_opts<ALLOWED_FMT_OPTS>(ctx, fmt_opts) };
#ifdef PFMT_ENABLE_GLAZE
        if (fmt_opts.json && !pnm::fmt::detail::GlazeSerializable<T, pnm::fmt::detail::GlazeFormat::Json>) {
            throw std::format_error("Formatting not possible: Json");
        }
        if (fmt_opts.yaml && (!pnm::fmt::detail::GlazeSerializable<T, pnm::fmt::detail::GlazeFormat::Yaml> ||
                              !pnm::fmt::detail::HasGlazeMeta<T>)) {
            throw std::format_error("Formatting not possible: Yaml");
        }
        if (fmt_opts.toml && !pnm::fmt::detail::GlazeSerializable<T, pnm::fmt::detail::GlazeFormat::Toml>) {
            throw std::format_error("Formatting not possible: Toml");
        }
#endif
        return it;
    }

    template<typename Ctx>
    auto format(const T& t, Ctx& ctx) const -> Ctx::iterator
    {
        if (fmt_opts) {
            if (auto it{ pnm::fmt::detail::handle_class_opts<Info>(ctx, t, fmt_opts) }; it.has_value()) {
                return it.value();
            }
        }

        auto args_tuple{ [&]<size_t... Is>(std::index_sequence<Is...>) -> auto {
            return pnm::fmt::detail::make_args_tuple(
              pnm::fmt::detail::check_arg(pnm::meta::structural::get<Is>(t))...);
        }(std::make_index_sequence<pnm::meta::structural::field_count<T>()>{}) };

        static constexpr auto fmt{ pnm::fmt::detail::class_format<Info>() };
        return std::apply([&ctx](const auto&... args) -> Ctx::iterator {
            return std::format_to(ctx.out(), fmt, args...);
        }, args_tuple);
    }
};

template<pnm::meta::enumeration::ScopedEnum T>
struct std::formatter<T>
{
    static constexpr pnm::fmt::detail::FmtOpts ALLOWED_FMT_OPTS{ .verbose = true };

    pnm::fmt::detail::FmtOpts fmt_opts{};

    template<typename Ctx>
    constexpr auto parse(Ctx& ctx) -> Ctx::iterator
    {
        return pnm::fmt::detail::parse_fmt_opts<ALLOWED_FMT_OPTS>(ctx, fmt_opts);
    }

    template<typename Ctx>
    auto format(T t, Ctx& ctx) const -> Ctx::iterator
    {
        if (fmt_opts.verbose) {
            return std::format_to(
              ctx.out(), "{}::{}", pnm::meta::type::name<T>(), pnm::meta::enumeration::enumerator_name(t));
        }
        return std::format_to(ctx.out(), "{}", pnm::meta::enumeration::enumerator_name(t));
    }
};

template<std::formattable<char> T>
struct std::formatter<std::optional<T>> : std::formatter<T>
{
    using Base = std::formatter<T>;

    template<typename Ctx>
    auto format(const std::optional<T>& t, Ctx& ctx) const -> Ctx::iterator
    {
        if (!t) {
            return std::ranges::copy("[ null ]"sv, ctx.out()).out;
        }

        ctx.advance_to(std::ranges::copy("[ "sv, ctx.out()).out);
        ctx.advance_to(Base::format(*t, ctx));
        return std::ranges::copy(" ]"sv, ctx.out()).out;
    }
};

template<pnm::fmt::detail::ValuePtr T>
struct std::formatter<T> : std::formatter<std::remove_pointer_t<std::remove_cvref_t<T>>>
{
    using Base = std::formatter<std::remove_pointer_t<std::remove_cvref_t<T>>>;

    template<typename Ctx>
    auto format(T t, Ctx& ctx) const -> Ctx::iterator
    {
        if (!t) {
            return std::format_to(ctx.out(), "[ ({}) -> {} ]", static_cast<const void*>(t), "null");
        }

        ctx.advance_to(std::format_to(ctx.out(), "[ ({}) -> ", static_cast<const void*>(t)));
        ctx.advance_to(Base::format(*t, ctx));
        return std::ranges::copy(" ]"sv, ctx.out()).out;
    }
};

template<pnm::fmt::detail::SmartPtr T>
struct std::formatter<T> : std::formatter<typename T::element_type*>
{
    template<typename Ctx>
    auto format(const T& t, Ctx& ctx) const -> Ctx::iterator
    {
        return std::formatter<typename T::element_type*>::format(t.get(), ctx);
    }
};

template<pnm::fmt::detail::Streamable T>
    requires(!pnm::fmt::detail::HasAdapter<T>)
struct std::formatter<T>
{
    template<typename Ctx>
    constexpr auto parse(Ctx& ctx) -> Ctx::iterator
    {
        pnm::fmt::detail::FmtOpts fmt_opts{};
        return pnm::fmt::detail::parse_fmt_opts<pnm::fmt::detail::FmtOpts{}>(ctx, fmt_opts);
    }

    template<typename Ctx>
    auto format(T t, Ctx& ctx) const -> Ctx::iterator
    {
        std::ostringstream oss;
        oss << t;
        return std::format_to(ctx.out(), "{}", oss.str());
    }
};

template<pnm::fmt::detail::HasToString T>
    requires(!pnm::fmt::detail::HasAdapter<T> && !pnm::fmt::detail::Streamable<T>)
struct std::formatter<T>
{
    template<typename Ctx>
    constexpr auto parse(Ctx& ctx) -> Ctx::iterator
    {
        pnm::fmt::detail::FmtOpts fmt_opts{};
        return pnm::fmt::detail::parse_fmt_opts<pnm::fmt::detail::FmtOpts{}>(ctx, fmt_opts);
    }

    template<typename Ctx>
    auto format(T t, Ctx& ctx) const -> Ctx::iterator
    {
        if constexpr (requires { std::decay_t<decltype(t)>::to_string(); }) {
            return std::format_to(ctx.out(), "{}", std::decay_t<decltype(t)>::to_string());
        }
        else if constexpr (requires { std::decay_t<decltype(t)>::toString(); }) {
            return std::format_to(ctx.out(), "{}", std::decay_t<decltype(t)>::toString());
        }
        else if constexpr (requires { t.to_string(); }) {
            return std::format_to(ctx.out(), "{}", t.to_string());
        }
        else if constexpr (requires { t.toString(); }) {
            return std::format_to(ctx.out(), "{}", t.toString());
        }
        else if constexpr (requires { to_string(t); }) {
            return std::format_to(ctx.out(), "{}", to_string(t));
        }
        else if constexpr (requires { toString(t); }) {
            return std::format_to(ctx.out(), "{}", toString(t));
        }
        throw std::format_error("Unreachable");
    }
};

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
