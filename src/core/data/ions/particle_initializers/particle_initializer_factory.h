#ifndef PHARE_PARTICLE_INITIALIZER_FACTORY_H
#define PHARE_PARTICLE_INITIALIZER_FACTORY_H


#include "data_provider.h"
#include "maxwellian_particle_initializer.h"
#include "particle_initializer.h"
#include "utilities/types.h"

#include <memory>

namespace PHARE
{
namespace core
{
    template<typename ParticleArray, typename GridLayout>
    class ParticleInitializerFactory
    {
        using ParticleInitializerT      = ParticleInitializer<ParticleArray, GridLayout>;
        static constexpr auto dimension = GridLayout::dimension;

    public:
        static std::unique_ptr<ParticleInitializerT> create(PHARE::initializer::PHAREDict<1>& dict)
        {
            auto initializerName = dict["name"].to<std::string>();

            std::string toto;

            if (initializerName == "MaxwellianParticleInitializer")
            {
                auto& density = dict["density"].to<PHARE::initializer::ScalarFunction<dimension>>();

                auto& bulkVelx
                    = dict["bulk_velocity_x"].to<PHARE::initializer::ScalarFunction<dimension>>();

                auto& bulkVely
                    = dict["bulk_velocity_y"].to<PHARE::initializer::ScalarFunction<dimension>>();

                auto& bulkVelz
                    = dict["bulk_velocity_z"].to<PHARE::initializer::ScalarFunction<dimension>>();

                auto& vthx = dict["thermal_velocity_x"]
                                 .to<PHARE::initializer::ScalarFunction<dimension>>();

                auto& vthy = dict["thermal_velocity_y"]
                                 .to<PHARE::initializer::ScalarFunction<dimension>>();

                auto& vthz = dict["thermal_velocity_z"]
                                 .to<PHARE::initializer::ScalarFunction<dimension>>();

                auto charge = dict["charge"].to<double>();

                auto nbrPartPerCell = dict["nbrPartPerCell"].to<std::size_t>();

                auto basisName = dict["basis"].to<std::string>();

                std::array<PHARE::initializer::ScalarFunction<dimension>, 3> v
                    = {bulkVelx, bulkVely, bulkVelz};

                std::array<PHARE::initializer::ScalarFunction<dimension>, 3> vth
                    = {vthx, vthy, vthz};

                if (basisName == "Cartesian")
                {
                    return std::make_unique<
                        MaxwellianParticleInitializer<ParticleArray, GridLayout>>(
                        density, v, vth, charge, nbrPartPerCell);
                }
                else if (basisName == "Magnetic")
                {
                    [[maybe_unused]] Basis basis = Basis::Magnetic;
                    [[maybe_unused]] auto& bx
                        = dict["magnetic_x"].to<PHARE::initializer::ScalarFunction<dimension>>();
                    [[maybe_unused]] auto& by
                        = dict["magnetic_x"].to<PHARE::initializer::ScalarFunction<dimension>>();
                    [[maybe_unused]] auto& bz
                        = dict["magnetic_x"].to<PHARE::initializer::ScalarFunction<dimension>>();

                    return std::make_unique<
                        MaxwellianParticleInitializer<ParticleArray, GridLayout>>(
                        density, v, vth, charge, nbrPartPerCell);
                }
            }
            // TODO throw?
            return nullptr;
        }
    };

} // namespace core

} // namespace PHARE


#endif