#include "pneumo/units.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

using pnm::units::Acceleration;
using pnm::units::AccelerationUnits;
using pnm::units::Angle;
using pnm::units::AngleUnits;
using pnm::units::AngularVelocity;
using pnm::units::AngularVelocityUnits;
using pnm::units::Area;
using pnm::units::AreaUnits;
using pnm::units::ByteSize;
using pnm::units::ByteSizeUnits;
using pnm::units::Current;
using pnm::units::CurrentUnits;
using pnm::units::DataRate;
using pnm::units::DataRateUnits;
using pnm::units::Distance;
using pnm::units::DistanceUnits;
using pnm::units::Energy;
using pnm::units::EnergyUnits;
using pnm::units::Force;
using pnm::units::ForceUnits;
using pnm::units::Frequency;
using pnm::units::FrequencyUnits;
using pnm::units::Mass;
using pnm::units::MassUnits;
using pnm::units::Power;
using pnm::units::PowerUnits;
using pnm::units::Pressure;
using pnm::units::PressureUnits;
using pnm::units::Ratio;
using pnm::units::RatioUnits;
using pnm::units::Temperature;
using pnm::units::TemperatureUnits;
using pnm::units::Time;
using pnm::units::TimeUnits;
using pnm::units::Unitless;
using pnm::units::UnitlessUnits;
using pnm::units::Units;
using pnm::units::Velocity;
using pnm::units::VelocityUnits;
using pnm::units::Voltage;
using pnm::units::VoltageUnits;

using namespace pnm::units::literals;

static_assert(std::is_same_v<decltype(512_bytes), ByteSize>);
static_assert(std::is_same_v<decltype(9.81_m_s2), Acceleration>);
static_assert(std::is_same_v<decltype(25.0_m2), Area>);
static_assert(std::is_same_v<decltype(2.0_A), Current>);
static_assert(std::is_same_v<decltype(3.5_km), Distance>);
static_assert(std::is_same_v<decltype(12.5_MiB_s), DataRate>);
static_assert(std::is_same_v<decltype(42.0_J), Energy>);
static_assert(std::is_same_v<decltype(50.0_N), Force>);
static_assert(std::is_same_v<decltype(60.0_Hz), Frequency>);
static_assert(std::is_same_v<decltype(75.0_kg), Mass>);
static_assert(std::is_same_v<decltype(10.0_W), Power>);
static_assert(std::is_same_v<decltype(1013.25_hPa), Pressure>);
static_assert(std::is_same_v<decltype(1013.25_mbar), Pressure>);
static_assert(std::is_same_v<decltype(1.0_kbar), Pressure>);
static_assert(std::is_same_v<decltype(22.4_C), Temperature>);
static_assert(std::is_same_v<decltype(2.0_h), Time>);
static_assert(std::is_same_v<decltype(36.0_km_h), Velocity>);
static_assert(std::is_same_v<decltype(12.0_V), Voltage>);
static_assert(std::is_same_v<decltype(50_percent), Ratio>);
static_assert(std::is_same_v<decltype(0.5_percent), Ratio>);
static_assert(std::is_same_v<decltype(1_fraction), Ratio>);
static_assert(std::is_same_v<decltype(2_unitless), Unitless>);
static_assert(std::is_same_v<decltype(0.5_unitless), Unitless>);
static_assert(pnm::units::IsQuantity<Unitless>);
static_assert(!std::is_same_v<Unitless, Ratio>);
static_assert(!std::is_convertible_v<Unitless, double>);
static_assert(!std::is_constructible_v<Unitless, double>);
static_assert(!std::is_convertible_v<Ratio, Unitless>);
static_assert(!std::is_convertible_v<Unitless, Ratio>);
static_assert((2_unitless * 0.5_unitless).get() == 1.0);
static_assert((50_percent * 2_unitless).get() == 1.0);
static_assert((2_unitless / 4_s).get() == 0.5);
static_assert((2_unitless / 4_Hz).get() == 0.5);
static_assert(std::is_same_v<decltype(1_rev), Angle>);
static_assert(std::is_same_v<decltype(60_rpm), AngularVelocity>);
static_assert(std::is_same_v<decltype(1.0_rad_s), AngularVelocity>);
static_assert(!std::is_convertible_v<Ratio, double>);
static_assert(!std::is_constructible_v<Ratio, double>);
static_assert(!std::is_convertible_v<Frequency, AngularVelocity>);
static_assert(!std::is_convertible_v<AngularVelocity, Frequency>);
static_assert(std::is_same_v<decltype(50_percent * 50_percent), Ratio>);
static_assert(std::is_same_v<decltype(50_percent / 25_percent), double>);
static_assert(std::is_same_v<decltype(60_rpm / 30_rpm), double>);
static_assert((50_percent * 50_percent).get() == 0.25);
static_assert((2000_rpm * 50_percent).get() == (1000_rpm).get());
static_assert((1_rev / 2_s).get() == std::numbers::pi);
static_assert(std::is_default_constructible_v<Distance>);
static_assert(!std::is_constructible_v<Distance, double>);
static_assert(
  std::is_same_v<decltype(std::declval<Distance&>() = std::declval<const Distance&>()), Distance&>);
static_assert(std::is_same_v<decltype(std::declval<Distance&>() = std::declval<Distance&&>()), Distance&>);
static_assert(
  std::is_same_v<decltype(std::declval<Distance&>() += std::declval<const Distance&>()), Distance&>);
static_assert(
  std::is_same_v<decltype(std::declval<Distance&>() -= std::declval<const Distance&>()), Distance&>);
static_assert(std::is_same_v<decltype(std::declval<Distance&>() *= 2.0), Distance&>);
static_assert(std::is_same_v<decltype(std::declval<Distance&>() /= 2.0), Distance&>);
static_assert(std::is_same_v<decltype(250ms), std::chrono::milliseconds>);
static_assert(std::is_convertible_v<std::chrono::milliseconds, Time>);
static_assert(std::is_convertible_v<std::chrono::duration<long double, std::milli>, Time>);
static_assert(std::is_convertible_v<Time, std::chrono::duration<double>>);
static_assert(std::is_convertible_v<Time, std::chrono::duration<double, std::milli>>);
static_assert(!std::is_convertible_v<Time, std::chrono::milliseconds>);
static_assert(std::is_scoped_enum_v<Units>);
static_assert(!std::is_convertible_v<Units, int>);
static_assert(Units::m != Units::s);
static_assert(Units::fraction != Units::unitless);
static_assert(std::is_same_v<decltype(Distance::create(1.0, Units::m)), std::optional<Distance>>);
static_assert(Distance::create(2.0, Units::km)->get() == 2000.0);
static_assert(Time::create(1.5, Units::min)->get() == 90.0);
static_assert(!Distance::create(1.0, Units::s));
static_assert(!Unitless::create(1.0, Units::fraction));
static_assert(pnm::units::ucumCode(Units::m) == "m");
static_assert(pnm::units::ucumCode(Units::m_s3) == "m/s3");
static_assert(std::is_same_v<decltype(Distance::create(1.0, "m")), std::optional<Distance>>);
static_assert(Distance::create(2.5, "km")->get() == 2500.0);
static_assert(Time::create(1.5, "min")->get() == 90.0);
static_assert(Unitless::create(2.0, "1")->get() == 2.0);
static_assert(Ratio::create(2.0, "1")->get() == 2.0);
static_assert(!Distance::create(1.0, "s"));
static_assert(!Distance::create(1.0, ""));
static_assert(DistanceUnits::km::UCUM_CODE == "km");
static_assert(DistanceUnits::m::UCUM_CODE == "m");
static_assert(TimeUnits::s::UCUM_CODE == "s");
static_assert(pnm::units::JerkUnits::m_s3::UCUM_CODE == "m/s3");
static_assert(!std::same_as<DistanceUnits::m, TimeUnits::s>);
static_assert(pnm::units::Unit<std::milli>::UCUM_CODE == "n/a");
static_assert(pnm::units::BaseUnit<>::UCUM_CODE == "n/a");
static_assert(pnm::units::Unit<std::milli, 0.0, "">::UCUM_CODE.empty());
static_assert(pnm::units::BaseUnit<"">::UCUM_CODE.empty());
static_assert(pnm::units::IsUnit<pnm::units::Unit<std::milli>>);
static_assert(pnm::units::IsUnit<pnm::units::BaseUnit<>>);
static_assert(pnm::units::BaseUnit<>::IS_BASE);
static_assert(pnm::units::BaseUnit<>::AUTO_FORMAT);
static_assert(pnm::units::Unit<std::milli, 0.0, "mm", "mm">::UCUM_CODE == "mm");
static_assert(pnm::units::BaseUnit<"m", "m">::UCUM_CODE == "m");

namespace
{
    struct OptionalCodeUnits
    {
        using s = pnm::units::BaseUnit<>;
        using ns = pnm::units::Unit<std::nano>;
        using us = pnm::units::Unit<std::micro>;
        using milliseconds = pnm::units::Unit<std::milli, 0.0, "ms", "milliseconds">;
        using internal = pnm::units::Unit<std::kilo, 0.0, "">;
    };

    class OptionalCodeQuantity : public pnm::units::QuantityBase<OptionalCodeQuantity, OptionalCodeUnits>
    {
      public:
        using QuantityBase::create;
        using QuantityBase::QuantityBase;
    };

    static_assert(OptionalCodeQuantity::create(2.0, "s")->get() == 2.0);
    static_assert(OptionalCodeQuantity::create(2.0, "ms")->get() == 0.002);
    static_assert(!OptionalCodeQuantity::create(2.0, "n/a"));

    using Quantities = std::tuple<pnm::units::FrequencyRate,
                                  pnm::units::FrequencyAcceleration,
                                  pnm::units::TransmissionLead,
                                  Distance,
                                  Area,
                                  Time,
                                  ByteSize,
                                  Mass,
                                  Temperature,
                                  Current,
                                  Voltage,
                                  Force,
                                  Energy,
                                  Power,
                                  Pressure,
                                  Frequency,
                                  DataRate,
                                  Velocity,
                                  Acceleration,
                                  pnm::units::Jerk,
                                  Angle,
                                  AngularVelocity,
                                  pnm::units::AngularAcceleration,
                                  pnm::units::AngularJerk,
                                  Ratio,
                                  Unitless>;

    struct FixedClock
    {
        using rep = std::chrono::milliseconds::rep;
        using period = std::chrono::milliseconds::period;
        using duration = std::chrono::milliseconds;
        using time_point = std::chrono::time_point<FixedClock>;
        [[maybe_unused]] static constexpr bool is_steady = true;

        static constexpr auto now() -> time_point { return time_point{ duration{ 1234 } }; }
    };

    struct InvalidClock
    {
        static auto now() -> int;
    };

    template<typename Clock>
    concept HasTimeNow = requires {
        { Time::now<Clock>() } -> std::same_as<Time>;
    };

    static_assert(HasTimeNow<std::chrono::steady_clock>);
    static_assert(HasTimeNow<std::chrono::system_clock>);
    static_assert(HasTimeNow<std::chrono::high_resolution_clock>);
    static_assert(HasTimeNow<FixedClock>);
    static_assert(!HasTimeNow<int>);
    static_assert(!HasTimeNow<InvalidClock>);

    auto stream_to_string(const auto& quantity) -> std::string
    {
        std::ostringstream stream;
        stream << quantity;
        return stream.str();
    }
}

TEST(UnitsTests, CombinedEnumContainsEveryRegisteredUnit)
{
    constexpr auto names{ pnm::meta::enumeration::enumerator_names<Units>() };
    std::size_t expected_count{};
    pnm::meta::tuple::for_each<Quantities>([&](auto index) {
        using Quantity = pnm::meta::tuple::type_t<index, Quantities>;
        for (const auto name : Quantity::UnitsMeta::NESTED_TYPE_NAMES) {
            EXPECT_EQ(std::ranges::count(names, name), 1) << name;
            ++expected_count;
        }
    });
    EXPECT_EQ(names.size(), expected_count);
}

TEST(UnitsTests, CombinedEnumPreservesUnitIdentifiers)
{
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::nm), "nm");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::um), "um");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::mm2), "mm2");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::C), "C");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::KiB_s), "KiB_s");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::rpm), "rpm");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::percent), "percent");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::unitless), "unitless");
    EXPECT_EQ(pnm::meta::enumeration::enumerator_name(Units::s), "s");
}

TEST(UnitsTests, RuntimeCreateMatchesTypedCreationForEveryRegisteredUnit)
{
    pnm::meta::tuple::for_each<Quantities>([](auto quantity_index) {
        using Quantity = pnm::meta::tuple::type_t<decltype(quantity_index)::value, Quantities>;
        SCOPED_TRACE(Quantity::UnitsMeta::NAME);
        pnm::meta::tuple::for_each<typename Quantity::UnitsMeta::NestedTypes>([](auto unit_index) {
            using Unit = pnm::meta::tuple::type_t<decltype(unit_index)::value,
                                                  typename Quantity::UnitsMeta::NestedTypes>;
            constexpr auto name{ Quantity::UnitsMeta::NESTED_TYPE_NAMES[decltype(unit_index)::value] };
            constexpr auto unit{ pnm::meta::enumeration::from_string<Units>(name) };
            static_assert(unit.has_value());
            SCOPED_TRACE(name);
            for (const auto value : { -2.5, 0.0, 1.25, 42.0 }) {
                SCOPED_TRACE(value);
                const auto result{ Quantity::create(value, *unit) };
                ASSERT_TRUE(result.has_value());
                EXPECT_DOUBLE_EQ(result->get(), Quantity::template create<Unit>(value).get());
            }
        });
    });
}

TEST(UnitsTests, RuntimeCreateRejectsEveryUnitFromOtherQuantities)
{
    pnm::meta::tuple::for_each<Quantities>([](auto index) {
        using Quantity = pnm::meta::tuple::type_t<index, Quantities>;
        SCOPED_TRACE(Quantity::UnitsMeta::NAME);
        for (const auto unit : pnm::meta::enumeration::enumerators<Units>()) {
            const auto name{ pnm::meta::enumeration::enumerator_name(unit) };
            if (!std::ranges::contains(Quantity::UnitsMeta::NESTED_TYPE_NAMES, name)) {
                SCOPED_TRACE(name);
                EXPECT_FALSE(Quantity::create(1.0, unit).has_value());
            }
        }
    });
}

TEST(UnitsTests, RuntimeCreateRejectsUnnamedEnumValues)
{
    constexpr std::array invalid_units{ static_cast<Units>(pnm::meta::enumeration::count<Units>()),
                                        static_cast<Units>(
                                          std::numeric_limits<std::underlying_type_t<Units>>::max()) };
    for (const auto unit : invalid_units) {
        ASSERT_TRUE(pnm::meta::enumeration::enumerator_name(unit).empty());
        pnm::meta::tuple::for_each<Quantities>([unit](auto index) {
            using Quantity = pnm::meta::tuple::type_t<index, Quantities>;
            SCOPED_TRACE(Quantity::UnitsMeta::NAME);
            EXPECT_FALSE(Quantity::create(1.0, unit).has_value());
        });
    }
    // Invalid lookups must not affect a subsequent valid construction.
    const auto valid{ Distance::create(2.0, Units::km) };
    ASSERT_TRUE(valid.has_value());
    EXPECT_DOUBLE_EQ(valid->get(), 2000.0);
}

TEST(UnitsTests, RuntimeCreateAppliesTemperatureOffsetsBeforeScaling)
{
    struct Case
    {
        double input;
        Units unit;
        double celsius;
    };
    constexpr std::array cases{ Case{ 30.0, Units::C, 30.0 },   Case{ 273.15, Units::K, 0.0 },
                                Case{ 0.0, Units::K, -273.15 }, Case{ 32.0, Units::F, 0.0 },
                                Case{ 212.0, Units::F, 100.0 }, Case{ -40.0, Units::F, -40.0 } };
    for (const auto& item : cases) {
        SCOPED_TRACE(item.input);
        SCOPED_TRACE(pnm::meta::enumeration::enumerator_name(item.unit));
        const auto result{ Temperature::create(item.input, item.unit) };
        ASSERT_TRUE(result.has_value());
        EXPECT_NEAR(result->get(), item.celsius, 1e-12);
    }
}

TEST(UnitsTests, RuntimeCreateHandlesPercentRotationAndCustomTime)
{
    const auto percent{ Ratio::create(50.0, Units::percent) };
    ASSERT_TRUE(percent.has_value());
    EXPECT_DOUBLE_EQ(percent->get(), 0.5);

    const auto rotation{ AngularVelocity::create(60.0, Units::rpm) };
    ASSERT_TRUE(rotation.has_value());
    EXPECT_DOUBLE_EQ(rotation->get(), 2.0 * std::numbers::pi);

    const auto time{ Time::create(1.5, Units::min) };
    ASSERT_TRUE(time.has_value());
    EXPECT_EQ(time->toChrono<std::chrono::milliseconds>(), std::chrono::milliseconds{ 90000 });

    const auto unitless{ Unitless::create(-2.5, Units::unitless) };
    ASSERT_TRUE(unitless.has_value());
    EXPECT_DOUBLE_EQ(unitless->get(), -2.5);
}

TEST(UnitsTests, RuntimeCreatePreservesNonFiniteValuesForValidUnits)
{
    const auto nan{ Distance::create(std::numeric_limits<double>::quiet_NaN(), Units::km) };
    ASSERT_TRUE(nan.has_value());
    EXPECT_TRUE(nan->isNaN());
    for (const auto value :
         { std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
        const auto result{ Distance::create(value, Units::km) };
        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result->isInf());
        EXPECT_EQ(result->get(), value);
    }
}

TEST(UnitsTests, UcumCodesMatchRegisteredExpressions)
{
    using namespace std::string_view_literals;
    constexpr std::array cases{ std::pair{ Units::unitless, "1"sv },
                                std::pair{ Units::nm, "nm"sv },
                                std::pair{ Units::um, "um"sv },
                                std::pair{ Units::mm, "mm"sv },
                                std::pair{ Units::cm, "cm"sv },
                                std::pair{ Units::dm, "dm"sv },
                                std::pair{ Units::m, "m"sv },
                                std::pair{ Units::km, "km"sv },
                                std::pair{ Units::mm2, "mm2"sv },
                                std::pair{ Units::cm2, "cm2"sv },
                                std::pair{ Units::dm2, "dm2"sv },
                                std::pair{ Units::m2, "m2"sv },
                                std::pair{ Units::km2, "km2"sv },
                                std::pair{ Units::ns, "ns"sv },
                                std::pair{ Units::us, "us"sv },
                                std::pair{ Units::ms, "ms"sv },
                                std::pair{ Units::s, "s"sv },
                                std::pair{ Units::min, "min"sv },
                                std::pair{ Units::h, "h"sv },
                                std::pair{ Units::bytes, "By"sv },
                                std::pair{ Units::KiB, "KiBy"sv },
                                std::pair{ Units::MiB, "MiBy"sv },
                                std::pair{ Units::GiB, "GiBy"sv },
                                std::pair{ Units::KB, "kBy"sv },
                                std::pair{ Units::MB, "MBy"sv },
                                std::pair{ Units::GB, "GBy"sv },
                                std::pair{ Units::mg, "mg"sv },
                                std::pair{ Units::g, "g"sv },
                                std::pair{ Units::kg, "kg"sv },
                                std::pair{ Units::t, "t"sv },
                                std::pair{ Units::C, "Cel"sv },
                                std::pair{ Units::K, "K"sv },
                                std::pair{ Units::F, "[degF]"sv },
                                std::pair{ Units::uA, "uA"sv },
                                std::pair{ Units::mA, "mA"sv },
                                std::pair{ Units::A, "A"sv },
                                std::pair{ Units::kA, "kA"sv },
                                std::pair{ Units::mV, "mV"sv },
                                std::pair{ Units::V, "V"sv },
                                std::pair{ Units::kV, "kV"sv },
                                std::pair{ Units::MV, "MV"sv },
                                std::pair{ Units::mN, "mN"sv },
                                std::pair{ Units::N, "N"sv },
                                std::pair{ Units::kN, "kN"sv },
                                std::pair{ Units::MN, "MN"sv },
                                std::pair{ Units::mJ, "mJ"sv },
                                std::pair{ Units::J, "J"sv },
                                std::pair{ Units::kJ, "kJ"sv },
                                std::pair{ Units::MJ, "MJ"sv },
                                std::pair{ Units::Wh, "W.h"sv },
                                std::pair{ Units::kWh, "kW.h"sv },
                                std::pair{ Units::mW, "mW"sv },
                                std::pair{ Units::W, "W"sv },
                                std::pair{ Units::kW, "kW"sv },
                                std::pair{ Units::MW, "MW"sv },
                                std::pair{ Units::Pa, "Pa"sv },
                                std::pair{ Units::hPa, "hPa"sv },
                                std::pair{ Units::mbar, "mbar"sv },
                                std::pair{ Units::kPa, "kPa"sv },
                                std::pair{ Units::bar, "bar"sv },
                                std::pair{ Units::atm, "atm"sv },
                                std::pair{ Units::MPa, "MPa"sv },
                                std::pair{ Units::kbar, "kbar"sv },
                                std::pair{ Units::mHz, "mHz"sv },
                                std::pair{ Units::Hz, "Hz"sv },
                                std::pair{ Units::kHz, "kHz"sv },
                                std::pair{ Units::MHz, "MHz"sv },
                                std::pair{ Units::GHz, "GHz"sv },
                                std::pair{ Units::bytes_s, "By/s"sv },
                                std::pair{ Units::KB_s, "kBy/s"sv },
                                std::pair{ Units::MB_s, "MBy/s"sv },
                                std::pair{ Units::GB_s, "GBy/s"sv },
                                std::pair{ Units::KiB_s, "KiBy/s"sv },
                                std::pair{ Units::MiB_s, "MiBy/s"sv },
                                std::pair{ Units::GiB_s, "GiBy/s"sv },
                                std::pair{ Units::mm_s, "mm/s"sv },
                                std::pair{ Units::m_s, "m/s"sv },
                                std::pair{ Units::km_h, "km/h"sv },
                                std::pair{ Units::mm_s2, "mm/s2"sv },
                                std::pair{ Units::m_s2, "m/s2"sv },
                                std::pair{ Units::km_h2, "km/h2"sv },
                                std::pair{ Units::mm_s3, "mm/s3"sv },
                                std::pair{ Units::m_s3, "m/s3"sv },
                                std::pair{ Units::rad, "rad"sv },
                                std::pair{ Units::deg, "deg"sv },
                                std::pair{ Units::rev, "circ"sv },
                                std::pair{ Units::rad_s, "rad/s"sv },
                                std::pair{ Units::rpm, "circ/min"sv },
                                std::pair{ Units::deg_s, "deg/s"sv },
                                std::pair{ Units::rad_s2, "rad/s2"sv },
                                std::pair{ Units::deg_s2, "deg/s2"sv },
                                std::pair{ Units::rpm_s, "circ/(min.s)"sv },
                                std::pair{ Units::rad_s3, "rad/s3"sv },
                                std::pair{ Units::deg_s3, "deg/s3"sv },
                                std::pair{ Units::rpm_s2, "circ/(min.s2)"sv },
                                std::pair{ Units::fraction, "1"sv },
                                std::pair{ Units::percent, "%"sv },
                                std::pair{ Units::Hz_s, "Hz/s"sv },
                                std::pair{ Units::Hz_s2, "Hz/s2"sv },
                                std::pair{ Units::m_rad, "m/rad"sv },
                                std::pair{ Units::mm_rev, "mm/circ"sv } };
    static_assert(cases.size() == pnm::meta::enumeration::count<Units>());
    for (const auto& [unit, expected] : cases) {
        SCOPED_TRACE(pnm::meta::enumeration::enumerator_name(unit));
        EXPECT_EQ(pnm::units::ucumCode(unit), expected);
    }
}

TEST(UnitsTests, OptionalUcumCodesPreserveIdentifiersAndOverrides)
{
    EXPECT_EQ(TimeUnits::ns::UCUM_CODE, "ns");
    EXPECT_EQ(TimeUnits::s::UCUM_CODE, "s");
    EXPECT_EQ(DistanceUnits::um::UCUM_CODE, "um");
    EXPECT_EQ(Distance::suffix<DistanceUnits::um>(), "µm");
    EXPECT_EQ(ByteSizeUnits::GB::UCUM_CODE, "GBy");
    EXPECT_EQ(ByteSize::suffix<ByteSizeUnits::GB>(), "GB");
    EXPECT_FALSE(ByteSizeUnits::GB::AUTO_FORMAT);
    EXPECT_EQ(TemperatureUnits::C::UCUM_CODE, "Cel");
    EXPECT_EQ(Temperature::suffix<TemperatureUnits::C>(), "°C");
    EXPECT_FALSE(Time::create(1.0, "n/a"));
}

TEST(UnitsTests, CustomUnitsInferMissingUcumCodesFromReflectedNames)
{
    const auto seconds{ OptionalCodeQuantity::create(2.0, "s") };
    const auto nanoseconds{ OptionalCodeQuantity::create(2.0, "ns") };
    const auto microseconds{ OptionalCodeQuantity::create(2.0, "us") };
    const auto milliseconds{ OptionalCodeQuantity::create(2.0, "ms") };
    ASSERT_TRUE(seconds);
    ASSERT_TRUE(nanoseconds);
    ASSERT_TRUE(microseconds);
    ASSERT_TRUE(milliseconds);
    EXPECT_DOUBLE_EQ(seconds->get(), 2.0);
    EXPECT_DOUBLE_EQ(nanoseconds->get(), 2e-9);
    EXPECT_DOUBLE_EQ(microseconds->get(), 2e-6);
    EXPECT_DOUBLE_EQ(milliseconds->get(), 0.002);
    EXPECT_EQ(OptionalCodeQuantity::suffix<OptionalCodeUnits::us>(), "µs");
    EXPECT_FALSE(OptionalCodeQuantity::create(1.0, "µs"));
    EXPECT_FALSE(OptionalCodeQuantity::create(1.0, "milliseconds"));
    EXPECT_FALSE(OptionalCodeQuantity::create(1.0, "internal"));
    EXPECT_FALSE(OptionalCodeQuantity::create(1.0, ""));
    EXPECT_FALSE(OptionalCodeQuantity::create(1.0, "n/a"));
    EXPECT_DOUBLE_EQ(OptionalCodeQuantity::create<OptionalCodeUnits::internal>(2.0).get(), 2000.0);
}

TEST(UnitsTests, UcumCodesCoverEveryUnitAndRejectInvalidEnumerators)
{
    for (const auto unit : pnm::meta::enumeration::enumerators<Units>()) {
        SCOPED_TRACE(pnm::meta::enumeration::enumerator_name(unit));
        const auto code{ pnm::units::ucumCode(unit) };
        ASSERT_TRUE(code);
        EXPECT_FALSE(code->empty());
        EXPECT_TRUE(std::ranges::all_of(*code, [](unsigned char c) { return c > ' ' && c < 127; }));
    }
    EXPECT_FALSE(pnm::units::ucumCode(static_cast<Units>(pnm::meta::enumeration::count<Units>())));
    EXPECT_FALSE(
      pnm::units::ucumCode(static_cast<Units>(std::numeric_limits<std::underlying_type_t<Units>>::max())));
}

TEST(UnitsTests, DecimalAndBinaryByteUnitsRemainDistinct)
{
    EXPECT_DOUBLE_EQ((1_KB).get(), 1000);
    EXPECT_DOUBLE_EQ((1_MB).get(), 1000000);
    EXPECT_DOUBLE_EQ((1_GB).get(), 1000000000);
    EXPECT_DOUBLE_EQ((1_KiB).get(), 1024);
    EXPECT_DOUBLE_EQ((1_MiB).get(), 1048576);
    EXPECT_DOUBLE_EQ((1_GiB).get(), 1073741824);
    EXPECT_DOUBLE_EQ((1_KiB).get<ByteSizeUnits::KB>(), 1.024);
    EXPECT_DOUBLE_EQ((1_MB).get<ByteSizeUnits::MiB>(), 0.95367431640625);
    EXPECT_DOUBLE_EQ((1_GB).get<ByteSizeUnits::GiB>(), 0.9313225746154785);
    EXPECT_DOUBLE_EQ(ByteSize::create(1, "kBy")->get(), 1000);
    EXPECT_DOUBLE_EQ(ByteSize::create(1, "MBy")->get(), 1000000);
    EXPECT_DOUBLE_EQ(ByteSize::create(1, "GBy")->get(), 1000000000);
    EXPECT_DOUBLE_EQ(ByteSize::create(1, "KiBy")->get(), 1024);
    EXPECT_EQ(ByteSize::suffix<ByteSizeUnits::KB>(), "kB");
    EXPECT_EQ(ByteSize::suffix<ByteSizeUnits::KiB>(), "KiB");
}

TEST(UnitsTests, DecimalAndBinaryRatesPreserveDimensionalConversions)
{
    EXPECT_DOUBLE_EQ((1_KB_s).get(), 1000);
    EXPECT_DOUBLE_EQ((1_MB_s).get(), 1000000);
    EXPECT_DOUBLE_EQ((1_GB_s).get(), 1000000000);
    EXPECT_DOUBLE_EQ((1_KiB_s).get(), 1024);
    EXPECT_DOUBLE_EQ((1_MiB_s).get(), 1048576);
    EXPECT_DOUBLE_EQ((1_GiB_s).get(), 1073741824);
    EXPECT_DOUBLE_EQ((1_MiB_s).get<DataRateUnits::MB_s>(), 1.048576);
    EXPECT_DOUBLE_EQ(DataRate::create(1, "kBy/s")->get(), 1000);
    EXPECT_DOUBLE_EQ(DataRate::create(1, "MBy/s")->get(), 1000000);
    EXPECT_DOUBLE_EQ(DataRate::create(1, "GBy/s")->get(), 1000000000);
    EXPECT_DOUBLE_EQ((10_MB / 2_s).get<DataRateUnits::MB_s>(), 5);
    EXPECT_DOUBLE_EQ((10_MiB / 2_s).get<DataRateUnits::MiB_s>(), 5);
    EXPECT_DOUBLE_EQ((2_KB_s * 2_s).get<ByteSizeUnits::bytes>(), 4000);
    EXPECT_DOUBLE_EQ((2_KiB_s * 2_s).get<ByteSizeUnits::bytes>(), 4096);
}

TEST(UnitsTests, AutomaticByteFormattingKeepsOnePrefixFamily)
{
    EXPECT_EQ(stream_to_string(1000_bytes), "1000 bytes");
    EXPECT_EQ(stream_to_string(1024_bytes), "1 KiB");
    EXPECT_EQ(stream_to_string(1_MiB), "1 MiB");
    EXPECT_EQ(stream_to_string(1_GiB), "1 GiB");
    EXPECT_EQ(stream_to_string(1000_bytes_s), "1 kB/s");
    EXPECT_EQ(stream_to_string(1_KiB_s), "1.024 kB/s");
    EXPECT_EQ(stream_to_string(1_MB_s), "1 MB/s");
    EXPECT_EQ(stream_to_string(1_GB_s), "1 GB/s");
    EXPECT_EQ(stream_to_string(-1000_bytes_s), "-1 kB/s");
    EXPECT_EQ(stream_to_string(0_bytes_s), "0 bytes/s");
}

TEST(UnitsTests, UcumCodeCreateMatchesTypedCreationForEveryRegisteredUnit)
{
    pnm::meta::tuple::for_each<Quantities>([](auto quantity_index) {
        using Quantity = pnm::meta::tuple::type_t<decltype(quantity_index)::value, Quantities>;
        SCOPED_TRACE(Quantity::UnitsMeta::NAME);
        pnm::meta::tuple::for_each<typename Quantity::UnitsMeta::NestedTypes>([](auto unit_index) {
            using Unit = pnm::meta::tuple::type_t<decltype(unit_index)::value,
                                                  typename Quantity::UnitsMeta::NestedTypes>;
            constexpr auto name{ Quantity::UnitsMeta::NESTED_TYPE_NAMES[decltype(unit_index)::value] };
            constexpr auto unit{ pnm::meta::enumeration::from_string<Units>(name) };
            constexpr auto code{ pnm::units::ucumCode(*unit) };
            static_assert(code.has_value());
            static_assert(Unit::UCUM_CODE == *code);
            SCOPED_TRACE(name);
            SCOPED_TRACE(*code);
            for (const auto value : { -2.5, 0.0, 1.25, 42.0 }) {
                const auto result{ Quantity::create(value, *code) };
                ASSERT_TRUE(result.has_value());
                EXPECT_DOUBLE_EQ(result->get(), Quantity::template create<Unit>(value).get());
            }
        });
    });
}

TEST(UnitsTests, UcumCodeCreateRejectsEveryCodeFromOtherQuantities)
{
    pnm::meta::tuple::for_each<Quantities>([](auto quantity_index) {
        using Quantity = pnm::meta::tuple::type_t<decltype(quantity_index)::value, Quantities>;
        SCOPED_TRACE(Quantity::UnitsMeta::NAME);
        for (const auto unit : pnm::meta::enumeration::enumerators<Units>()) {
            const auto code{ pnm::units::ucumCode(unit) };
            ASSERT_TRUE(code);
            const auto belongs{ std::ranges::any_of(Quantity::UnitsMeta::NESTED_TYPE_NAMES,
                                                    [&code](std::string_view name) {
                return pnm::units::ucumCode(*pnm::meta::enumeration::from_string<Units>(name)) == code;
            }) };
            SCOPED_TRACE(*code);
            EXPECT_EQ(Quantity::create(1.0, *code).has_value(), belongs);
        }
    });
}

TEST(UnitsTests, UcumCodeCreateResolvesSharedCodesWithinQuantity)
{
    const auto unitless{ Unitless::create(2.5, "1") };
    const auto ratio{ Ratio::create(2.5, "1") };
    ASSERT_TRUE(unitless);
    ASSERT_TRUE(ratio);
    EXPECT_DOUBLE_EQ(unitless->get(), 2.5);
    EXPECT_DOUBLE_EQ(ratio->get(), 2.5);
    EXPECT_FALSE(Distance::create(2.5, "1"));
    EXPECT_FALSE(Unitless::create(50.0, "%"));
}

TEST(UnitsTests, UcumCodeCreateRequiresAnExactRegisteredCode)
{
    using namespace std::string_view_literals;
    for (const auto code : { ""sv, "???"sv, "KM"sv, "Km"sv, " km"sv, "km "sv, "kmx"sv, "km\0"sv, "µm"sv }) {
        SCOPED_TRACE(code);
        EXPECT_FALSE(Distance::create(1.0, code));
    }
    pnm::meta::tuple::for_each<Quantities>([](auto index) {
        using Quantity = pnm::meta::tuple::type_t<index, Quantities>;
        SCOPED_TRACE(Quantity::UnitsMeta::NAME);
        EXPECT_FALSE(Quantity::create(1.0, ""));
    });
    // Readable symbols are distinct from the registered UCUM expressions.
    EXPECT_FALSE(ByteSize::create(1.0, "B"));
    EXPECT_FALSE(ByteSize::create(1.0, "KiB"));
    EXPECT_FALSE(Temperature::create(1.0, "C"));
    EXPECT_FALSE(Temperature::create(1.0, "°C"));
    // This lookup does not normalize equivalent expressions or parse annotations.
    EXPECT_FALSE(Acceleration::create(1.0, "m.s-2"));
    EXPECT_FALSE(AngularVelocity::create(1.0, "circ.min-1"));
    EXPECT_FALSE(Unitless::create(1.0, "{count}"));

    const std::array input{ 'k', 'm', 'x' };
    const auto distance{ Distance::create(2.5, std::string_view{ input.data(), 2 }) };
    ASSERT_TRUE(distance);
    EXPECT_DOUBLE_EQ(distance->get(), 2500.0);
}

TEST(UnitsTests, UcumCodeCreateRejectsLegacyCodes)
{
    EXPECT_FALSE(Distance::create(1.0, "KMT"));
    EXPECT_FALSE(Temperature::create(1.0, "FAH"));
    EXPECT_FALSE(ByteSize::create(1.0, "E64"));
    EXPECT_FALSE(Ratio::create(1.0, "C62"));
    EXPECT_FALSE(pnm::units::Jerk::create(1.0, "P03D03443"));
    EXPECT_FALSE(DataRate::create(1.0, "P6588AC2B"));
}

TEST(UnitsTests, UcumCodeCreateSupportsCompoundUnits)
{
    using namespace pnm::units;
    const auto jerk{ Jerk::create(2.5, "mm/s3") };
    ASSERT_TRUE(jerk);
    EXPECT_DOUBLE_EQ(jerk->get(), 0.0025);

    const auto acceleration{ AngularAcceleration::create(60.0, "circ/(min.s)") };
    ASSERT_TRUE(acceleration);
    EXPECT_DOUBLE_EQ(acceleration->get(), 2.0 * std::numbers::pi);

    const auto angular_jerk{ AngularJerk::create(60.0, "circ/(min.s2)") };
    ASSERT_TRUE(angular_jerk);
    EXPECT_DOUBLE_EQ(angular_jerk->get(), 2.0 * std::numbers::pi);

    const auto lead{ TransmissionLead::create(1.0, "mm/circ") };
    ASSERT_TRUE(lead);
    EXPECT_DOUBLE_EQ(lead->get(), 0.001 / (2.0 * std::numbers::pi));

    const auto rate{ DataRate::create(1.0, "KiBy/s") };
    ASSERT_TRUE(rate);
    EXPECT_DOUBLE_EQ(rate->get(), 1024.0);

    const auto frequency{ Frequency::create(2.0, "mHz") };
    ASSERT_TRUE(frequency);
    EXPECT_DOUBLE_EQ(frequency->get(), 0.002);

    const auto frequency_rate{ FrequencyRate::create(2.0, "Hz/s") };
    ASSERT_TRUE(frequency_rate);
    EXPECT_DOUBLE_EQ(frequency_rate->get(), 2.0);

    const auto frequency_acceleration{ FrequencyAcceleration::create(3.0, "Hz/s2") };
    ASSERT_TRUE(frequency_acceleration);
    EXPECT_DOUBLE_EQ(frequency_acceleration->get(), 3.0);
}

TEST(UnitsTests, UcumCodeCreateAppliesOffsetsAndBinaryAndAngularScales)
{
    const auto temperature{ Temperature::create(212.0, "[degF]") };
    ASSERT_TRUE(temperature);
    EXPECT_NEAR(temperature->get(), 100.0, 1e-12);

    const auto storage{ ByteSize::create(1.0, "GiBy") };
    ASSERT_TRUE(storage);
    EXPECT_DOUBLE_EQ(storage->get(), 1073741824.0);

    const auto rotation{ AngularVelocity::create(60.0, "circ/min") };
    ASSERT_TRUE(rotation);
    EXPECT_DOUBLE_EQ(rotation->get(), 2.0 * std::numbers::pi);

    const auto percent{ Ratio::create(50.0, "%") };
    ASSERT_TRUE(percent);
    EXPECT_DOUBLE_EQ(percent->get(), 0.5);
}

TEST(UnitsTests, UcumCodeCreatePreservesNonFiniteValuesForValidCodes)
{
    const auto nan{ Distance::create(std::numeric_limits<double>::quiet_NaN(), "km") };
    ASSERT_TRUE(nan);
    EXPECT_TRUE(nan->isNaN());
    for (const auto value :
         { std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() }) {
        const auto result{ Distance::create(value, "km") };
        ASSERT_TRUE(result);
        EXPECT_EQ(result->get(), value);
    }
}

TEST(UnitsTests, ByteSizeConvertsAcrossRegisteredUnits)
{
    const auto size = 2.0_KiB;

    EXPECT_EQ(size.get(), static_cast<size_t>(2048));
    EXPECT_EQ(size.get<ByteSizeUnits::bytes>(), static_cast<size_t>(2048));
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::KiB>(), 2.0);
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MiB>(), 2.0 / 1024.0);
}

TEST(UnitsTests, DistanceCreateAndGetUseBaseMeters)
{
    const auto distance = Distance::create<DistanceUnits::km>(3.5);

    EXPECT_DOUBLE_EQ(distance.get(), 3500.0);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::m>(), 3500.0);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::cm>(), 350000.0);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::km>(), 3.5);
}

TEST(UnitsTests, AreaConversionsUseSquareMetersAsBase)
{
    const auto area = 250.0_m2;

    EXPECT_DOUBLE_EQ(area.get(), 250.0);
    EXPECT_DOUBLE_EQ(area.get<AreaUnits::m2>(), 250.0);
    EXPECT_DOUBLE_EQ(area.get<AreaUnits::cm2>(), 2500000.0);
    EXPECT_DOUBLE_EQ(area.get<AreaUnits::km2>(), 0.00025);
}

TEST(UnitsTests, TimeLiteralsRoundTripThroughSeconds)
{
    const auto duration = 2.0_h;

    EXPECT_DOUBLE_EQ(duration.get(), 7200.0);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::min>(), 120.0);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::ms>(), 7200000.0);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::h>(), 2.0);
}

TEST(UnitsTests, TimeNowConvertsCustomClockDurationToSeconds)
{
    const auto time = Time::now<FixedClock>();
    EXPECT_DOUBLE_EQ(time.get<TimeUnits::s>(), 1.234);
    EXPECT_DOUBLE_EQ(time.get<TimeUnits::ms>(), 1234.0);
}

TEST(UnitsTests, TimeNowDefaultsToSteadyClock)
{
    const Time before = std::chrono::steady_clock::now().time_since_epoch();
    const auto time = Time::now();
    const Time after = std::chrono::steady_clock::now().time_since_epoch();
    EXPECT_GE(time, before);
    EXPECT_LE(time, after);
}

TEST(UnitsTests, ChronoDurationsConvertImplicitlyToTime)
{
    const Time duration = 1500ms;
    const Time fractional_duration = 2.5s;

    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 1.5);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::ms>(), 1500.0);
    EXPECT_DOUBLE_EQ(fractional_duration.get<TimeUnits::s>(), 2.5);
}

TEST(UnitsTests, TimeConvertsToChronoDurations)
{
    const auto duration = 1.5_s;
    const std::chrono::duration<double> seconds = duration;
    const std::chrono::duration<double, std::milli> floating_milliseconds = duration;
    const auto milliseconds = static_cast<std::chrono::milliseconds>(duration);
    const auto nanoseconds = duration.toChrono<std::chrono::nanoseconds>();
    const auto default_duration = duration.toChrono();

    static_assert(
      std::is_same_v<std::remove_cvref_t<decltype(default_duration)>, std::chrono::duration<double>>);
    EXPECT_DOUBLE_EQ(seconds.count(), 1.5);
    EXPECT_DOUBLE_EQ(floating_milliseconds.count(), 1500.0);
    EXPECT_EQ(milliseconds.count(), 1500);
    EXPECT_EQ(nanoseconds.count(), 1500000000);
    EXPECT_DOUBLE_EQ(default_duration.count(), 1.5);
}

TEST(UnitsTests, TimeAndChronoDurationsSupportMixedArithmeticAndComparison)
{
    auto duration = 1.25_s;
    duration += 250ms;
    duration -= 500ms;
    duration = 1s;

    const auto time_sum = duration + 250ms;
    const auto chrono_sum = 250ms + duration;
    const auto time_difference = duration - 250ms;
    const auto chrono_difference = 2s - duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(time_sum)>, Time>);
    static_assert(std::is_same_v<std::remove_cvref_t<decltype(chrono_sum)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 1.0);
    EXPECT_DOUBLE_EQ(time_sum.get<TimeUnits::s>(), 1.25);
    EXPECT_DOUBLE_EQ(chrono_sum.get<TimeUnits::s>(), 1.25);
    EXPECT_DOUBLE_EQ(time_difference.get<TimeUnits::s>(), 0.75);
    EXPECT_DOUBLE_EQ(chrono_difference.get<TimeUnits::s>(), 1.0);
    EXPECT_TRUE(duration == 1s);
    EXPECT_TRUE(1s == duration);
    EXPECT_TRUE(duration < 1500ms);
    EXPECT_TRUE(500ms < duration);
}

TEST(UnitsTests, DataRateConversionsUseBytesPerSecondAsBase)
{
    const auto rate = 12.5_MiB_s;

    EXPECT_DOUBLE_EQ(rate.get(), 13107200.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::bytes_s>(), 13107200.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::KiB_s>(), 12800.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::GiB_s>(), 12.5 / 1024.0);
}

TEST(UnitsTests, MassConversionsUseGramsAsBase)
{
    const auto mass = 2500.0_g;

    EXPECT_DOUBLE_EQ(mass.get(), 2500.0);
    EXPECT_DOUBLE_EQ(mass.get<MassUnits::g>(), 2500.0);
    EXPECT_DOUBLE_EQ(mass.get<MassUnits::kg>(), 2.5);
    EXPECT_DOUBLE_EQ(mass.get<MassUnits::mg>(), 2500000.0);
    EXPECT_DOUBLE_EQ(mass.get<MassUnits::t>(), 0.0025);
}

TEST(UnitsTests, TemperatureConversionsUseCelsiusAsBase)
{
    const auto temperature = 30.0_C;

    EXPECT_DOUBLE_EQ(temperature.get(), 30.0);
    EXPECT_DOUBLE_EQ(temperature.get<TemperatureUnits::K>(), 303.15);
    EXPECT_DOUBLE_EQ(temperature.get<TemperatureUnits::F>(), 86.0);
}

TEST(UnitsTests, TemperatureConversionsFromKelvin)
{
    const auto temperature = 295.5_K;

    EXPECT_DOUBLE_EQ(temperature.get<TemperatureUnits::K>(), 295.5);
    EXPECT_NEAR(temperature.get<TemperatureUnits::C>(), 22.35, 1e-11);
    EXPECT_DOUBLE_EQ(temperature.get<TemperatureUnits::F>(), 72.23);
}

TEST(UnitsTests, CurrentConversionsUseAmperesAsBase)
{
    const auto current = 1500.0_mA;

    EXPECT_DOUBLE_EQ(current.get(), 1.5);
    EXPECT_DOUBLE_EQ(current.get<CurrentUnits::A>(), 1.5);
    EXPECT_DOUBLE_EQ(current.get<CurrentUnits::uA>(), 1500000.0);
    EXPECT_DOUBLE_EQ(current.get<CurrentUnits::kA>(), 0.0015);
}

TEST(UnitsTests, VoltageConversionsUseVoltsAsBase)
{
    const auto voltage = 2.5_kV;

    EXPECT_DOUBLE_EQ(voltage.get(), 2500.0);
    EXPECT_DOUBLE_EQ(voltage.get<VoltageUnits::V>(), 2500.0);
    EXPECT_DOUBLE_EQ(voltage.get<VoltageUnits::mV>(), 2500000.0);
    EXPECT_DOUBLE_EQ(voltage.get<VoltageUnits::MV>(), 0.0025);
}

TEST(UnitsTests, ForceConversionsUseNewtonsAsBase)
{
    const auto force = 12.5_kN;

    EXPECT_DOUBLE_EQ(force.get(), 12500.0);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::N>(), 12500.0);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::mN>(), 12500000.0);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::MN>(), 0.0125);
}

TEST(UnitsTests, EnergyConversionsUseJoulesAsBase)
{
    const auto energy = 2.0_kWh;

    EXPECT_DOUBLE_EQ(energy.get(), 7200000.0);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::J>(), 7200000.0);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::kJ>(), 7200.0);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::MJ>(), 7.2);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::Wh>(), 2000.0);
}

TEST(UnitsTests, PowerConversionsUseWattsAsBase)
{
    const auto power = 1.5_kW;

    EXPECT_DOUBLE_EQ(power.get(), 1500.0);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::W>(), 1500.0);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::mW>(), 1500000.0);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::MW>(), 0.0015);
}

TEST(UnitsTests, PressureConversionsUseBarAsBase)
{
    const auto pressure = 1.0_atm;

    EXPECT_DOUBLE_EQ(pressure.get(), 1.01325);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::Pa>(), 101325.0);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::hPa>(), 1013.25);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::mbar>(), 1013.25);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::kPa>(), 101.325);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::bar>(), 1.01325);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::MPa>(), 0.101325);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::kbar>(), 0.00101325);

    EXPECT_DOUBLE_EQ((1.0_hPa).get<PressureUnits::Pa>(), 100.0);
    EXPECT_DOUBLE_EQ((1.0_mbar).get<PressureUnits::Pa>(), 100.0);
    EXPECT_DOUBLE_EQ((1.0_kPa).get<PressureUnits::bar>(), 0.01);
    EXPECT_DOUBLE_EQ((1.0_MPa).get<PressureUnits::bar>(), 10.0);
    EXPECT_DOUBLE_EQ((1.0_kbar).get<PressureUnits::Pa>(), 100000000.0);
}

TEST(UnitsTests, FrequencyConversionsUseHertzAsBase)
{
    const auto frequency = 2.5_kHz;

    EXPECT_DOUBLE_EQ(frequency.get(), 2500.0);
    EXPECT_DOUBLE_EQ(frequency.get<FrequencyUnits::Hz>(), 2500.0);
    EXPECT_DOUBLE_EQ(frequency.get<FrequencyUnits::mHz>(), 2500000.0);
    EXPECT_DOUBLE_EQ(frequency.get<FrequencyUnits::MHz>(), 0.0025);
}

TEST(UnitsTests, VelocityConversionsUseMetersPerSecondAsBase)
{
    const auto speed = 36.0_km_h;

    EXPECT_DOUBLE_EQ(speed.get(), 10.0);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::m_s>(), 10.0);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::km_h>(), 36.0);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::mm_s>(), 10000.0);
}

TEST(UnitsTests, AccelerationConversionsUseMetersPerSecondSquaredAsBase)
{
    const auto acceleration = 9.81_m_s2;

    EXPECT_DOUBLE_EQ(acceleration.get(), 9.81);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::m_s2>(), 9.81);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::mm_s2>(), 9810.0);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::km_h2>(), 127137.6);
}

TEST(UnitsTests, SuffixReturnsTheRequestedUnitsName)
{
    static_assert(Distance::suffix<DistanceUnits::mm>() == "mm");
    static_assert(Distance::suffix<DistanceUnits::um>() == "µm");
    static_assert(Acceleration::suffix<AccelerationUnits::m_s2>() == "m/s²");
    static_assert(AngularVelocity::suffix<AngularVelocityUnits::deg_s>() == "deg/s");
    static_assert(Temperature::suffix<TemperatureUnits::C>() == "°C");
    static_assert(Ratio::suffix<RatioUnits::fraction>().empty());
    static_assert(Unitless::suffix<UnitlessUnits::unitless>().empty());
    EXPECT_EQ(Distance::suffix<DistanceUnits::m>(), "m");
    EXPECT_EQ(Time::suffix<TimeUnits::s>(), "s");
    EXPECT_EQ(Distance::suffix<const DistanceUnits::mm&>(), "mm");
    EXPECT_EQ(Pressure::suffix<PressureUnits::hPa>(), "hPa");
    EXPECT_EQ(Pressure::suffix<PressureUnits::mbar>(), "mbar");
    EXPECT_EQ((1500_m).suffix<DistanceUnits::m>(), "m");
    EXPECT_EQ((1500_m).suffix<DistanceUnits::km>(), "km");
    EXPECT_EQ(Velocity::suffix<VelocityUnits::m_s>(), "m/s");
    EXPECT_EQ(Velocity::suffix<VelocityUnits::km_h>(), "km/h");
    EXPECT_EQ(DataRate::suffix<DataRateUnits::MiB_s>(), "MiB/s");
}

TEST(UnitsTests, SuffixPreservesCustomSymbolsAndEmptySuffixes)
{
    EXPECT_EQ(Distance::suffix<DistanceUnits::um>(), "µm");
    EXPECT_EQ(Current::suffix<CurrentUnits::uA>(), "µA");
    EXPECT_EQ(Area::suffix<AreaUnits::m2>(), "m²");
    EXPECT_EQ(Acceleration::suffix<AccelerationUnits::m_s2>(), "m/s²");
    EXPECT_EQ(Temperature::suffix<TemperatureUnits::C>(), "°C");
    EXPECT_EQ(Temperature::suffix<TemperatureUnits::F>(), "°F");
    EXPECT_EQ(Angle::suffix<AngleUnits::deg>(), "°");
    EXPECT_EQ(Ratio::suffix<RatioUnits::percent>(), "%");
    EXPECT_TRUE(Ratio::suffix<RatioUnits::fraction>().empty());
    EXPECT_TRUE(Unitless::suffix<UnitlessUnits::unitless>().empty());
}

TEST(UnitsTests, StreamInsertionPrintsSelectedUnits)
{
    EXPECT_EQ(stream_to_string(2.0_h), "2 h");
    EXPECT_EQ(stream_to_string(1536.0_bytes), "1.5 KiB");
    EXPECT_EQ(stream_to_string(1500.0_m), "1.5 km");
    EXPECT_EQ(stream_to_string(-1500.0_m), "-1.5 km");
    EXPECT_EQ(stream_to_string(0.0_m), "0 m");
    EXPECT_EQ(stream_to_string(1500.0_kg), "1.5 t");
    EXPECT_EQ(stream_to_string(2.0_kWh), "2 kWh");
    EXPECT_EQ(stream_to_string(101325.0_Pa), "1 atm");
    EXPECT_EQ(stream_to_string(1.0_mbar), "1 mbar");
    EXPECT_EQ(stream_to_string(30.0_C), "30 °C");
    EXPECT_EQ(stream_to_string(295.5_K), "22.35 °C");
    EXPECT_EQ(stream_to_string(86.0_F), "30 °C");
    EXPECT_EQ(stream_to_string(2000.0_Hz), "2 kHz");
}

TEST(UnitsTests, StreamInsertionUsesCorrectSuffixWhenUnitOrderDiffersFromScaleOrder)
{
    EXPECT_EQ(stream_to_string(0.5_m_s), "1.8 km/h");
    EXPECT_EQ(stream_to_string(0.5_m_s2), "500 mm/s²");
}

TEST(UnitsTests, StreamInsertionDisplaysCompoundUnitsWithSlashes)
{
    EXPECT_EQ(stream_to_string(20.0_m_s), "20 m/s");
    EXPECT_EQ(stream_to_string(9.81_m_s2), "9.81 m/s²");
    EXPECT_EQ(stream_to_string(3.0_MB_s), "3 MB/s");
}

TEST(UnitsTests, StreamInsertionPrintsUnicodeSuffixes)
{
    EXPECT_EQ(stream_to_string(2_um), "2 µm");
    EXPECT_EQ(stream_to_string(2_us), "2 µs");
    EXPECT_EQ(stream_to_string(2_uA), "2 µA");
    EXPECT_EQ(stream_to_string(2_mm2), "2 mm²");
    EXPECT_EQ(stream_to_string(2_m2), "2 m²");
    EXPECT_EQ(stream_to_string(45_deg), "45 °");
}

TEST(UnitsTests, TimeLiteralsUnaryPlusAndMinus)
{
    const auto duration1 = +1.5_min;
    const auto duration2 = -30.0_s;

    EXPECT_DOUBLE_EQ(duration1.get(), 90.0);
    EXPECT_DOUBLE_EQ(duration2.get(), -30.0);
}

TEST(UnitsTests, TimeLiteralsAddition)
{
    const auto duration1 = 2.0_h;
    const auto duration2 = 30.0_min;
    const auto total_duration = duration1 + duration2;

    EXPECT_DOUBLE_EQ(total_duration.get(), 9000.0);
}

TEST(UnitsTests, DistanceLiteralsSubtraction)
{
    const auto distance1 = 5.0_km;
    const auto distance2 = 500.0_m;
    const auto remaining_distance = distance1 - distance2;

    EXPECT_DOUBLE_EQ(remaining_distance.get(), 4500.0);
}

TEST(UnitsTests, ByteSizeLiteralsAdditionAssignment)
{
    auto size = 50.0_KiB;
    size += 10.0_KiB;

    EXPECT_DOUBLE_EQ(size.get(), 61440.0);
}

TEST(UnitsTests, VelocityLiteralsSubtractionAssignment)
{
    auto speed = 100.0_km_h;
    speed -= 20.0_km_h;

    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::km_h>(), 80.0);
}

TEST(UnitsTests, TimeLiteralsComparison)
{
    const auto duration1 = 1.0_min;
    const auto duration2 = 60.0_s;

    EXPECT_TRUE(duration1 == duration2);
    EXPECT_FALSE(duration1 != duration2);
    EXPECT_FALSE(duration1 < duration2);
    EXPECT_FALSE(duration1 > duration2);
    EXPECT_TRUE(duration1 <= duration2);
    EXPECT_TRUE(duration1 >= duration2);
}

TEST(UnitsTests, DistanceScalarMultiplication)
{
    const auto distance = 3.0_km;
    const auto scaled_distance = 2.0 * distance;

    EXPECT_DOUBLE_EQ(scaled_distance.get(), 6000.0);
}

TEST(UnitsTests, DistanceScalarMultiplicationAssignment)
{
    auto distance = 3.0_km;
    distance *= 2.0;

    EXPECT_DOUBLE_EQ(distance.get(), 6000.0);
}

TEST(UnitsTests, VelocityRatioDivision)
{
    const auto speed = 90.0_km_h;
    const auto speed_ratio = speed / 20.0_km_h;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(speed_ratio)>, double>);
    EXPECT_DOUBLE_EQ(speed_ratio, 4.5);
}

TEST(UnitsTests, VelocityScalarDivisionAssignment)
{
    auto speed = 90.0_km_h;
    speed /= 2.0;

    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::km_h>(), 45.0);
}

TEST(UnitsTests, DistanceDividedByTimeProducesVelocity)
{
    const auto distance = 100.0_m;
    const auto duration = 4.0_s;
    const auto speed = distance / duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(speed)>, Velocity>);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::m_s>(), 25.0);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::km_h>(), 90.0);
}

TEST(UnitsTests, DistanceTimesDistanceProducesArea)
{
    const auto length = 12.0_m;
    const auto width = 4.0_m;
    const auto area = length * width;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(area)>, Area>);
    EXPECT_DOUBLE_EQ(area.get<AreaUnits::m2>(), 48.0);
    EXPECT_DOUBLE_EQ(area.get<AreaUnits::cm2>(), 480000.0);
}

TEST(UnitsTests, AreaDividedByDistanceProducesDistance)
{
    const auto area = 144.0_m2;
    const auto width = 12.0_m;
    const auto length = area / width;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(length)>, Distance>);
    EXPECT_DOUBLE_EQ(length.get<DistanceUnits::m>(), 12.0);
    EXPECT_DOUBLE_EQ(length.get<DistanceUnits::cm>(), 1200.0);
}

TEST(UnitsTests, VelocityTimesTimeProducesDistance)
{
    const auto speed = 90.0_km_h;
    const auto duration = 2.0_h;
    const auto distance = speed * duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(distance)>, Distance>);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::m>(), 180000.0);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::km>(), 180.0);
}

TEST(UnitsTests, TimeTimesVelocityProducesDistance)
{
    const auto duration = 30.0_min;
    const auto speed = 36.0_km_h;
    const auto distance = duration * speed;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(distance)>, Distance>);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::m>(), 18000.0);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::km>(), 18.0);
}

TEST(UnitsTests, DistanceDividedByVelocityProducesTime)
{
    const auto distance = 180.0_km;
    const auto speed = 90.0_km_h;
    const auto duration = distance / speed;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(duration)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 7200.0);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::h>(), 2.0);
}

TEST(UnitsTests, VelocityDividedByTimeProducesAcceleration)
{
    const auto speed = 20.0_m_s;
    const auto duration = 4.0_s;
    const auto acceleration = speed / duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(acceleration)>, Acceleration>);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::m_s2>(), 5.0);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::km_h2>(), 64800.0);
}

TEST(UnitsTests, AccelerationTimesTimeProducesVelocity)
{
    const auto acceleration = 3.0_m_s2;
    const auto duration = 4.0_s;
    const auto speed = acceleration * duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(speed)>, Velocity>);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::m_s>(), 12.0);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::km_h>(), 43.2);
}

TEST(UnitsTests, TimeTimesAccelerationProducesVelocity)
{
    const auto duration = 3.0_s;
    const auto acceleration = 2.5_m_s2;
    const auto speed = duration * acceleration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(speed)>, Velocity>);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::m_s>(), 7.5);
    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::km_h>(), 27.0);
}

TEST(UnitsTests, VelocityDividedByAccelerationProducesTime)
{
    const auto speed = 27.77777777777778_m_s;
    const auto acceleration = 3.4722222222222223_m_s2;
    const auto duration = speed / acceleration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(duration)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 8.0);
}

TEST(UnitsTests, MassTimesAccelerationProducesForce)
{
    const auto mass = 2.0_kg;
    const auto acceleration = 9.81_m_s2;
    const auto force = mass * acceleration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(force)>, Force>);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::N>(), 19.62);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::kN>(), 0.01962);
}

TEST(UnitsTests, AccelerationTimesMassProducesForce)
{
    const auto acceleration = 2.5_m_s2;
    const auto mass = 8.0_kg;
    const auto force = acceleration * mass;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(force)>, Force>);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::N>(), 20.0);
}

TEST(UnitsTests, ForceDividedByMassProducesAcceleration)
{
    const auto force = 20.0_N;
    const auto mass = 4.0_kg;
    const auto acceleration = force / mass;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(acceleration)>, Acceleration>);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::m_s2>(), 5.0);
}

TEST(UnitsTests, ForceDividedByAccelerationProducesMass)
{
    const auto force = 24.0_N;
    const auto acceleration = 3.0_m_s2;
    const auto mass = force / acceleration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(mass)>, Mass>);
    EXPECT_DOUBLE_EQ(mass.get<MassUnits::kg>(), 8.0);
}

TEST(UnitsTests, ForceTimesDistanceProducesEnergy)
{
    const auto force = 10.0_N;
    const auto distance = 5.0_m;
    const auto energy = force * distance;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(energy)>, Energy>);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::J>(), 50.0);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::mJ>(), 50000.0);
}

TEST(UnitsTests, DistanceTimesForceProducesEnergy)
{
    const auto distance = 12.0_m;
    const auto force = 1.5_kN;
    const auto energy = distance * force;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(energy)>, Energy>);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::kJ>(), 18.0);
}

TEST(UnitsTests, EnergyDividedByForceProducesDistance)
{
    const auto energy = 90.0_J;
    const auto force = 15.0_N;
    const auto distance = energy / force;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(distance)>, Distance>);
    EXPECT_DOUBLE_EQ(distance.get<DistanceUnits::m>(), 6.0);
}

TEST(UnitsTests, EnergyDividedByDistanceProducesForce)
{
    const auto energy = 24.0_kJ;
    const auto distance = 3.0_m;
    const auto force = energy / distance;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(force)>, Force>);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::kN>(), 8.0);
}

TEST(UnitsTests, EnergyDividedByTimeProducesPower)
{
    const auto energy = 7200.0_J;
    const auto duration = 2.0_s;
    const auto power = energy / duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(power)>, Power>);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::W>(), 3600.0);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::kW>(), 3.6);
}

TEST(UnitsTests, PowerTimesTimeProducesEnergy)
{
    const auto power = 2.0_kW;
    const auto duration = 30.0_min;
    const auto energy = power * duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(energy)>, Energy>);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::MJ>(), 3.6);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::kWh>(), 1.0);
}

TEST(UnitsTests, TimeTimesPowerProducesEnergy)
{
    const auto duration = 0.5_h;
    const auto power = 4.0_kW;
    const auto energy = duration * power;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(energy)>, Energy>);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::kWh>(), 2.0);
}

TEST(UnitsTests, EnergyDividedByPowerProducesTime)
{
    const auto energy = 2.0_kWh;
    const auto power = 500.0_W;
    const auto duration = energy / power;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(duration)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::h>(), 4.0);
}

TEST(UnitsTests, ForceDividedByAreaProducesPressure)
{
    const auto force = 1000.0_N;
    const auto area = 2.0_m2;
    const auto pressure = force / area;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(pressure)>, Pressure>);
    EXPECT_DOUBLE_EQ(pressure.get(), 0.005);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::Pa>(), 500.0);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::hPa>(), 5.0);
    EXPECT_DOUBLE_EQ(pressure.get<PressureUnits::mbar>(), 5.0);
}

TEST(UnitsTests, PressureTimesAreaProducesForce)
{
    const auto pressure = 250.0_kPa;
    const auto area = 2.0_m2;
    const auto force = pressure * area;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(force)>, Force>);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::kN>(), 500.0);
}

TEST(UnitsTests, AreaTimesPressureProducesForce)
{
    const auto area = 0.5_m2;
    const auto pressure = 1.0_bar;
    const auto force = area * pressure;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(force)>, Force>);
    EXPECT_DOUBLE_EQ(force.get<ForceUnits::kN>(), 50.0);
}

TEST(UnitsTests, ForceDividedByPressureProducesArea)
{
    const auto force = 10.0_kN;
    const auto pressure = 20.0_kPa;
    const auto area = force / pressure;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(area)>, Area>);
    EXPECT_DOUBLE_EQ(area.get<AreaUnits::m2>(), 0.5);
}

TEST(UnitsTests, VoltageTimesCurrentProducesPower)
{
    const auto voltage = 12.0_V;
    const auto current = 2.0_A;
    const auto power = voltage * current;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(power)>, Power>);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::W>(), 24.0);
}

TEST(UnitsTests, CurrentTimesVoltageProducesPower)
{
    const auto current = 3.0_A;
    const auto voltage = 230.0_V;
    const auto power = current * voltage;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(power)>, Power>);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::W>(), 690.0);
}

TEST(UnitsTests, PowerDividedByVoltageProducesCurrent)
{
    const auto power = 240.0_W;
    const auto voltage = 12.0_V;
    const auto current = power / voltage;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(current)>, Current>);
    EXPECT_DOUBLE_EQ(current.get<CurrentUnits::A>(), 20.0);
}

TEST(UnitsTests, PowerDividedByCurrentProducesVoltage)
{
    const auto power = 460.0_W;
    const auto current = 2.0_A;
    const auto voltage = power / current;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(voltage)>, Voltage>);
    EXPECT_DOUBLE_EQ(voltage.get<VoltageUnits::V>(), 230.0);
}

TEST(UnitsTests, ScalarDividedByTimeProducesFrequency)
{
    const auto frequency = 1.0 / 2.0_s;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(frequency)>, Frequency>);
    EXPECT_DOUBLE_EQ(frequency.get<FrequencyUnits::Hz>(), 0.5);
    EXPECT_DOUBLE_EQ(frequency.get<FrequencyUnits::mHz>(), 500.0);
}

TEST(UnitsTests, ScalarDividedByFrequencyProducesTime)
{
    const auto duration = 1.0 / 2.0_Hz;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(duration)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 0.5);
}

TEST(UnitsTests, RatioDividedByTimeProducesFrequency)
{
    static_assert(std::is_same_v<decltype(50_percent / 2_s), Frequency>);
    static_assert(50_percent / 2_s == 0.25_Hz);
    EXPECT_DOUBLE_EQ((50_percent / 2_s).get<FrequencyUnits::Hz>(), 0.25);
    EXPECT_DOUBLE_EQ((1_fraction / 500_ms).get<FrequencyUnits::Hz>(), 2.0);
    EXPECT_DOUBLE_EQ((150_percent / 1_min).get<FrequencyUnits::mHz>(), 25.0);
    EXPECT_DOUBLE_EQ((0_percent / 2_s).get(), 0.0);
    EXPECT_DOUBLE_EQ((-50_percent / 2_s).get(), -0.25);
}

TEST(UnitsTests, RatioDividedByChronoDurationUsesTimeRelation)
{
    static_assert(std::is_same_v<decltype(50_percent / 2s), Frequency>);
    static_assert(50_percent / 2s == 0.25_Hz);
    EXPECT_EQ(50_percent / 2s, 50_percent / 2_s);
    EXPECT_EQ(1_fraction / 500ms, 1_fraction / 500_ms);
    EXPECT_EQ(150_percent / 1min, 150_percent / 1_min);
    EXPECT_EQ(50_percent / 0.5s, 1_Hz);
}

TEST(UnitsTests, TimeTimesFrequencyProducesScalar)
{
    const auto cycles = 2.0_s * 0.5_Hz;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(cycles)>, double>);
    EXPECT_DOUBLE_EQ(cycles, 1.0);
}

TEST(UnitsTests, FrequencyTimesTimeProducesScalar)
{
    const auto cycles = 60.0_Hz * 0.5_s;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(cycles)>, double>);
    EXPECT_DOUBLE_EQ(cycles, 30.0);
}

TEST(UnitsTests, ByteSizeDividedByTimeProducesDataRate)
{
    const auto size = 90.0_MiB;
    const auto duration = 30.0_s;
    const auto rate = size / duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(rate)>, DataRate>);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::MiB_s>(), 3.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::bytes_s>(), 3145728.0);
}

TEST(UnitsTests, DataRateTimesTimeProducesByteSize)
{
    const auto rate = 25.0_MiB_s;
    const auto duration = 4.0_s;
    const auto size = rate * duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(size)>, ByteSize>);
    EXPECT_EQ(size.get<ByteSizeUnits::bytes>(), static_cast<size_t>(104857600));
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MiB>(), 100.0);
}

TEST(UnitsTests, TimeTimesDataRateProducesByteSize)
{
    const auto duration = 2.0_s;
    const auto rate = 8.0_MiB_s;
    const auto size = duration * rate;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(size)>, ByteSize>);
    EXPECT_EQ(size.get<ByteSizeUnits::bytes>(), static_cast<size_t>(16777216));
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MiB>(), 16.0);
}

TEST(UnitsTests, ByteSizeDividedByDataRateProducesTime)
{
    const auto size = 300.0_MiB;
    const auto rate = 50.0_MiB_s;
    const auto duration = size / rate;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(duration)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 6.0);
}

TEST(UnitsTests, ChronoDurationsParticipateInTimeBasedQuantityRelations)
{
    const auto speed = 100.0_m / 4s;
    const auto acceleration = 20.0_m_s / 4s;
    const auto power = 100.0_J / 2s;
    const auto rate = 8.0_MiB / 2s;
    const auto distance_from_right = 10.0_m_s * 2500ms;
    const auto distance_from_left = 2500ms * 10.0_m_s;
    const auto velocity = 2.0_m_s2 * 1500ms;
    const auto energy = 250ms * 100.0_W;
    const auto size = 1.5s * 2.0_MiB_s;
    const auto cycles_from_right = 2s * 0.5_Hz;
    const auto cycles_from_left = 0.5_Hz * 2s;

    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::m_s>(), 25.0);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::m_s2>(), 5.0);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::W>(), 50.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::MiB_s>(), 4.0);
    EXPECT_DOUBLE_EQ(distance_from_right.get<DistanceUnits::m>(), 25.0);
    EXPECT_DOUBLE_EQ(distance_from_left.get<DistanceUnits::m>(), 25.0);
    EXPECT_DOUBLE_EQ(velocity.get<VelocityUnits::m_s>(), 3.0);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::J>(), 25.0);
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MiB>(), 3.0);
    EXPECT_DOUBLE_EQ(cycles_from_right, 1.0);
    EXPECT_DOUBLE_EQ(cycles_from_left, 1.0);
}

TEST(UnitsTests, ChronoPeriodConvertsDirectlyToFrequency)
{
    const auto frequency_from_operator = 1.0 / 250ms;
    const auto frequency_from_helper = pnm::units::frequency_from_period(2s);
    const auto scaled_frequency = pnm::units::operator/(3.0, 500ms);

    EXPECT_DOUBLE_EQ(frequency_from_operator.get<FrequencyUnits::Hz>(), 4.0);
    EXPECT_DOUBLE_EQ(frequency_from_helper.get<FrequencyUnits::Hz>(), 0.5);
    EXPECT_DOUBLE_EQ(scaled_frequency.get<FrequencyUnits::Hz>(), 6.0);
}

TEST(UnitsTests, AngleLiterals)
{
    const auto angle1 = 90.0_deg;
    const auto angle2 = 10.0_deg;

    EXPECT_DOUBLE_EQ((angle1 + angle2).get<AngleUnits::deg>(), 100.0);
    EXPECT_DOUBLE_EQ(angle1.get(), std::numbers::pi / 2.0);
}

TEST(UnitsTests, PercentConversionsUseNormalizedRatioAsBase)
{
    EXPECT_DOUBLE_EQ((50_percent).get(), 0.5);
    EXPECT_DOUBLE_EQ((50_percent).get<RatioUnits::fraction>(), 0.5);
    EXPECT_DOUBLE_EQ((1_fraction).get<RatioUnits::percent>(), 100.0);
    EXPECT_DOUBLE_EQ(Ratio::create(0.25).get<RatioUnits::percent>(), 25.0);
    EXPECT_DOUBLE_EQ(Ratio::create<RatioUnits::percent>(75.0).get(), 0.75);
    EXPECT_DOUBLE_EQ((-25_percent).get(), -0.25);
    EXPECT_DOUBLE_EQ((150_percent).get(), 1.5);
    EXPECT_DOUBLE_EQ((0_percent).get(), 0.0);
    EXPECT_EQ(25_percent + 25_percent, 50_percent);
    EXPECT_EQ(100_percent - 25_percent, 75_percent);
}

TEST(UnitsTests, RatiosScaleQuantitiesInBothMultiplicationOrders)
{
    static_assert(std::is_same_v<decltype(2000_rpm * 50_percent), AngularVelocity>);
    static_assert(std::is_same_v<decltype(50_percent * 2000_rpm), AngularVelocity>);
    static_assert(std::is_same_v<decltype(2000_rpm / 50_percent), AngularVelocity>);
    EXPECT_DOUBLE_EQ((2000_rpm * 50_percent).get<AngularVelocityUnits::rpm>(), 1000.0);
    EXPECT_DOUBLE_EQ((50_percent * 2000_rpm).get<AngularVelocityUnits::rpm>(), 1000.0);
    EXPECT_DOUBLE_EQ((2000_rpm / 50_percent).get<AngularVelocityUnits::rpm>(), 4000.0);
    EXPECT_EQ(50_percent * 4_m, 2_m);
    EXPECT_EQ(4_m / 50_percent, 8_m);
    EXPECT_EQ(4_s * 50_percent, 2_s);
    EXPECT_EQ(4_m * 0_percent, 0_m);
    EXPECT_EQ(4_m * -25_percent, -1_m);
}

TEST(UnitsTests, RatiosSupportProductsScalarArithmeticAndExplicitQuotientConstruction)
{
    EXPECT_EQ(50_percent * 50_percent, 25_percent);
    EXPECT_DOUBLE_EQ(50_percent / 25_percent, 2.0);
    EXPECT_EQ(50_percent * 2.0, 1_fraction);
    EXPECT_EQ(2.0 * 50_percent, 1_fraction);
    EXPECT_EQ(50_percent / 2.0, 25_percent);
    EXPECT_EQ(Ratio::create(1_m / 4_m), 25_percent);
}

TEST(UnitsTests, RatioCompoundScalingReturnsQuantityReference)
{
    auto speed = 2000_rpm;
    EXPECT_EQ(&(speed *= 50_percent), &speed);
    EXPECT_DOUBLE_EQ(speed.get<AngularVelocityUnits::rpm>(), 1000.0);
    EXPECT_EQ(&(speed /= 25_percent), &speed);
    EXPECT_DOUBLE_EQ(speed.get<AngularVelocityUnits::rpm>(), 4000.0);

    auto ratio = 50_percent;
    EXPECT_EQ(&(ratio *= ratio), &ratio);
    EXPECT_EQ(ratio, 25_percent);
    EXPECT_EQ(&(ratio /= ratio), &ratio);
    EXPECT_EQ(ratio, 1_fraction);
}

TEST(UnitsTests, UnitlessValuesSupportExplicitConstructionAndBaseUnitAccess)
{
    EXPECT_DOUBLE_EQ(Unitless{}.get(), 0.0);
    EXPECT_DOUBLE_EQ((2_unitless).get<UnitlessUnits::unitless>(), 2.0);
    EXPECT_EQ(Unitless::create(0.5), 0.5_unitless);
    EXPECT_EQ(Unitless::create<UnitlessUnits::unitless>(-3.0), -3_unitless);
    EXPECT_EQ(Unitless::create(6_m / 3_m), 2_unitless);
    EXPECT_EQ(Unitless::create((50_percent).get()), 0.5_unitless);
    EXPECT_EQ(Ratio::create((0.5_unitless).get()), 50_percent);
}

TEST(UnitsTests, UnitlessArithmeticPreservesExistingSameTypeDivisionConvention)
{
    static_assert(std::is_same_v<decltype(2_unitless * 3_unitless), Unitless>);
    static_assert(std::is_same_v<decltype(2_unitless / 3_unitless), double>);
    static_assert(std::is_same_v<decltype(1.0 / 2_unitless), Unitless>);
    EXPECT_EQ(2_unitless + 0.5_unitless, 2.5_unitless);
    EXPECT_EQ(2_unitless - 0.5_unitless, 1.5_unitless);
    EXPECT_LT(-2_unitless, 0_unitless);
    EXPECT_EQ(2_unitless * -0.5_unitless, -1_unitless);
    EXPECT_DOUBLE_EQ(2_unitless / 0.5_unitless, 4.0);
    EXPECT_EQ(2_unitless * 3.0, 6_unitless);
    EXPECT_EQ(3.0 * 2_unitless, 6_unitless);
    EXPECT_EQ(2_unitless / 4.0, 0.5_unitless);
    EXPECT_EQ(1.0 / 2_unitless, 0.5_unitless);

    auto value = 2_unitless;
    EXPECT_EQ(&(value *= value), &value);
    EXPECT_EQ(value, 4_unitless);
    EXPECT_EQ(&(value /= value), &value);
    EXPECT_EQ(value, 1_unitless);
}

TEST(UnitsTests, UnitlessValuesScaleQuantitiesInBothMultiplicationOrders)
{
    static_assert(std::is_same_v<decltype(1.5_unitless * 8_V), Voltage>);
    static_assert(std::is_same_v<decltype(8_V * 1.5_unitless), Voltage>);
    static_assert(std::is_same_v<decltype(8_V / 2_unitless), Voltage>);
    EXPECT_EQ(1.5_unitless * 8_V, 12_V);
    EXPECT_EQ(8_V * 1.5_unitless, 12_V);
    EXPECT_EQ(8_V / 2_unitless, 4_V);
    EXPECT_EQ(4_m * 0_unitless, 0_m);
    EXPECT_EQ(-0.5_unitless * 4_m, -2_m);

    auto distance = 4_m;
    EXPECT_EQ(&(distance *= 1.5_unitless), &distance);
    EXPECT_EQ(distance, 6_m);
    EXPECT_EQ(&(distance /= 2_unitless), &distance);
    EXPECT_EQ(distance, 3_m);
}

TEST(UnitsTests, RatiosScaleUnitlessValuesInBothMultiplicationOrders)
{
    static_assert(std::is_same_v<decltype(50_percent * 2_unitless), Unitless>);
    static_assert(std::is_same_v<decltype(2_unitless * 50_percent), Unitless>);
    static_assert(std::is_same_v<decltype(2_unitless / 50_percent), Unitless>);
    static_assert(std::is_same_v<decltype(50_percent / 2_unitless), Ratio>);
    EXPECT_EQ(50_percent * 2_unitless, 1_unitless);
    EXPECT_EQ(2_unitless * 50_percent, 1_unitless);
    EXPECT_EQ(2_unitless / 50_percent, 4_unitless);
    EXPECT_EQ(50_percent / 2_unitless, 25_percent);

    auto value = 2_unitless;
    EXPECT_EQ(&(value *= 50_percent), &value);
    EXPECT_EQ(value, 1_unitless);
    EXPECT_EQ(&(value /= 25_percent), &value);
    EXPECT_EQ(value, 4_unitless);

    auto ratio = 50_percent;
    EXPECT_EQ(&(ratio *= 2_unitless), &ratio);
    EXPECT_EQ(ratio, 1_fraction);
    EXPECT_EQ(&(ratio /= 4_unitless), &ratio);
    EXPECT_EQ(ratio, 25_percent);
}

TEST(UnitsTests, UnitlessValuesSupportTimeAndFrequencyRelations)
{
    static_assert(std::is_same_v<decltype(4_unitless / 2_s), Frequency>);
    static_assert(std::is_same_v<decltype(4_unitless / 2_Hz), Time>);
    EXPECT_EQ(4_unitless / 2_s, 2_Hz);
    EXPECT_EQ(4_unitless / 2_Hz, 2_s);
    EXPECT_EQ(1_unitless / 250_ms, 4_Hz);
    EXPECT_EQ(1_unitless / 1_kHz, 1_ms);
}

TEST(UnitsTests, UnitlessValuesSupportChronoDurations)
{
    static_assert(std::is_same_v<decltype(1.5_unitless * 500ms), Time>);
    static_assert(std::is_same_v<decltype(500ms * 1.5_unitless), Time>);
    static_assert(std::is_same_v<decltype(500ms / 2_unitless), Time>);
    static_assert(std::is_same_v<decltype(2_unitless / 500ms), Frequency>);
    EXPECT_EQ(1.5_unitless * 500ms, 750_ms);
    EXPECT_EQ(500ms * 1.5_unitless, 750_ms);
    EXPECT_EQ(500ms / 2_unitless, 250_ms);
    EXPECT_EQ(2_unitless / 500ms, 4_Hz);
}

TEST(UnitsTests, UnitlessStreamOutputHasNoUnitSuffix)
{
    EXPECT_EQ(stream_to_string(0_unitless), "0");
    EXPECT_EQ(stream_to_string(0.5_unitless), "0.5");
    EXPECT_EQ(stream_to_string(-2_unitless), "-2");
    EXPECT_EQ(stream_to_string(1000_unitless), "1000");
}

TEST(UnitsTests, RevolutionsConvertToRadiansAndDegrees)
{
    EXPECT_DOUBLE_EQ((1_rev).get(), 2.0 * std::numbers::pi);
    EXPECT_DOUBLE_EQ((1_rev).get<AngleUnits::deg>(), 360.0);
    EXPECT_DOUBLE_EQ((180_deg).get<AngleUnits::rev>(), 0.5);
    EXPECT_DOUBLE_EQ(Angle::create<AngleUnits::rev>(0.25).get<AngleUnits::deg>(), 90.0);
}

TEST(UnitsTests, RpmConversionsUseRadiansPerSecondAsBase)
{
    EXPECT_DOUBLE_EQ((60_rpm).get(), 2.0 * std::numbers::pi);
    EXPECT_DOUBLE_EQ((60_rpm).get<AngularVelocityUnits::rad_s>(), 2.0 * std::numbers::pi);
    EXPECT_DOUBLE_EQ((1_rad_s).get<AngularVelocityUnits::rpm>(), 60.0 / (2.0 * std::numbers::pi));
    EXPECT_DOUBLE_EQ(AngularVelocity::create<AngularVelocityUnits::rpm>(30.0).get(), std::numbers::pi);
    EXPECT_DOUBLE_EQ((-60_rpm).get(), -2.0 * std::numbers::pi);
    EXPECT_DOUBLE_EQ((0_rpm).get(), 0.0);
    EXPECT_DOUBLE_EQ(60_rpm / 30_rpm, 2.0);
}

TEST(UnitsTests, DegreesPerSecondSupportsLiteralAndEnumCreation)
{
    EXPECT_DOUBLE_EQ((180_deg_s).get(), std::numbers::pi);
    EXPECT_NEAR((6_deg_s).get<AngularVelocityUnits::rpm>(), 1.0, 1e-12);
    const auto velocity{ AngularVelocity::create(90.0, Units::deg_s) };
    ASSERT_TRUE(velocity.has_value());
    EXPECT_NEAR(velocity->get(), std::numbers::pi / 2.0, 1e-12);
    EXPECT_DOUBLE_EQ((1_deg_s * 2_s).get<AngleUnits::deg>(), 2.0);
}

TEST(UnitsTests, AngleDividedByTimeProducesAngularVelocity)
{
    static_assert(std::is_same_v<decltype(1_rev / 2_s), AngularVelocity>);
    EXPECT_DOUBLE_EQ((1_rev / 2_s).get<AngularVelocityUnits::rpm>(), 30.0);
    EXPECT_DOUBLE_EQ((180_deg / 2_s).get<AngularVelocityUnits::rad_s>(), std::numbers::pi / 2.0);
}

TEST(UnitsTests, AngularVelocityTimesTimeProducesAngleInBothOrders)
{
    static_assert(std::is_same_v<decltype(60_rpm * 2_s), Angle>);
    static_assert(std::is_same_v<decltype(2_s * 60_rpm), Angle>);
    EXPECT_DOUBLE_EQ((60_rpm * 2_s).get<AngleUnits::rev>(), 2.0);
    EXPECT_DOUBLE_EQ((2_s * 60_rpm).get<AngleUnits::deg>(), 720.0);
}

TEST(UnitsTests, AngleDividedByAngularVelocityProducesTime)
{
    static_assert(std::is_same_v<decltype(1_rev / 60_rpm), Time>);
    EXPECT_DOUBLE_EQ((1_rev / 60_rpm).get<TimeUnits::s>(), 1.0);
    EXPECT_DOUBLE_EQ((180_deg / 30_rpm).get<TimeUnits::s>(), 1.0);
}

TEST(UnitsTests, AngularRelationsSupportChronoDurations)
{
    static_assert(std::is_same_v<decltype(1_rev / 500ms), AngularVelocity>);
    static_assert(std::is_same_v<decltype(60_rpm * 500ms), Angle>);
    static_assert(std::is_same_v<decltype(500ms * 60_rpm), Angle>);
    EXPECT_DOUBLE_EQ((1_rev / 500ms).get<AngularVelocityUnits::rpm>(), 120.0);
    EXPECT_DOUBLE_EQ((60_rpm * 500ms).get<AngleUnits::deg>(), 180.0);
    EXPECT_DOUBLE_EQ((500ms * 60_rpm).get<AngleUnits::rev>(), 0.5);
}

TEST(UnitsTests, RatioAndRotationalUnitsUseAutomaticStreamUnitSelection)
{
    EXPECT_EQ(stream_to_string(50_percent), "50 %");
    EXPECT_EQ(stream_to_string(100_percent), "1");
    EXPECT_EQ(stream_to_string(0_percent), "0");
    EXPECT_EQ(stream_to_string(360_deg), "1 rev");
    EXPECT_EQ(stream_to_string(1_rpm), "1 rpm");
    EXPECT_EQ(stream_to_string(1_rad_s), "1 rad/s");
    EXPECT_EQ(stream_to_string(0_rpm), "0 rad/s");
}

TEST(UnitsTests, MotionDynamics)
{
    using namespace pnm::units;
    static_assert(std::is_same_v<decltype(1_m_s2 / 1_s), Jerk>);
    static_assert(std::is_same_v<decltype(1_rad_s / 1_s), AngularAcceleration>);
    static_assert(std::is_same_v<decltype(1_rad_s2 / 1_s), AngularJerk>);
    static_assert(std::is_same_v<decltype(1_rad_s3 * 1_s), AngularAcceleration>);
    static_assert(std::is_same_v<decltype(1_rad_s2 * 1_s), AngularVelocity>);
    EXPECT_NEAR((180_deg_s2).get<AngularAccelerationUnits::rad_s2>(), std::numbers::pi, 1e-12);
    EXPECT_NEAR((180_deg_s3).get<AngularJerkUnits::rad_s3>(), std::numbers::pi, 1e-12);
    EXPECT_NEAR((1000_mm_s3).get<JerkUnits::m_s3>(), 1.0, 1e-12);
    EXPECT_NEAR((90_deg_s2 * 2_s).get<AngularVelocityUnits::rpm>(), 30.0, 1e-12);
}

TEST(UnitsTests, MotorDerivativesAndTransmissionLead)
{
    using namespace pnm::units;
    EXPECT_DOUBLE_EQ((60_rpm_s).get<AngularAccelerationUnits::rad_s2>(), 2 * std::numbers::pi);
    EXPECT_DOUBLE_EQ((60_rpm_s2).get<AngularJerkUnits::rad_s3>(), 2 * std::numbers::pi);
    EXPECT_DOUBLE_EQ((1_mm_rev).get<TransmissionLeadUnits::m_rad>(), .001 / (2 * std::numbers::pi));
    static_assert(std::is_same_v<decltype(1_Hz / 1_s), FrequencyRate>);
    static_assert(std::is_same_v<decltype(1_Hz_s / 1_s), FrequencyAcceleration>);
    static_assert(std::is_same_v<UnitTraits<Units::rpm_s>::Quantity, AngularAcceleration>);
    static_assert(std::is_same_v<UnitTraits<Units::mm_rev>::Quantity, TransmissionLead>);
    EXPECT_EQ(AngularAcceleration::suffix<AngularAccelerationUnits::rpm_s>(), "rpm/s");
    EXPECT_EQ(AngularJerk::suffix<AngularJerkUnits::rpm_s2>(), "rpm/s²");
}
