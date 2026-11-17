#pragma once

#include "meta.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <flat_map>
#include <numbers>
#include <optional>
#include <ratio>
#include <string_view>

// NOLINTBEGIN(bugprone-macro-parentheses,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

// Case-sensitive UCUM 2.2 expressions, verified on 2026-11-17: https://ucum.org/ucum
// The string after each identifier is its UCUM expression, independent of its display suffix.

// ---------- Unitless ----------

#define PNM_UNITLESS_UNITS(X, XX, ctx) XX(ctx, unitless, "1", "")

// ---------- Distance ----------

#define PNM_DISTANCE_UNITS(X, XX, ctx)                                                                       \
    X(ctx, nm, "nm", std::nano)                                                                              \
    X(ctx, um, "um", std::micro)                                                                             \
    X(ctx, mm, "mm", std::milli)                                                                             \
    X(ctx, cm, "cm", std::centi)                                                                             \
    X(ctx, dm, "dm", std::deci)                                                                              \
    XX(ctx, m, "m")                                                                                          \
    X(ctx, km, "km", std::kilo)

// ---------- Area ----------

#define PNM_AREA_UNITS(X, XX, ctx)                                                                           \
    X(ctx, mm2, "mm2", std::micro)                                                                           \
    X(ctx, cm2, "cm2", std::ratio<1Z, 10'000Z>)                                                              \
    X(ctx, dm2, "dm2", std::centi)                                                                           \
    XX(ctx, m2, "m2")                                                                                        \
    X(ctx, km2, "km2", std::mega)

// ---------- Time ----------

#define PNM_TIME_UNITS(X, XX, ctx)                                                                           \
    X(ctx, ns, "ns", std::nano)                                                                              \
    X(ctx, us, "us", std::micro)                                                                             \
    X(ctx, ms, "ms", std::milli)                                                                             \
    XX(ctx, s, "s")                                                                                          \
    X(ctx, min, "min", std::ratio<60Z>)                                                                      \
    X(ctx, h, "h", std::ratio<3600Z>)

// ---------- ByteSize ----------

#define PNM_BYTE_SIZE_UNITS(X, XX, ctx)                                                                      \
    XX(ctx, bytes, "By")                                                                                     \
    X(ctx, KiB, "KiBy", std::ratio<1024Z>)                                                                   \
    X(ctx, MiB, "MiBy", std::ratio<1024Z * 1024Z>)                                                           \
    X(ctx, GiB, "GiBy", std::ratio<1024Z * 1024Z * 1024Z>)                                                   \
    X(ctx, KB, "kBy", std::kilo, 0.0, "kB", false)                                                           \
    X(ctx, MB, "MBy", std::mega, 0.0, "MB", false)                                                           \
    X(ctx, GB, "GBy", std::giga, 0.0, "GB", false)

// ---------- Mass ----------

#define PNM_MASS_UNITS(X, XX, ctx)                                                                           \
    X(ctx, mg, "mg", std::milli)                                                                             \
    XX(ctx, g, "g")                                                                                          \
    X(ctx, kg, "kg", std::kilo)                                                                              \
    X(ctx, t, "t", std::mega)

// ---------- Temperature ----------

#define PNM_TEMPERATURE_UNITS(X, XX, ctx)                                                                    \
    XX(ctx, C, "Cel", "°C")                                                                                  \
    X(ctx, K, "K", detail::factor<1.0>, -273.15)                                                             \
    X(ctx, F, "[degF]", std::ratio<5Z, 9Z>, -32.0, "°F")

// ---------- Current ----------

#define PNM_CURRENT_UNITS(X, XX, ctx)                                                                        \
    X(ctx, uA, "uA", std::micro)                                                                             \
    X(ctx, mA, "mA", std::milli)                                                                             \
    XX(ctx, A, "A")                                                                                          \
    X(ctx, kA, "kA", std::kilo)

// ---------- Voltage ----------

#define PNM_VOLTAGE_UNITS(X, XX, ctx)                                                                        \
    X(ctx, mV, "mV", std::milli)                                                                             \
    XX(ctx, V, "V")                                                                                          \
    X(ctx, kV, "kV", std::kilo)                                                                              \
    X(ctx, MV, "MV", std::mega)

// ---------- Force ----------

#define PNM_FORCE_UNITS(X, XX, ctx)                                                                          \
    X(ctx, mN, "mN", std::milli)                                                                             \
    XX(ctx, N, "N")                                                                                          \
    X(ctx, kN, "kN", std::kilo)                                                                              \
    X(ctx, MN, "MN", std::mega)

// ---------- Energy ----------

#define PNM_ENERGY_UNITS(X, XX, ctx)                                                                         \
    X(ctx, mJ, "mJ", std::milli)                                                                             \
    XX(ctx, J, "J")                                                                                          \
    X(ctx, kJ, "kJ", std::kilo)                                                                              \
    X(ctx, MJ, "MJ", std::mega)                                                                              \
    X(ctx, Wh, "W.h", std::ratio<3600Z>)                                                                     \
    X(ctx, kWh, "kW.h", std::ratio<3'600'000Z>)

// ---------- Power ----------

#define PNM_POWER_UNITS(X, XX, ctx)                                                                          \
    X(ctx, mW, "mW", std::milli)                                                                             \
    XX(ctx, W, "W")                                                                                          \
    X(ctx, kW, "kW", std::kilo)                                                                              \
    X(ctx, MW, "MW", std::mega)

// ---------- Pressure ----------

#define PNM_PRESSURE_UNITS(X, XX, ctx)                                                                       \
    X(ctx, Pa, "Pa", std::ratio<1Z, 100'000Z>)                                                               \
    X(ctx, hPa, "hPa", std::ratio<100Z, 100'000Z>)                                                           \
    X(ctx, mbar, "mbar", std::ratio<1Z, 1000Z>)                                                              \
    X(ctx, kPa, "kPa", std::ratio<1000Z, 100'000Z>)                                                          \
    XX(ctx, bar, "bar")                                                                                      \
    X(ctx, atm, "atm", std::ratio<101'325Z, 100'000Z>)                                                       \
    X(ctx, MPa, "MPa", std::ratio<1'000'000Z, 100'000Z>)                                                     \
    X(ctx, kbar, "kbar", std::ratio<1000Z>)

// ---------- Frequency ----------

#define PNM_FREQUENCY_UNITS(X, XX, ctx)                                                                      \
    X(ctx, mHz, "mHz", std::milli)                                                                           \
    XX(ctx, Hz, "Hz")                                                                                        \
    X(ctx, kHz, "kHz", std::kilo)                                                                            \
    X(ctx, MHz, "MHz", std::mega)                                                                            \
    X(ctx, GHz, "GHz", std::giga)

// ---------- Data Rate ----------

#define PNM_DATA_RATE_UNITS(X, XX, ctx)                                                                      \
    XX(ctx, bytes_s, "By/s")                                                                                 \
    X(ctx, KB_s, "kBy/s", std::kilo, 0.0, "kB/s")                                                            \
    X(ctx, MB_s, "MBy/s", std::mega)                                                                         \
    X(ctx, GB_s, "GBy/s", std::giga)                                                                         \
    X(ctx, KiB_s, "KiBy/s", std::ratio<1024Z>, 0.0, "KiB/s", false)                                          \
    X(ctx, MiB_s, "MiBy/s", std::ratio<1024Z * 1024Z>, 0.0, "MiB/s", false)                                  \
    X(ctx, GiB_s, "GiBy/s", std::ratio<1024Z * 1024Z * 1024Z>, 0.0, "GiB/s", false)

// ---------- Velocity ----------

#define PNM_VELOCITY_UNITS(X, XX, ctx)                                                                       \
    X(ctx, mm_s, "mm/s", std::milli)                                                                         \
    XX(ctx, m_s, "m/s")                                                                                      \
    X(ctx, km_h, "km/h", std::ratio<1000Z, 3600Z>)

// ---------- Acceleration ----------

#define PNM_ACCELERATION_UNITS(X, XX, ctx)                                                                   \
    X(ctx, mm_s2, "mm/s2", std::milli)                                                                       \
    XX(ctx, m_s2, "m/s2", "m/s²")                                                                            \
    X(ctx, km_h2, "km/h2", std::ratio<1000Z, 3600Z * 3600Z>)

// ---------- Jerk ----------

#define PNM_JERK_UNITS(X, XX, ctx)                                                                           \
    X(ctx, mm_s3, "mm/s3", std::milli)                                                                       \
    XX(ctx, m_s3, "m/s3")

// ---------- Angle ----------

#define PNM_ANGLE_UNITS(X, XX, ctx)                                                                          \
    XX(ctx, rad, "rad")                                                                                      \
    X(ctx, deg, "deg", detail::factor<std::numbers::pi / 180.0>, 0.0, "°")                                   \
    X(ctx, rev, "circ", detail::factor<2.0 * std::numbers::pi>)

// ---------- Angular Velocity ----------

#define PNM_ANGULAR_VELOCITY_UNITS(X, XX, ctx)                                                               \
    XX(ctx, rad_s, "rad/s")                                                                                  \
    X(ctx, rpm, "circ/min", detail::factor<2.0 * std::numbers::pi / 60.0>)                                   \
    X(ctx, deg_s, "deg/s", detail::factor<std::numbers::pi / 180.0>)

// ---------- Angular Acceleration ----------

#define PNM_ANGULAR_ACCELERATION_UNITS(X, XX, ctx)                                                           \
    XX(ctx, rad_s2, "rad/s2")                                                                                \
    X(ctx, deg_s2, "deg/s2", detail::factor<std::numbers::pi / 180.0>)                                       \
    X(ctx, rpm_s, "circ/(min.s)", detail::factor<2.0 * std::numbers::pi / 60.0>, 0.0, "rpm/s")

// ---------- Angular Jerk ----------

#define PNM_ANGULAR_JERK_UNITS(X, XX, ctx)                                                                   \
    XX(ctx, rad_s3, "rad/s3")                                                                                \
    X(ctx, deg_s3, "deg/s3", detail::factor<std::numbers::pi / 180.0>)                                       \
    X(ctx, rpm_s2, "circ/(min.s2)", detail::factor<2.0 * std::numbers::pi / 60.0>, 0.0, "rpm/s²")

// ---------- Ratio ----------

#define PNM_RATIO_UNITS(X, XX, ctx)                                                                          \
    XX(ctx, fraction, "1", "")                                                                               \
    X(ctx, percent, "%", std::centi, 0.0, "%")

// ---------- Frequency derivatives and transmission lead ----------
// Frequency counts events per second; derivatives retain that event meaning.
#define PNM_FREQUENCY_RATE_UNITS(X, XX, ctx) XX(ctx, Hz_s, "Hz/s", "Hz/s")
#define PNM_FREQUENCY_ACCELERATION_UNITS(X, XX, ctx) XX(ctx, Hz_s2, "Hz/s2", "Hz/s²")
#define PNM_TRANSMISSION_LEAD_UNITS(X, XX, ctx)                                                              \
    XX(ctx, m_rad, "m/rad", "m/rad")                                                                         \
    X(ctx, mm_rev, "mm/circ", detail::factor<0.001 / (2.0 * std::numbers::pi)>, 0.0, "mm/rev")

// ---------- Quantity Registry ----------

// Time has a custom implementation and is expanded separately.
#define PNM_QUANTITIES(X)                                                                                    \
    X(Unitless, PNM_UNITLESS_UNITS)                                                                          \
    X(Distance, PNM_DISTANCE_UNITS)                                                                          \
    X(Area, PNM_AREA_UNITS)                                                                                  \
    X(ByteSize, PNM_BYTE_SIZE_UNITS)                                                                         \
    X(Mass, PNM_MASS_UNITS)                                                                                  \
    X(Temperature, PNM_TEMPERATURE_UNITS)                                                                    \
    X(Current, PNM_CURRENT_UNITS)                                                                            \
    X(Voltage, PNM_VOLTAGE_UNITS)                                                                            \
    X(Force, PNM_FORCE_UNITS)                                                                                \
    X(Energy, PNM_ENERGY_UNITS)                                                                              \
    X(Power, PNM_POWER_UNITS)                                                                                \
    X(Pressure, PNM_PRESSURE_UNITS)                                                                          \
    X(Frequency, PNM_FREQUENCY_UNITS)                                                                        \
    X(DataRate, PNM_DATA_RATE_UNITS)                                                                         \
    X(Velocity, PNM_VELOCITY_UNITS)                                                                          \
    X(Acceleration, PNM_ACCELERATION_UNITS)                                                                  \
    X(Jerk, PNM_JERK_UNITS)                                                                                  \
    X(Angle, PNM_ANGLE_UNITS)                                                                                \
    X(AngularVelocity, PNM_ANGULAR_VELOCITY_UNITS)                                                           \
    X(AngularAcceleration, PNM_ANGULAR_ACCELERATION_UNITS)                                                   \
    X(AngularJerk, PNM_ANGULAR_JERK_UNITS)                                                                   \
    X(Ratio, PNM_RATIO_UNITS)                                                                                \
    X(FrequencyRate, PNM_FREQUENCY_RATE_UNITS)                                                               \
    X(FrequencyAcceleration, PNM_FREQUENCY_ACCELERATION_UNITS)                                               \
    X(TransmissionLead, PNM_TRANSMISSION_LEAD_UNITS)

// ---------- Helper Macros ----------

#define PNM_ENUM_UNIT(_, unit_suffix, ...) unit_suffix,
#define PNM_ENUM_GROUP(_, unit_list) unit_list(PNM_ENUM_UNIT, PNM_ENUM_UNIT, _)

#define PNM_UCUM_CODE(_, unit_suffix, ucum_code, ...) std::string_view{ ucum_code },
#define PNM_UCUM_CODE_GROUP(_, unit_list) unit_list(PNM_UCUM_CODE, PNM_UCUM_CODE, _)

#define PNM_DEFINE_LITERAL(quantity_name, unit_suffix, ...)                                                  \
    namespace literals                                                                                       \
    {                                                                                                        \
        constexpr auto operator""_##unit_suffix(unsigned long long value)                                    \
        {                                                                                                    \
            return quantity_name::template create<                                                           \
              ::pnm::units::detail::units_t<quantity_name>::unit_suffix>(static_cast<double>(value));        \
        }                                                                                                    \
        constexpr auto operator""_##unit_suffix(long double value)                                           \
        {                                                                                                    \
            return quantity_name::template create<                                                           \
              ::pnm::units::detail::units_t<quantity_name>::unit_suffix>(static_cast<double>(value));        \
        }                                                                                                    \
    }

#define PNM_DEFINE_UNIT(_, unit_suffix, ...) using unit_suffix = ::pnm::units::Unit<__VA_ARGS__>;
#define PNM_DEFINE_BASE_UNIT(_, unit_suffix, ...) using unit_suffix = ::pnm::units::BaseUnit<__VA_ARGS__>;

#define PNM_DEFINE_QUANTITY(quantity_name, unit_list)                                                        \
    struct quantity_name##Units                                                                              \
    {                                                                                                        \
        unit_list(PNM_DEFINE_UNIT, PNM_DEFINE_BASE_UNIT, _)                                                  \
    };                                                                                                       \
                                                                                                             \
    class quantity_name : public ::pnm::units::QuantityBase<quantity_name, quantity_name##Units>             \
    {                                                                                                        \
      public:                                                                                                \
        using QuantityBase::create;                                                                          \
        using QuantityBase::get;                                                                             \
        using QuantityBase::QuantityBase;                                                                    \
    };                                                                                                       \
                                                                                                             \
    unit_list(PNM_DEFINE_LITERAL, PNM_DEFINE_LITERAL, quantity_name)

#define PNM_DEFINE_QUANTITY_DIVIDE_RESULT(dividend, divisor, result)                                         \
    template<>                                                                                               \
    struct detail::divide_result<detail::units_t<dividend>, detail::units_t<divisor>>                        \
    {                                                                                                        \
        using type = result;                                                                                 \
    };

#define PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(                                                        \
  dividend, dividend_unit_name, divisor, divisor_unit_name, result, result_unit_name)                        \
    template<>                                                                                               \
    struct detail::divide_result<detail::units_t<dividend>, detail::units_t<divisor>>                        \
    {                                                                                                        \
        using type = result;                                                                                 \
        using LhsUnit = dividend##Units::dividend_unit_name;                                                 \
        using RhsUnit = divisor##Units::divisor_unit_name;                                                   \
        using ResultUnit = result##Units::result_unit_name;                                                  \
    };

#define PNM_DEFINE_QUANTITY_MULTIPLY_RESULT(lhs, rhs, result)                                                \
    template<>                                                                                               \
    struct detail::multiply_result<detail::units_t<lhs>, detail::units_t<rhs>>                               \
    {                                                                                                        \
        using type = result;                                                                                 \
    };

#define PNM_DEFINE_QUANTITY_MULTIPLY_RESULT_WITH_UNITS(                                                      \
  lhs, lhs_unit_name, rhs, rhs_unit_name, result, result_unit_name)                                          \
    template<>                                                                                               \
    struct detail::multiply_result<detail::units_t<lhs>, detail::units_t<rhs>>                               \
    {                                                                                                        \
        using type = result;                                                                                 \
        using LhsUnit = lhs##Units::lhs_unit_name;                                                           \
        using RhsUnit = rhs##Units::rhs_unit_name;                                                           \
        using ResultUnit = result##Units::result_unit_name;                                                  \
    };

#define PNM_DEFINE_QUANTITY_MULTIPLY_SCALAR_RESULT_WITH_UNITS(lhs, lhs_unit_name, rhs, rhs_unit_name)        \
    template<>                                                                                               \
    struct detail::multiply_result<detail::units_t<lhs>, detail::units_t<rhs>>                               \
    {                                                                                                        \
        using type = double;                                                                                 \
        using LhsUnit = lhs##Units::lhs_unit_name;                                                           \
        using RhsUnit = rhs##Units::rhs_unit_name;                                                           \
    };

#define PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(                                                               \
  dividend, dividend_unit, divisor, divisor_unit, quotient, quotient_unit)                                   \
    PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(                                                            \
      dividend, dividend_unit, divisor, divisor_unit, quotient, quotient_unit)                               \
    PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(                                                            \
      dividend, dividend_unit, quotient, quotient_unit, divisor, divisor_unit)                               \
    PNM_DEFINE_QUANTITY_MULTIPLY_RESULT_WITH_UNITS(                                                          \
      quotient, quotient_unit, divisor, divisor_unit, dividend, dividend_unit)                               \
    PNM_DEFINE_QUANTITY_MULTIPLY_RESULT_WITH_UNITS(                                                          \
      divisor, divisor_unit, quotient, quotient_unit, dividend, dividend_unit)

#define PNM_DEFINE_QUANTITY_SQUARE_RELATION(factor, factor_unit, product, product_unit)                      \
    PNM_DEFINE_QUANTITY_MULTIPLY_RESULT_WITH_UNITS(                                                          \
      factor, factor_unit, factor, factor_unit, product, product_unit)                                       \
    PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(                                                            \
      product, product_unit, factor, factor_unit, factor, factor_unit)

#define PNM_DEFINE_QUANTITY_RECIPROCAL_RELATION(quantity, quantity_unit, reciprocal, reciprocal_unit)        \
    template<>                                                                                               \
    struct detail::reciprocal_result<detail::units_t<quantity>>                                              \
    {                                                                                                        \
        using type = reciprocal;                                                                             \
        using DenominatorUnit = quantity##Units::quantity_unit;                                              \
        using ResultUnit = reciprocal##Units::reciprocal_unit;                                               \
    };                                                                                                       \
                                                                                                             \
    template<>                                                                                               \
    struct detail::reciprocal_result<detail::units_t<reciprocal>>                                            \
    {                                                                                                        \
        using type = quantity;                                                                               \
        using DenominatorUnit = reciprocal##Units::reciprocal_unit;                                          \
        using ResultUnit = quantity##Units::quantity_unit;                                                   \
    };                                                                                                       \
                                                                                                             \
    PNM_DEFINE_QUANTITY_MULTIPLY_SCALAR_RESULT_WITH_UNITS(                                                   \
      quantity, quantity_unit, reciprocal, reciprocal_unit)                                                  \
    PNM_DEFINE_QUANTITY_MULTIPLY_SCALAR_RESULT_WITH_UNITS(                                                   \
      reciprocal, reciprocal_unit, quantity, quantity_unit)

// NOLINTEND(bugprone-macro-parentheses,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

namespace pnm::units
{
    namespace detail
    {
        // NOLINTBEGIN(readability-identifier-naming)
        template<auto Value>
            requires(std::floating_point<decltype(Value)>)
        struct factor
        {
            static constexpr auto value{ Value };
        };

        template<typename T>
        struct is_factor : std::false_type
        {
        };

        template<auto Value>
        struct is_factor<factor<Value>> : std::true_type
        {
        };

        template<typename T>
        inline constexpr bool is_factor_v{ is_factor<std::remove_cvref_t<T>>::value };

        template<typename T>
        struct is_ratio : std::false_type
        {
        };

        template<std::intmax_t Num, std::intmax_t Den>
        struct is_ratio<std::ratio<Num, Den>> : std::true_type
        {
        };

        template<typename T>
        inline constexpr bool is_ratio_v{ is_ratio<std::remove_cvref_t<T>>::value };

        template<typename T>
        concept FactorProvider = is_factor_v<T> || is_ratio_v<T>;

        template<FactorProvider T>
        consteval auto extractFactor() -> double
        {
            if constexpr (is_factor_v<T>) {
                return static_cast<double>(T::value);
            }
            else if constexpr (is_ratio_v<T>) {
                return static_cast<double>(T::num) / static_cast<double>(T::den);
            }
            else {
                static_assert(false, "Invalid factor type provided");
            }
        }
        // NOLINTEND(readability-identifier-naming)
    }

    template<meta::string::FixedString UcumCode,
             detail::FactorProvider Factor,
             double Offset = 0.0,
             meta::string::FixedString Suffix = "n/a",
             bool AutoFormat = true,
             bool IsBase = false>
    struct Unit
    {
        static constexpr auto UCUM_CODE{ static_cast<std::string_view>(UcumCode) };
        static constexpr auto FACTOR{ detail::extractFactor<Factor>() };
        static constexpr auto OFFSET{ Offset };
        static constexpr auto SUFFIX{ static_cast<std::string_view>(Suffix) };
        static constexpr auto AUTO_FORMAT{ AutoFormat };
        static constexpr auto IS_BASE{ IsBase };
        static_assert(!IS_BASE || FACTOR == 1.0);
    };

    template<meta::string::FixedString UcumCode = "", meta::string::FixedString Suffix = "n/a">
    using BaseUnit = Unit<UcumCode, detail::factor<1.0>, 0.0, Suffix, true, true>;

    namespace detail
    {
        // NOLINTBEGIN(readability-identifier-naming)
        template<typename T>
        struct is_unit : std::false_type
        {
        };

        template<meta::string::FixedString UcumCode,
                 typename Ratio,
                 double Offset,
                 meta::string::FixedString Suffix,
                 bool AutoFormat,
                 bool IsBase>
        struct is_unit<Unit<UcumCode, Ratio, Offset, Suffix, AutoFormat, IsBase>> : std::true_type
        {
        };

        // NOLINTEND(readability-identifier-naming)
    }

    template<typename T>
    concept IsUnit = detail::is_unit<std::remove_cvref_t<T>>::value;

    template<typename T>
    concept IsQuantity = requires { typename std::remove_cvref_t<T>::UnitsMeta::Type; };

    namespace detail
    {
        // NOLINTBEGIN(readability-identifier-naming)

        template<typename T>
        struct is_chrono_duration : std::false_type
        {
        };

        template<typename Rep, typename Period>
        struct is_chrono_duration<std::chrono::duration<Rep, Period>> : std::true_type
        {
        };

        template<typename T>
        concept ChronoDuration = is_chrono_duration<std::remove_cvref_t<T>>::value;

        template<IsQuantity T>
        using units_t = typename std::remove_cvref_t<T>::UnitsMeta::Type;

        template<typename LhsUnits, typename RhsUnits>
        struct multiply_result;

        template<typename LhsUnits, typename RhsUnits>
        struct divide_result;

        template<typename Units>
        struct reciprocal_result;

        template<typename LhsUnits, typename RhsUnits>
        concept HasMultiplyResult = requires { typename multiply_result<LhsUnits, RhsUnits>::type; };

        template<typename LhsUnits, typename RhsUnits>
        concept HasDivideResult = requires { typename divide_result<LhsUnits, RhsUnits>::type; };

        template<typename LhsUnits, typename RhsUnits>
        concept HasMultiplyCalculationUnits = requires {
            typename multiply_result<LhsUnits, RhsUnits>::LhsUnit;
            typename multiply_result<LhsUnits, RhsUnits>::RhsUnit;
        };

        template<typename LhsUnits, typename RhsUnits>
        concept HasMultiplyResultUnit =
          requires { typename multiply_result<LhsUnits, RhsUnits>::ResultUnit; };

        template<typename LhsUnits, typename RhsUnits>
        concept HasDivideCalculationUnits = requires {
            typename divide_result<LhsUnits, RhsUnits>::LhsUnit;
            typename divide_result<LhsUnits, RhsUnits>::RhsUnit;
        };

        template<typename LhsUnits, typename RhsUnits>
        concept HasDivideResultUnit = requires { typename divide_result<LhsUnits, RhsUnits>::ResultUnit; };

        template<typename Units>
        concept HasReciprocalResult = requires { typename reciprocal_result<Units>::type; };

        template<typename Units>
        concept HasReciprocalCalculationUnits = requires {
            typename reciprocal_result<Units>::DenominatorUnit;
            typename reciprocal_result<Units>::ResultUnit;
        };

        template<typename LhsUnits, typename RhsUnits>
        using multiply_result_t = typename multiply_result<LhsUnits, RhsUnits>::type;

        template<typename LhsUnits, typename RhsUnits>
        using divide_result_t = typename divide_result<LhsUnits, RhsUnits>::type;

        template<typename Units>
        using reciprocal_result_t = typename reciprocal_result<Units>::type;
        // NOLINTEND(readability-identifier-naming)
    } // namespace detail

    // ---------- Operators between quantities ----------

    template<IsQuantity Lhs, IsQuantity Rhs>
        requires detail::HasMultiplyResult<detail::units_t<Lhs>, detail::units_t<Rhs>>
    constexpr auto operator*(const Lhs& lhs, const Rhs& rhs)
      -> detail::multiply_result_t<detail::units_t<Lhs>, detail::units_t<Rhs>>
    {
        using Traits = detail::multiply_result<detail::units_t<Lhs>, detail::units_t<Rhs>>;
        using Result = typename Traits::type;
        if constexpr (detail::HasMultiplyCalculationUnits<detail::units_t<Lhs>, detail::units_t<Rhs>>) {
            using LhsUnit = typename Traits::LhsUnit;
            using RhsUnit = typename Traits::RhsUnit;
            auto result{ lhs.template get<LhsUnit>() * rhs.template get<RhsUnit>() };
            if constexpr (std::same_as<Result, double>) {
                return result;
            }
            else {
                static_assert(detail::HasMultiplyResultUnit<detail::units_t<Lhs>, detail::units_t<Rhs>>);
                using ResultUnit = typename Traits::ResultUnit;
                return Result::template create<ResultUnit>(result);
            }
        }
        else if constexpr (std::same_as<Result, double>) {
            return lhs.get() * rhs.get();
        }
        else {
            return Result::create(lhs.get() * rhs.get());
        }
    }

    template<IsQuantity Lhs, IsQuantity Rhs>
        requires detail::HasDivideResult<detail::units_t<Lhs>, detail::units_t<Rhs>>
    constexpr auto operator/(const Lhs& lhs, const Rhs& rhs)
      -> detail::divide_result_t<detail::units_t<Lhs>, detail::units_t<Rhs>>
    {
        using Traits = detail::divide_result<detail::units_t<Lhs>, detail::units_t<Rhs>>;
        using Result = typename Traits::type;
        if constexpr (detail::HasDivideCalculationUnits<detail::units_t<Lhs>, detail::units_t<Rhs>>) {
            using LhsUnit = typename Traits::LhsUnit;
            using RhsUnit = typename Traits::RhsUnit;
            auto result{ lhs.template get<LhsUnit>() / rhs.template get<RhsUnit>() };
            if constexpr (std::same_as<Result, double>) {
                return result;
            }
            else {
                static_assert(detail::HasDivideResultUnit<detail::units_t<Lhs>, detail::units_t<Rhs>>);
                using ResultUnit = typename Traits::ResultUnit;
                return Result::template create<ResultUnit>(result);
            }
        }
        else if constexpr (std::same_as<Result, double>) {
            return lhs.get() / rhs.get();
        }
        else {
            return Result::create(lhs.get() / rhs.get());
        }
    }

    template<typename Scalar, IsQuantity Rhs>
        requires std::convertible_to<Scalar, double> && (!IsQuantity<Scalar>) &&
                 detail::HasReciprocalResult<detail::units_t<Rhs>>
    constexpr auto operator/(Scalar lhs, const Rhs& rhs) -> detail::reciprocal_result_t<detail::units_t<Rhs>>
    {
        using Traits = detail::reciprocal_result<detail::units_t<Rhs>>;
        using Result = typename Traits::type;
        if constexpr (detail::HasReciprocalCalculationUnits<detail::units_t<Rhs>>) {
            using DenominatorUnit = typename Traits::DenominatorUnit;
            using ResultUnit = typename Traits::ResultUnit;
            return Result::template create<ResultUnit>(static_cast<double>(lhs) /
                                                       rhs.template get<DenominatorUnit>());
        }
        else {
            return Result::create(static_cast<double>(lhs) / rhs.get());
        }
    }

    // ---------- Literals ----------

    template<meta::structural::Class Quantity, meta::structural::Class Units>
    class QuantityBase
    {
      public:
        using UnitsMeta = meta::structural::Info<Units>;

      private:
        static consteval auto determineBaseUnitIndex() -> size_t
        {
            auto index{ std::numeric_limits<size_t>::max() };
            meta::tuple::for_each<typename UnitsMeta::NestedTypes>([&index](auto i) {
                using T = meta::tuple::type_t<i, typename UnitsMeta::NestedTypes>;
                if constexpr (!IsUnit<T>) {
                    throw std::logic_error("Not a unit: " + std::string(meta::type::name<T>()));
                }
                if constexpr (T::IS_BASE) {
                    if (index != std::numeric_limits<size_t>::max()) {
                        throw std::logic_error("Duplicate base unit");
                    }
                    index = i;
                }
            });

            if (index == std::numeric_limits<size_t>::max()) {
                throw std::logic_error("Base unit not found");
            }
            return index;
        }

        static consteval auto makeUnitFactors()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array{ meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>::FACTOR... };
            }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{});
        }

        static consteval auto makeUnitOffsets()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array{ meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>::OFFSET... };
            }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{});
        }

        static consteval auto makeUnitSuffixes()
        {
            return []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array{ meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>::SUFFIX... };
            }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{});
        }

        static consteval auto makeSortedUnitIndices()
        {
            auto indices{ []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array{ Is... };
            }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{}) };

            constexpr auto factors{ makeUnitFactors() };
            constexpr auto base_unit_index{ determineBaseUnitIndex() };
            std::ranges::sort(indices, [&factors, base_unit_index](auto lhs, auto rhs) {
                if (factors[lhs] == factors[rhs]) {
                    if (lhs == base_unit_index) {
                        return false;
                    }
                    if (rhs == base_unit_index) {
                        return true;
                    }
                    return lhs < rhs;
                }
                return factors[lhs] < factors[rhs];
            });
            return indices;
        }

        static constexpr auto isPrintableUnitIndex(size_t index) -> bool
        {
            static constexpr auto enabled{ []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array{ meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>::AUTO_FORMAT... };
            }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{}) };
            return enabled[index] && UNIT_OFFSETS[index] == 0.0;
        }

        constexpr auto determinePrintUnitIndex() const -> size_t
        {
            auto abs_value{ std::abs(value) };
            if (abs_value == 0.0 || !std::isfinite(abs_value)) {
                return BASE_UNIT_INDEX;
            }

            for (auto i{ SORTED_UNIT_INDICES.size() }; i > 0; --i) {
                auto unit_index{ SORTED_UNIT_INDICES[i - 1] };
                if (isPrintableUnitIndex(unit_index) && (abs_value / UNIT_FACTORS[unit_index]) >= 1.0) {
                    return unit_index;
                }
            }

            for (auto unit_index : SORTED_UNIT_INDICES) {
                if (isPrintableUnitIndex(unit_index)) {
                    return unit_index;
                }
            }
            return BASE_UNIT_INDEX;
        }

        static constexpr auto suffix(size_t index) -> std::string
        {
            std::string result{ UNIT_SUFFIXES[index] };
            if (result == "n/a") {
                result.clear();
                auto i{ 0UZ };
                for (const auto c : UnitsMeta::NESTED_TYPE_NAMES[index]) {
                    if (i == 0 && c == 'u') {
                        result += "µ";
                    }
                    else if (c == '_') {
                        result += '/';
                    }
                    else if (i == UnitsMeta::NESTED_TYPE_NAMES[index].size() - 1 && c == '2') {
                        result += "²";
                    }
                    else {
                        result += c;
                    }
                    ++i;
                }
            }
            return result;
        }

        static constexpr auto BASE_UNIT_INDEX{ QuantityBase::determineBaseUnitIndex() };
        static constexpr auto SORTED_UNIT_INDICES{ QuantityBase::makeSortedUnitIndices() };
        static constexpr auto UNIT_FACTORS{ QuantityBase::makeUnitFactors() };
        static constexpr auto UNIT_OFFSETS{ QuantityBase::makeUnitOffsets() };
        static constexpr auto UNIT_SUFFIXES{ QuantityBase::makeUnitSuffixes() };

        QuantityBase() = default;
        QuantityBase(const QuantityBase&) = default;
        QuantityBase(QuantityBase&&) = default;

        constexpr QuantityBase(double val)
          : value{ val }
        {
        }

        friend Quantity;

      public:
        ~QuantityBase() = default;

        auto operator=(const QuantityBase&) -> QuantityBase& = default;

        auto operator=(QuantityBase&&) -> QuantityBase& = default;

        constexpr auto operator<=>(const QuantityBase&) const = default;

        constexpr auto operator+() const -> Quantity { return Quantity{ value }; }
        constexpr auto operator-() const -> Quantity { return Quantity{ -value }; }

        constexpr auto operator+(const Quantity& rhs) const -> Quantity
        {
            return Quantity{ value + rhs.value };
        }

        constexpr auto operator+=(const Quantity& rhs) -> Quantity&
        {
            value += rhs.value;
            return static_cast<Quantity&>(*this);
        }

        constexpr auto operator-(const Quantity& rhs) const -> Quantity
        {
            return Quantity{ value - rhs.value };
        }

        constexpr auto operator-=(const Quantity& rhs) -> Quantity&
        {
            value -= rhs.value;
            return static_cast<Quantity&>(*this);
        }

        constexpr auto operator*(std::convertible_to<double> auto rhs) const -> Quantity
        {
            return Quantity{ value * static_cast<double>(rhs) };
        }

        template<typename Scalar>
            requires std::convertible_to<Scalar, double>
        friend constexpr auto operator*(Scalar lhs, const Quantity& rhs) -> Quantity
        {
            return rhs * lhs;
        }

        constexpr auto operator*=(std::convertible_to<double> auto rhs) -> Quantity&
        {
            value = value * static_cast<double>(rhs);
            return static_cast<Quantity&>(*this);
        }

        constexpr auto operator/(const Quantity& rhs) const -> double { return value / rhs.value; }

        constexpr auto operator/(std::convertible_to<double> auto rhs) const -> Quantity
        {
            return Quantity{ value / static_cast<double>(rhs) };
        }

        constexpr auto operator/=(std::convertible_to<double> auto rhs) -> Quantity&
        {
            value = value / static_cast<double>(rhs);
            return static_cast<Quantity&>(*this);
        }

        template<IsUnit U>
            requires meta::tuple::ContainsType<typename UnitsMeta::NestedTypes, U>
        static constexpr auto suffix() -> std::string
        {
            constexpr auto index{ [] consteval {
                auto result{ -1Z };
                meta::tuple::for_each<typename UnitsMeta::NestedTypes>([&result](auto i) {
                    if constexpr (std::same_as<std::remove_cvref_t<U>,
                                               meta::tuple::type_t<i, typename UnitsMeta::NestedTypes>>) {
                        result = i;
                    }
                });
                return result;
            }() };
            static_assert(index >= 0Z && "Invalid Unit provided");

            return suffix(index);
        }

        friend std::ostream& operator<<(std::ostream& os, const QuantityBase& quantity)
        {
            auto index{ quantity.determinePrintUnitIndex() };
            os << (quantity.value / UNIT_FACTORS[index]);

            auto suf{ suffix(index) };
            if (!suf.empty()) {
                os << ' ' << suf;
            }

            return os;
        }

        template<pnm::meta::enumeration::ScopedEnum Unit>
        static constexpr auto create(double value, Unit unit) -> std::optional<Quantity>
        {
            using namespace std::string_view_literals;
            static_assert(pnm::meta::type::namespaces<Unit>() == std::array{ "pnm"sv, "units"sv });
            static_assert(pnm::meta::type::name<Unit>() == "Units");

            static constexpr auto units{ [] consteval {
                return pnm::meta::container::FixedMap{ []<std::size_t... Is>(std::index_sequence<Is...>) {
                    return std::array{ std::make_pair(UnitsMeta::NESTED_TYPE_NAMES[Is], +[](double v) {
                        using CurrentUnit = pnm::meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>;
                        return static_cast<double>((v + CurrentUnit::OFFSET) * CurrentUnit::FACTOR);
                    })... };
                }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{}) };
            }() };

            auto unit_name{ pnm::meta::enumeration::enumerator_name(unit) };
            auto modifier{ units.at(unit_name) };
            if (!modifier) {
                return std::nullopt;
            }

            return Quantity{ (*modifier)(value) };
        }

        static constexpr auto create(double value, std::string_view ucum_code) -> std::optional<Quantity>
        {
            if (ucum_code.empty()) {
                return std::nullopt;
            }
            static constexpr auto units{ [] consteval {
                return pnm::meta::container::FixedMap{ []<std::size_t... Is>(std::index_sequence<Is...>) {
                    return std::array{ std::make_pair(
                      pnm::meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>::UCUM_CODE,
                      +[](double v) {
                        using CurrentUnit = pnm::meta::tuple::type_t<Is, typename UnitsMeta::NestedTypes>;
                        return static_cast<double>((v + CurrentUnit::OFFSET) * CurrentUnit::FACTOR);
                    })... };
                }(std::make_index_sequence<UnitsMeta::numNestedTypes()>{}) };
            }() };

            auto modifier{ units.at(ucum_code) };
            if (!modifier) {
                return std::nullopt;
            }

            return Quantity{ (*modifier)(value) };
        }

        template<IsUnit Unit>
        static constexpr auto create(double value) -> Quantity
        {
            return Quantity{ static_cast<double>((value + Unit::OFFSET) * Unit::FACTOR) };
        }

        static constexpr auto create(double value) -> Quantity { return Quantity{ value }; }

        template<IsUnit Unit>
        constexpr auto get() const -> double
        {
            return static_cast<double>((value / Unit::FACTOR) - Unit::OFFSET);
        }

        constexpr auto get() const -> double { return value; }

        constexpr auto isFinite() const -> bool { return std::isfinite(value); }
        constexpr auto isInf() const -> bool { return std::isinf(value); }
        constexpr auto isNaN() const -> bool { return std::isnan(value); }

        double value{};
    };

    struct TimeUnits
    {
        PNM_TIME_UNITS(PNM_DEFINE_UNIT, PNM_DEFINE_BASE_UNIT, _)
    };

    class Time : public QuantityBase<Time, TimeUnits>
    {
      private:
        using Base = QuantityBase<Time, TimeUnits>;

      public:
        using Base::Base;
        using Base::create;
        using Base::get;

        constexpr Time() = default;

        template<typename Rep, typename Period>
            requires std::constructible_from<std::chrono::duration<double>,
                                             std::chrono::duration<Rep, Period>>
        constexpr Time(std::chrono::duration<Rep, Period> duration)
          : Base{ std::chrono::duration<double>{ duration }.count() }
        {
        }

        template<detail::ChronoDuration Duration = std::chrono::duration<double>>
        constexpr auto toChrono() const -> std::remove_cvref_t<Duration>
        {
            using Target = std::remove_cvref_t<Duration>;
            return std::chrono::duration_cast<Target>(std::chrono::duration<double>{ get() });
        }

        template<typename Rep, typename Period>
        explicit(!std::chrono::treat_as_floating_point_v<Rep>) constexpr operator std::chrono::
          duration<Rep, Period>() const
        {
            return toChrono<std::chrono::duration<Rep, Period>>();
        }

        template<typename Clock = std::chrono::steady_clock>
            requires requires {
                { Clock::now().time_since_epoch() } -> std::convertible_to<Time>;
            }
        static Time now()
        {
            return Clock::now().time_since_epoch();
        }
    };

    PNM_TIME_UNITS(PNM_DEFINE_LITERAL, PNM_DEFINE_LITERAL, Time)

    PNM_QUANTITIES(PNM_DEFINE_QUANTITY)

    // NOLINTBEGIN(readability-identifier-naming)
    enum class Units : std::uint8_t
    {
        PNM_QUANTITIES(PNM_ENUM_GROUP) PNM_TIME_UNITS(PNM_ENUM_UNIT, PNM_ENUM_UNIT, _)
    };
    // NOLINTEND(readability-identifier-naming)

    template<Units U>
    struct UnitTraits;
#define PNM_UNIT_TRAIT(quantity, suffix, ...)                                                                \
    template<>                                                                                               \
    struct UnitTraits<Units::suffix>                                                                         \
    {                                                                                                        \
        using Quantity = quantity;                                                                           \
        using Type = quantity##Units::suffix;                                                                \
    };
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define PNM_QUANTITY_TRAITS(quantity, list) list(PNM_UNIT_TRAIT, PNM_UNIT_TRAIT, quantity)
    PNM_QUANTITIES(PNM_QUANTITY_TRAITS)
    PNM_TIME_UNITS(PNM_UNIT_TRAIT, PNM_UNIT_TRAIT, Time)
#undef PNM_QUANTITY_TRAITS
#undef PNM_UNIT_TRAIT

    // NOLINTNEXTLINE(readability-identifier-naming)
    constexpr auto ucumCode(Units unit) -> std::optional<std::string_view>
    {
        // The enum and this table use the same registry order.
        static constexpr std::array codes{ PNM_QUANTITIES(PNM_UCUM_CODE_GROUP)
                                             PNM_TIME_UNITS(PNM_UCUM_CODE, PNM_UCUM_CODE, _) };
        auto index{ static_cast<std::size_t>(unit) };
        if (index >= codes.size() || codes[index].empty()) {
            return std::nullopt;
        }
        return codes[index];
    }

    static_assert(std::ranges::all_of(meta::enumeration::enumerators<Units>(),
                                      [](auto unit) { return ucumCode(unit).has_value(); }),
                  "Every built-in unit needs a UCUM expression");

    PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(Unitless, unitless, Time, s, Frequency, Hz)
    PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(Unitless, unitless, Frequency, Hz, Time, s)
    PNM_DEFINE_QUANTITY_SQUARE_RELATION(Distance, m, Area, m2)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(ByteSize, bytes, Time, s, DataRate, bytes_s)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Distance, m, Time, s, Velocity, m_s)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Velocity, m_s, Time, s, Acceleration, m_s2)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Acceleration, m_s2, Time, s, Jerk, m_s3)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Angle, rad, Time, s, AngularVelocity, rad_s)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(AngularVelocity, rad_s, Time, s, AngularAcceleration, rad_s2)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(AngularAcceleration, rad_s2, Time, s, AngularJerk, rad_s3)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Force, N, Mass, kg, Acceleration, m_s2)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Energy, J, Force, N, Distance, m)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Energy, J, Time, s, Power, W)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Force, N, Area, m2, Pressure, Pa)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Power, W, Voltage, V, Current, A)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Frequency, Hz, Time, s, FrequencyRate, Hz_s)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(FrequencyRate, Hz_s, Time, s, FrequencyAcceleration, Hz_s2)
    PNM_DEFINE_QUANTITY_QUOTIENT_RELATION(Distance, m, Angle, rad, TransmissionLead, m_rad)
    PNM_DEFINE_QUANTITY_RECIPROCAL_RELATION(Time, s, Frequency, Hz)
    PNM_DEFINE_QUANTITY_DIVIDE_RESULT_WITH_UNITS(Ratio, fraction, Time, s, Frequency, Hz)

    namespace detail
    {
        template<typename T>
        concept Dimensionless = std::same_as<T, Ratio> || std::same_as<T, Unitless>;

        template<>
        struct reciprocal_result<UnitlessUnits>
        {
            using type = Unitless;
        };
    }

    template<IsQuantity Quantity, detail::Dimensionless Scale>
    constexpr auto operator*(const Quantity& quantity, const Scale& scale)
    {
        if constexpr (std::same_as<Quantity, Ratio> && std::same_as<Scale, Unitless>) {
            // A percentage scales a generic unitless value in either multiplication order.
            return scale * quantity.get();
        }
        else {
            return quantity * scale.get();
        }
    }

    template<IsQuantity Quantity, detail::Dimensionless Scale>
        requires(!detail::Dimensionless<Quantity>)
    constexpr auto operator*(const Scale& scale, const Quantity& quantity) -> Quantity
    {
        return quantity * scale.get();
    }

    template<IsQuantity Quantity, detail::Dimensionless Scale>
        requires(!std::same_as<Quantity, Scale>)
    constexpr auto operator/(const Quantity& quantity, const Scale& scale) -> Quantity
    {
        return quantity / scale.get();
    }

    template<IsQuantity Quantity, detail::Dimensionless Scale>
    constexpr auto operator*=(Quantity& quantity, const Scale& scale) -> Quantity&
    {
        return quantity *= scale.get();
    }

    template<IsQuantity Quantity, detail::Dimensionless Scale>
    constexpr auto operator/=(Quantity& quantity, const Scale& scale) -> Quantity&
    {
        return quantity /= scale.get();
    }

    // ---------- Chrono interoperability ----------

    template<detail::ChronoDuration Duration, detail::Dimensionless Scale>
    constexpr auto operator*(Duration lhs, const Scale& rhs) -> Time
    {
        return Time{ lhs } * rhs.get();
    }

    template<detail::ChronoDuration Duration, detail::Dimensionless Scale>
    constexpr auto operator*(const Scale& lhs, Duration rhs) -> Time
    {
        return lhs.get() * Time{ rhs };
    }

    template<detail::ChronoDuration Duration, detail::Dimensionless Scale>
    constexpr auto operator/(Duration lhs, const Scale& rhs) -> Time
    {
        return Time{ lhs } / rhs.get();
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator+(const Time& lhs, Duration rhs) -> Time
    {
        return lhs + Time{ rhs };
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator+(Duration lhs, const Time& rhs) -> Time
    {
        return Time{ lhs } + rhs;
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator-(const Time& lhs, Duration rhs) -> Time
    {
        return lhs - Time{ rhs };
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator-(Duration lhs, const Time& rhs) -> Time
    {
        return Time{ lhs } - rhs;
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator==(const Time& lhs, Duration rhs) -> bool
    {
        return lhs == Time{ rhs };
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator==(Duration lhs, const Time& rhs) -> bool
    {
        return Time{ lhs } == rhs;
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator<=>(const Time& lhs, Duration rhs)
    {
        return lhs <=> Time{ rhs };
    }

    template<detail::ChronoDuration Duration>
    constexpr auto operator<=>(Duration lhs, const Time& rhs)
    {
        return Time{ lhs } <=> rhs;
    }

    template<IsQuantity Lhs, detail::ChronoDuration Rhs>
        requires detail::HasMultiplyResult<detail::units_t<Lhs>, detail::units_t<Time>>
    constexpr auto operator*(const Lhs& lhs, Rhs rhs)
      -> detail::multiply_result_t<detail::units_t<Lhs>, detail::units_t<Time>>
    {
        return lhs * Time{ rhs };
    }

    template<detail::ChronoDuration Lhs, IsQuantity Rhs>
        requires detail::HasMultiplyResult<detail::units_t<Time>, detail::units_t<Rhs>>
    constexpr auto operator*(Lhs lhs, const Rhs& rhs)
      -> detail::multiply_result_t<detail::units_t<Time>, detail::units_t<Rhs>>
    {
        return Time{ lhs } * rhs;
    }

    template<IsQuantity Lhs, detail::ChronoDuration Rhs>
        requires detail::HasDivideResult<detail::units_t<Lhs>, detail::units_t<Time>>
    constexpr auto operator/(const Lhs& lhs, Rhs rhs)
      -> detail::divide_result_t<detail::units_t<Lhs>, detail::units_t<Time>>
    {
        return lhs / Time{ rhs };
    }

    template<detail::ChronoDuration Lhs, IsQuantity Rhs>
        requires detail::HasDivideResult<detail::units_t<Time>, detail::units_t<Rhs>>
    constexpr auto operator/(Lhs lhs, const Rhs& rhs)
      -> detail::divide_result_t<detail::units_t<Time>, detail::units_t<Rhs>>
    {
        return Time{ lhs } / rhs;
    }

    template<typename Scalar, detail::ChronoDuration Duration>
        requires std::convertible_to<Scalar, double>
    constexpr auto operator/(Scalar numerator, Duration duration) -> Frequency
    {
        return numerator / Time{ duration };
    }

    template<detail::ChronoDuration Duration>
    constexpr auto frequency_from_period(Duration duration) -> Frequency
    {
        return 1.0 / Time{ duration };
    }

    namespace literals
    {
        using std::chrono_literals::operator""h;
        using std::chrono_literals::operator""min;
        using std::chrono_literals::operator""ms;
        using std::chrono_literals::operator""ns;
        using std::chrono_literals::operator""s;
        using std::chrono_literals::operator""us;

        using ::pnm::units::operator/;
    }
}

#undef PNM_UNITLESS_UNITS
#undef PNM_DISTANCE_UNITS
#undef PNM_AREA_UNITS
#undef PNM_TIME_UNITS
#undef PNM_BYTE_SIZE_UNITS
#undef PNM_MASS_UNITS
#undef PNM_TEMPERATURE_UNITS
#undef PNM_CURRENT_UNITS
#undef PNM_VOLTAGE_UNITS
#undef PNM_FORCE_UNITS
#undef PNM_ENERGY_UNITS
#undef PNM_POWER_UNITS
#undef PNM_PRESSURE_UNITS
#undef PNM_FREQUENCY_UNITS
#undef PNM_FREQUENCY_RATE_UNITS
#undef PNM_FREQUENCY_ACCELERATION_UNITS
#undef PNM_TRANSMISSION_LEAD_UNITS
#undef PNM_DATA_RATE_UNITS
#undef PNM_VELOCITY_UNITS
#undef PNM_ACCELERATION_UNITS
#undef PNM_JERK_UNITS
#undef PNM_ANGLE_UNITS
#undef PNM_ANGULAR_VELOCITY_UNITS
#undef PNM_ANGULAR_ACCELERATION_UNITS
#undef PNM_ANGULAR_JERK_UNITS
#undef PNM_RATIO_UNITS
#undef PNM_QUANTITIES
#undef PNM_ENUM_UNIT
#undef PNM_ENUM_GROUP
#undef PNM_UCUM_CODE
#undef PNM_UCUM_CODE_GROUP
