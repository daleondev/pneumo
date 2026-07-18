#include "pneumo/units.hpp"

#include <chrono>
#include <iostream>

// NOLINTBEGIN

namespace
{
    auto print_storage_example() -> void
    {
        using namespace pnm::units::literals;

        const auto footage_size = 3.5_GB;

        std::cout << "4K drone footage\n";
        std::cout << "  size: " << footage_size.get<pnm::units::ByteSizeUnits::GB>() << " GB\n";
        std::cout << "  size: " << footage_size.get<pnm::units::ByteSizeUnits::MB>() << " MB\n";
        std::cout << "  size: " << footage_size.get<pnm::units::ByteSizeUnits::bytes>() << " bytes\n\n";
    }

    auto print_trip_example() -> void
    {
        using namespace pnm::units::literals;

        const auto trip_distance = 42.0_km;
        const auto trip_time = 35min;
        const auto average_speed = trip_distance / trip_time;
        const auto remaining_distance = 18.0_km;
        const auto eta = remaining_distance / average_speed;
        const std::chrono::duration<double, std::chrono::minutes::period> chrono_eta = eta;

        std::cout << "Commute planning\n";
        std::cout << "  distance: " << trip_distance.get<pnm::units::DistanceUnits::km>() << " km\n";
        std::cout << "  average speed: " << average_speed.get<pnm::units::VelocityUnits::km_h>() << "km/h\n";
        std::cout << "  ETA for remaining 18 km: " << chrono_eta.count() << "min\n\n";
    }

    auto print_acceleration_example() -> void
    {
        using namespace pnm::units::literals;

        const auto highway_speed = 100.0_km_h;
        const auto zero_to_hundred = 8.0_s;
        const auto average_acceleration = highway_speed / zero_to_hundred;
        const auto speed_after_three_seconds = average_acceleration * 3.0_s;
        const auto time_to_city_speed = 50.0_km_h / average_acceleration;

        std::cout << "EV acceleration\n";
        std::cout << "  0-100 km/h time: " << zero_to_hundred.get<pnm::units::TimeUnits::s>() << " s\n";
        std::cout << "  average acceleration: "
                  << average_acceleration.get<pnm::units::AccelerationUnits::m_s2>() << " m/s^2\n";
        std::cout << "  speed after 3 s: " << speed_after_three_seconds.get<pnm::units::VelocityUnits::km_h>()
                  << " km/h\n";
        std::cout << "  time to 50 km/h: " << time_to_city_speed.get<pnm::units::TimeUnits::s>() << "s\n\n ";

        std::cout << zero_to_hundred << std::endl;
    }

    auto print_mechanics_example() -> void
    {
        using namespace pnm::units::literals;

        const auto payload = 120.0_kg;
        const auto lift_acceleration = 1.5_m_s2;
        const auto lift_force = payload * lift_acceleration;
        const auto lift_height = 6.0_m;
        const auto lift_energy = lift_force * lift_height;
        const auto lift_time = 3.0_s;
        const auto motor_power = lift_energy / lift_time;
        const auto motor_current = motor_power / 48.0_V;
        const auto hydraulic_pressure = lift_force / 0.02_m2;

        std::cout << "Lift sizing\n";
        std::cout << "  force: " << lift_force.get<pnm::units::ForceUnits::N>() << " N\n";
        std::cout << "  energy: " << lift_energy.get<pnm::units::EnergyUnits::J>() << " J\n";
        std::cout << "  power: " << motor_power.get<pnm::units::PowerUnits::W>() << " W\n";
        std::cout << "  current at 48 V: " << motor_current.get<pnm::units::CurrentUnits::A>() << " A\n";
        std::cout << "  hydraulic pressure: " << hydraulic_pressure.get<pnm::units::PressureUnits::bar>()
                  << " bar\n\n";
    }
}

auto main() -> int
{
    print_storage_example();
    print_trip_example();
    print_acceleration_example();
    print_mechanics_example();

    return 0;
}

// NOLINTEND
