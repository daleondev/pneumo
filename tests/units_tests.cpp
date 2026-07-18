#include "pneumo/pneumo.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <sstream>
#include <type_traits>
#include <utility>

using pnm::units::Acceleration;
using pnm::units::AccelerationUnits;
using pnm::units::Angle;
using pnm::units::AngleUnits;
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
using pnm::units::Temperature;
using pnm::units::TemperatureUnits;
using pnm::units::Time;
using pnm::units::TimeUnits;
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
static_assert(std::is_same_v<decltype(12.5_MB_s), DataRate>);
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

namespace
{
    auto stream_to_string(const auto& quantity) -> std::string
    {
        std::ostringstream stream;
        stream << quantity;
        return stream.str();
    }
}

TEST(UnitsTests, ByteSizeConvertsAcrossRegisteredUnits)
{
    const auto size = 2.0_KB;

    EXPECT_EQ(size.get(), static_cast<size_t>(2048));
    EXPECT_EQ(size.get<ByteSizeUnits::bytes>(), static_cast<size_t>(2048));
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::KB>(), 2.0);
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MB>(), 2.0 / 1024.0);
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
    const auto rate = 12.5_MB_s;

    EXPECT_DOUBLE_EQ(rate.get(), 13107200.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::bytes_s>(), 13107200.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::KB_s>(), 12800.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::GB_s>(), 12.5 / 1024.0);
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

TEST(UnitsTests, StreamInsertionPrintsSelectedUnits)
{
    EXPECT_EQ(stream_to_string(2.0_h), "2h");
    EXPECT_EQ(stream_to_string(1536.0_bytes), "1.5KB");
    EXPECT_EQ(stream_to_string(1500.0_m), "1.5km");
    EXPECT_EQ(stream_to_string(-1500.0_m), "-1.5km");
    EXPECT_EQ(stream_to_string(0.0_m), "0m");
    EXPECT_EQ(stream_to_string(1500.0_kg), "1.5t");
    EXPECT_EQ(stream_to_string(2.0_kWh), "2kWh");
    EXPECT_EQ(stream_to_string(101325.0_Pa), "1atm");
    EXPECT_EQ(stream_to_string(1.0_mbar), "1mbar");
    EXPECT_EQ(stream_to_string(30.0_C), "30C");
    EXPECT_EQ(stream_to_string(295.5_K), "22.35C");
    EXPECT_EQ(stream_to_string(86.0_F), "30C");
    EXPECT_EQ(stream_to_string(2000.0_Hz), "2kHz");
}

TEST(UnitsTests, StreamInsertionUsesCorrectSuffixWhenUnitOrderDiffersFromScaleOrder)
{
    EXPECT_EQ(stream_to_string(0.5_m_s), "1.8km/h");
    EXPECT_EQ(stream_to_string(0.5_m_s2), "500mm/s2");
}

TEST(UnitsTests, StreamInsertionDisplaysCompoundUnitsWithSlashes)
{
    EXPECT_EQ(stream_to_string(20.0_m_s), "20m/s");
    EXPECT_EQ(stream_to_string(9.81_m_s2), "9.81m/s2");
    EXPECT_EQ(stream_to_string(3.0_MB_s), "3MB/s");
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
    auto size = 50.0_KB;
    size += 10.0_KB;

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
    const auto size = 90.0_MB;
    const auto duration = 30.0_s;
    const auto rate = size / duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(rate)>, DataRate>);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::MB_s>(), 3.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::bytes_s>(), 3145728.0);
}

TEST(UnitsTests, DataRateTimesTimeProducesByteSize)
{
    const auto rate = 25.0_MB_s;
    const auto duration = 4.0_s;
    const auto size = rate * duration;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(size)>, ByteSize>);
    EXPECT_EQ(size.get<ByteSizeUnits::bytes>(), static_cast<size_t>(104857600));
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MB>(), 100.0);
}

TEST(UnitsTests, TimeTimesDataRateProducesByteSize)
{
    const auto duration = 2.0_s;
    const auto rate = 8.0_MB_s;
    const auto size = duration * rate;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(size)>, ByteSize>);
    EXPECT_EQ(size.get<ByteSizeUnits::bytes>(), static_cast<size_t>(16777216));
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MB>(), 16.0);
}

TEST(UnitsTests, ByteSizeDividedByDataRateProducesTime)
{
    const auto size = 300.0_MB;
    const auto rate = 50.0_MB_s;
    const auto duration = size / rate;

    static_assert(std::is_same_v<std::remove_cvref_t<decltype(duration)>, Time>);
    EXPECT_DOUBLE_EQ(duration.get<TimeUnits::s>(), 6.0);
}

TEST(UnitsTests, ChronoDurationsParticipateInTimeBasedQuantityRelations)
{
    const auto speed = 100.0_m / 4s;
    const auto acceleration = 20.0_m_s / 4s;
    const auto power = 100.0_J / 2s;
    const auto rate = 8.0_MB / 2s;
    const auto distance_from_right = 10.0_m_s * 2500ms;
    const auto distance_from_left = 2500ms * 10.0_m_s;
    const auto velocity = 2.0_m_s2 * 1500ms;
    const auto energy = 250ms * 100.0_W;
    const auto size = 1.5s * 2.0_MB_s;
    const auto cycles_from_right = 2s * 0.5_Hz;
    const auto cycles_from_left = 0.5_Hz * 2s;

    EXPECT_DOUBLE_EQ(speed.get<VelocityUnits::m_s>(), 25.0);
    EXPECT_DOUBLE_EQ(acceleration.get<AccelerationUnits::m_s2>(), 5.0);
    EXPECT_DOUBLE_EQ(power.get<PowerUnits::W>(), 50.0);
    EXPECT_DOUBLE_EQ(rate.get<DataRateUnits::MB_s>(), 4.0);
    EXPECT_DOUBLE_EQ(distance_from_right.get<DistanceUnits::m>(), 25.0);
    EXPECT_DOUBLE_EQ(distance_from_left.get<DistanceUnits::m>(), 25.0);
    EXPECT_DOUBLE_EQ(velocity.get<VelocityUnits::m_s>(), 3.0);
    EXPECT_DOUBLE_EQ(energy.get<EnergyUnits::J>(), 25.0);
    EXPECT_DOUBLE_EQ(size.get<ByteSizeUnits::MB>(), 3.0);
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
