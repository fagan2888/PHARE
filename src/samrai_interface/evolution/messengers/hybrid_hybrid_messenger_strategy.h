
#ifndef PHARE_HYBRID_HYBRID_MESSENGER_STRATEGY_H
#define PHARE_HYBRID_HYBRID_MESSENGER_STRATEGY_H

#include "communicators.h"
#include "data/field/coarsening/field_coarsen_operator.h"
#include "data/field/refine/field_refine_operator.h"
#include "data/field/time_interpolate/field_linear_time_interpolate.h"
#include "data/particles/refine/particles_data_split.h"
#include "data/particles/refine/split.h"
#include "evolution/messengers/hybrid_messenger_info.h"
#include "evolution/messengers/hybrid_messenger_strategy.h"
#include "numerics/interpolator/interpolator.h"
#include "physical_models/physical_model.h"
#include "tools/amr_utils.h"
#include "tools/resources_manager_utilities.h"

#include <SAMRAI/xfer/RefineAlgorithm.h>
#include <SAMRAI/xfer/RefineSchedule.h>


#include <iterator>
#include <optional>
#include <utility>


namespace PHARE
{
namespace amr_interface
{
    /** \brief An HybridMessenger is the specialization of a HybridMessengerStrategy for hybrid to
     * hybrid data communications.
     */
    template<typename HybridModel>
    class HybridHybridMessengerStrategy : public HybridMessengerStrategy<HybridModel>
    {
        using IonsT                                = typename HybridModel::ions_type;
        using ElectromagT                          = typename HybridModel::electromag_type;
        using VecFieldT                            = typename HybridModel::vecfield_type;
        using GridLayoutT                          = typename HybridModel::gridLayout_type;
        using FieldT                               = typename VecFieldT::field_type;
        using ResourcesManagerT                    = typename HybridModel::resources_manager_type;
        static constexpr std::size_t dimension     = GridLayoutT::dimension;
        static constexpr std::size_t interpOrder   = GridLayoutT::interp_order;
        using SplitT                               = Split<dimension, interpOrder>;
        static constexpr std::size_t nbRefinedPart = 2; // TODO stop hard-coding this
        using InteriorParticleRefineOp
            = ParticlesRefineOperator<dimension, interpOrder, ParticlesDataSplitType::interior,
                                      nbRefinedPart, SplitT>;

        using CoarseToFineRefineOpOld
            = ParticlesRefineOperator<dimension, interpOrder,
                                      ParticlesDataSplitType::coarseBoundaryOld, nbRefinedPart,
                                      SplitT>;

        using CoarseToFineRefineOpNew
            = ParticlesRefineOperator<dimension, interpOrder,
                                      ParticlesDataSplitType::coarseBoundaryNew, nbRefinedPart,
                                      SplitT>;


    public:
        static const std::string stratName;
        static constexpr std::size_t rootLevelNumber = 0;


        HybridHybridMessengerStrategy(std::shared_ptr<ResourcesManagerT> manager,
                                      int const firstLevel)
            : HybridMessengerStrategy<HybridModel>{stratName}
            , resourcesManager_{std::move(manager)}
            , firstLevel_{firstLevel}
        {
            resourcesManager_->registerResources(EM_old_);
        }

        virtual ~HybridHybridMessengerStrategy() = default;



        /* ------------------------------------------------------------------------
                        methods used for the IMessenger interface
           ------------------------------------------------------------------------ */


        /**
         * @brief allocate the messenger strategy internal variables to the model resourceManager
         */
        virtual void allocate(SAMRAI::hier::Patch& patch, double const allocateTime) const override
        {
            resourcesManager_->allocate(EM_old_, patch, allocateTime);
        }



        /**
         * @brief setup creates all SAMRAI algorithms to communicate data involved in a messenger
         * between the coarse and fine levels.
         *
         * This method creates the SAMRAI algorithms for communications associated between pairs of
         * variables. The function does not create the SAMRAI schedules since they depend on the
         * levels
         */
        virtual void
        registerQuantities(std::unique_ptr<IMessengerInfo> fromCoarserInfo,
                           [[maybe_unused]] std::unique_ptr<IMessengerInfo> fromFinerInfo) override
        {
            std::unique_ptr<HybridMessengerInfo> hybridInfo{
                dynamic_cast<HybridMessengerInfo*>(fromCoarserInfo.release())};

            registerForSpaceTimeComm_(hybridInfo);
            registerForSpaceComm_(hybridInfo);
        }



        /**
         * @brief registerLevel registers the level for all Communicators
         *
         * The level must always be registered to ghost Communicators but only to init Communicators
         * if it is not the root level since root is not initialized by coarser data.
         *
         * Ghost communicators that need to know this level :
         *
         *  - magnetic fields
         *  - electric fields
         *  - patch ghost particles
         *
         *  ion moments : do not need to be filled on ghost node by SAMRAI schedules
         *  since they will be filled with levelGhostParticles[old,new] on level ghost nodes
         *  and computed by ghost particles on interior patch ghost nodes
         *
         *
         * Init communicators that need to know this level :
         *
         *  - magnetic fields
         *  - electric fields
         *  - ion interior particle arrays
         *  - ion levelGhostParticlesOld particle arrays
         */
        virtual void registerLevel(std::shared_ptr<SAMRAI::hier::PatchHierarchy> const& hierarchy,
                                   int const levelNumber) override
        {
            auto level = hierarchy->getPatchLevel(levelNumber);

            magneticGhosts_.registerLevel(hierarchy, level);
            electricGhosts_.registerLevel(hierarchy, level);
            patchGhostParticles_.registerLevel(hierarchy, level);

            // root level is not initialized with a schedule using coarser level data
            // so we don't create these schedules if root level
            if (levelNumber != rootLevelNumber)
            {
                magneticInit_.registerLevel(hierarchy, level);
                electricInit_.registerLevel(hierarchy, level);
                interiorParticles_.registerLevel(hierarchy, level);
                levelGhostParticlesOld_.registerLevel(hierarchy, level);
                levelGhostParticlesNew_.registerLevel(hierarchy, level);
            }
        }



        /**
         * @brief regrid performs the regriding communications for Hybrid to Hybrid messengers
         *
         * basically, all quantities that are in initialization refiners need to be regridded
         */
        virtual void regrid(std::shared_ptr<SAMRAI::hier::PatchHierarchy> const& hierarchy,
                            const int levelNumber,
                            std::shared_ptr<SAMRAI::hier::PatchLevel> const& oldLevel,
                            IPhysicalModel& model, double const initDataTime) override
        {
            auto level = hierarchy->getPatchLevel(levelNumber);
            magneticInit_.regrid(hierarchy, levelNumber, oldLevel, initDataTime);
            electricInit_.regrid(hierarchy, levelNumber, oldLevel, initDataTime);
            interiorParticles_.regrid(hierarchy, levelNumber, oldLevel, initDataTime);
            levelGhostParticlesOld_.regrid(hierarchy, levelNumber, oldLevel, initDataTime);
            copyLevelGhostOldToPushable_(*level, model);
            computeIonMoments_(*level, model);
            // levelGhostNew will be refined in next firstStep
        }




        virtual std::string fineModelName() const override { return HybridModel::model_name; }



        virtual std::string coarseModelName() const override { return HybridModel::model_name; }




        virtual std::unique_ptr<IMessengerInfo> emptyInfoFromCoarser() override
        {
            return std::make_unique<HybridMessengerInfo>();
        }




        virtual std::unique_ptr<IMessengerInfo> emptyInfoFromFiner() override
        {
            return std::make_unique<HybridMessengerInfo>();
        }



        /**
         * @brief initLevel is used to initialize hybrid data on the level levelNumer at time
         * initDataTime from hybrid coarser data.
         */
        virtual void initLevel(IPhysicalModel& model, SAMRAI::hier::PatchLevel& level,
                               double const initDataTime) override
        {
            auto levelNumber = level.getLevelNumber();

            magneticInit_.fill(levelNumber, initDataTime);
            electricInit_.fill(levelNumber, initDataTime);

            // no need to call these :
            // magneticGhosts_.fill(levelNumber, initDataTime);
            // electricGhosts_.fill(levelNumber, initDataTime);
            // because the SAMRAI schedules in the 'init' communicators
            // already fill the patch ghost box from the neighbor interior box.
            // so ghost nodes are already filled .

            interiorParticles_.fill(levelNumber, initDataTime);
            // however we need to call the ghost communicator for patch ghost particles
            // since the interior schedules have a restriction to the interior of the patch.
            patchGhostParticles_.fill(levelNumber, initDataTime);


            levelGhostParticlesOld_.fill(levelNumber, initDataTime);


            // levelGhostParticles will be pushed during the advance phase
            // they need to be identical to levelGhostParticlesOld before advance
            copyLevelGhostOldToPushable_(level, model);

            computeIonMoments_(level, model);
        }



        /* ------------------------------------------------------------------------
                     methods used for the HybridMessenger interface
           ------------------------------------------------------------------------ */


        /**
         * @brief see IMessenger::fillMagneticGhosts for documentation

         * Note on the HybridHybrid version:
         * The function throws if the given magnetic field B has not been registered
         * in the ghostMagnetic field of the HybridMessengerInfo
         *
         * The method finds if the name of the VecField is
         *
         */
        virtual void fillMagneticGhosts(VecFieldT& B, int const levelNumber,
                                        double const fillTime) override
        {
            magneticGhosts_.fill(B, levelNumber, fillTime);
        }




        virtual void fillElectricGhosts(VecFieldT& E, int const levelNumber,
                                        double const fillTime) override
        {
            electricGhosts_.fill(E, levelNumber, fillTime);
        }




        /**
         * @brief fillIonGhostParticles will fill the interior ghost particle array from neighbor
         * patches of the same level. Before doing that, it empties the array for all populations
         */
        virtual void fillIonGhostParticles(IonsT& ions, SAMRAI::hier::PatchLevel& level,
                                           double const fillTime) override
        {
            std::cout << "perform the ghost particle fill\n";

            for (auto patch : level)
            {
                auto dataOnPatch = resourcesManager_->setOnPatch(*patch, ions);
                for (auto& pop : ions)
                {
                    empty(pop.patchGhostParticles());
                }
            }

            patchGhostParticles_.fill(level.getLevelNumber(), fillTime);
        }




        /**
         * @brief fillIonMomentGhosts will compute the ion moments for all species on ghost nodes.
         *
         * For patch ghost nodes, patch ghost particles are used.
         * For level ghost nodes, levelGhostParticlesOld and new are both projected with a time
         * interpolation coef.
         */
        virtual void fillIonMomentGhosts(IonsT& ions, SAMRAI::hier::PatchLevel& level,
                                         double const beforePushTime,
                                         double const afterPushTime) override
        {
            auto alpha = timeInterpCoef_(beforePushTime, afterPushTime);


            for (auto patch : level)
            {
                auto dataOnPatch = resourcesManager_->setOnPatch(*patch, ions);
                auto layout      = layoutFromPatch<GridLayoutT>(*patch);

                for (auto& pop : ions)
                {
                    // first thing to do is to project patchGhostParitcles moments
                    auto& patchGhosts = pop.patchGhostParticles();
                    auto& density     = pop.density();
                    auto& flux        = pop.flux();

                    interpolate_(std::begin(patchGhosts), std::end(patchGhosts), density, flux,
                                 layout);


                    // then grab levelGhostParticlesOld and levelGhostParticlesNew
                    // and project them with alpha and (1-alpha) coefs, respectively
                    auto& levelGhostOld = pop.levelGhostParticlesOld();
                    auto& levelGhostNew = pop.levelGhostParticlesNew();

                    interpolate_(std::begin(levelGhostOld), std::end(levelGhostOld), density, flux,
                                 layout, 1. - alpha);

                    interpolate_(std::begin(levelGhostOld), std::end(levelGhostOld), density, flux,
                                 layout, alpha);
                }
            }
        }




        // synchronization/coarsening methods
        virtual void syncMagnetic(VecFieldT& B) override
        {
            //
            std::cout << "perform coarse magnetic sync to the next coarser level\n";
        }


        virtual void syncElectric(VecFieldT& E) override
        {
            //
            std::cout << "perform coarse electric sync to the next coarser level\n";
        }


        virtual void syncIonMoments(IonsT& ions) override
        {
            //
            std::cout << "perform coarse moments sync to the next coarser level\n";
        }


        /**
         * @brief firstStep : in the HybridHybridMessengerStrategy, the firstStep method is used to
         * get level border ghost particles from the next coarser level. These particles are defined
         * in the future at the time the method is called because the coarser level is ahead in
         * time. These particles are communicated only at first step of a substepping cycle. They
         * will be used with the levelGhostParticlesOld particles to get the moments on level border
         * nodes.
         */
        virtual void firstStep(IPhysicalModel& model, SAMRAI::hier::PatchLevel& level,
                               const std::shared_ptr<SAMRAI::hier::PatchHierarchy>& hierarchy,
                               double time) override
        {
            auto& hybridModel = static_cast<HybridModel&>(model);
            auto levelNumber  = level.getLevelNumber();
            levelGhostParticlesNew_.fill(levelNumber, time);

            // during firstStep() coarser level and current level are at the same time
            // so 'time' is also the beforePushCoarseTime_
            beforePushCoarseTime_ = time;

            if (levelNumber != 0)
            {
                auto coarserLevel    = hierarchy->getPatchLevel(levelNumber - 1);
                auto patch           = *std::begin(*coarserLevel);
                auto times           = resourcesManager_->getTimes(hybridModel.state.ions, *patch);
                afterPushCoarseTime_ = times[0];
            }
        }



        /**
         * @brief lastStep is used to perform operations at the last step of a substepping cycle.
         * It is called after the level is advanced. Here for hybrid-hybrid messages, the method
         * moves levelGhostParticlesNew particles into levelGhostParticlesOld ones. Then
         * levelGhostParticlesNew are emptied since it will be filled again at firstStep of the next
         * substepping cycle. the new CoarseToFineOld content is then copied to levelGhostParticles
         * so that they can be pushed during the next subcycle
         */
        virtual void lastStep(IPhysicalModel& model, SAMRAI::hier::PatchLevel& level) override
        {
            auto& hybridModel = static_cast<HybridModel&>(model);
            for (auto& patch : level)
            {
                auto& ions       = hybridModel.state.ions;
                auto dataOnPatch = resourcesManager_->setOnPatch(*patch, ions);
                for (auto& pop : ions)
                {
                    auto& levelGhostParticlesOld = pop.levelGhostParticlesOld();
                    auto& levelGhostParticlesNew = pop.levelGhostParticlesNew();
                    auto& levelGhostParticles    = pop.levelGhostParticles();

                    core::swap(levelGhostParticlesNew, levelGhostParticlesOld);
                    core::empty(levelGhostParticlesNew);
                    core::empty(levelGhostParticles);
                    std::copy(std::begin(levelGhostParticlesOld), std::end(levelGhostParticlesOld),
                              std::back_inserter(levelGhostParticles));
                }
            }
        }




        /**
         * @brief prepareStep is the concrete implementation of the
         * HybridMessengerStrategy::prepareStep method For hybrid-Hybrid communications.
         * This method copies the current model electromagnetic field, defined at t=n. Since
         * prepareStep() is called just before advancing the level, this operation actually saves
         * the t=n electromagntic field into the messenger. When the time comes that the next finer
         * level needs to time interpolate the electromagnetic field at its ghost nodes, this level
         * will have its model EM field at t=n+1 and thanks to this methods, the t=n field will be
         * in the messenger.
         */
        virtual void prepareStep(IPhysicalModel& model, SAMRAI::hier::PatchLevel& level) final
        {
            auto& hybridModel = static_cast<HybridModel&>(model);
            for (auto& patch : level)
            {
                auto dataOnPatch
                    = resourcesManager_->setOnPatch(*patch, hybridModel.state.electromag, EM_old_);

                auto& EM = hybridModel.state.electromag;
                EM_old_.copyData(EM);
            }
        }




        virtual void fillRootGhosts(IPhysicalModel& model, SAMRAI::hier::PatchLevel& level,
                                    double const initDataTime) final
        {
            auto& hybridModel = static_cast<HybridModel&>(model);
            auto levelNumber  = level.getLevelNumber();
            assert(levelNumber == 0);

            magneticGhosts_.fill(levelNumber, initDataTime);
            electricGhosts_.fill(levelNumber, initDataTime);
            patchGhostParticles_.fill(levelNumber, initDataTime);

            // at some point in the future levelGhostParticles could be filled with injected
            // particles depending on the domain boundary condition.

            computeIonMoments_(level, model);
        }




    private:
        void registerForSpaceTimeComm_(std::unique_ptr<HybridMessengerInfo> const& info)
        {
            auto const& Eold = EM_old_.E;
            auto const& Bold = EM_old_.B;


            makeCommunicators_(info->ghostElectric, info->modelElectric, VecFieldDescriptor{Eold},
                               electricGhosts_);

            makeCommunicators_(info->ghostMagnetic, info->modelMagnetic, VecFieldDescriptor{Bold},
                               magneticGhosts_);
        }




        void registerForSpaceComm_(std::unique_ptr<HybridMessengerInfo> const& info)
        {
            auto makeKeys = [](auto const& descriptor) {
                std::vector<std::string> keys;
                std::transform(std::begin(descriptor), std::end(descriptor),
                               std::back_inserter(keys), [](auto const& d) { return d.vecName; });
                return keys;
            };

            makeCommunicators_(info->initMagnetic, fieldRefineOp_, magneticInit_,
                               makeKeys(info->initMagnetic));

            makeCommunicators_(info->initElectric, fieldRefineOp_, electricInit_,
                               makeKeys(info->initElectric));


            makeCommunicators_(info->interiorParticles, interiorParticleRefineOp_,
                               interiorParticles_, info->interiorParticles);


            makeCommunicators_(info->levelGhostParticlesOld, levelGhostParticlesOldOp_,
                               levelGhostParticlesOld_, info->levelGhostParticlesOld);


            makeCommunicators_(info->levelGhostParticlesNew, levelGhostParticlesNewOp_,
                               levelGhostParticlesNew_, info->levelGhostParticlesNew);


            makeCommunicators_(info->patchGhostParticles, nullptr, patchGhostParticles_,
                               info->patchGhostParticles);
        }



        /**
         * @brief makeCommunicators_ adds to the ghost communicators all VecFieldDescriptor of
         * the given vector field.
         *
         * Each of the ghost VecFieldDescriptor will have an entry in the ghost communicators
         *
         * @param ghostVec is the collection of VecFieldDescriptor. Each VecFieldDescriptor
         * corresponds to a VecField for which ghosts will be needed.
         * @param modelVec is VecFieldDescriptor for the model VecField associated with the VecField
         * for which ghosts are needed. When ghosts are filled, this quantity is taken on the
         * coarser level and is definer at t_coarse+dt_coarse
         * @param oldModelVec is the VecFieldDescriptor for the VecField for which ghosts are
         * needed, at t_coarse. These are typically internal variables of the messenger, like Eold
         * or Bold.
         */
        void makeCommunicators_(std::vector<VecFieldDescriptor> const& ghostVecs,
                                VecFieldDescriptor const& modelVec,
                                VecFieldDescriptor const& oldModelVec,
                                Communicators<CommunicatorType::GhostField>& communicators)
        {
            for (auto const& ghostVec : ghostVecs)
            {
                communicators.add(ghostVec, modelVec, oldModelVec, resourcesManager_,
                                  fieldRefineOp_, fieldTimeOp_, ghostVec.vecName);
            }
        }




        template<typename Descriptors, typename Communicators>
        void makeCommunicators_(Descriptors const& descriptors,
                                std::shared_ptr<SAMRAI::hier::RefineOperator> refineOp,
                                Communicators& communicators, std::vector<std::string> keys)
        {
            auto key = std::begin(keys);
            for (auto const& descriptor : descriptors)
            {
                communicators.add(descriptor, refineOp, *key++, resourcesManager_);
            }
        }



        void copyLevelGhostOldToPushable_(SAMRAI::hier::PatchLevel& level, IPhysicalModel& model)
        {
            auto& hybridModel = static_cast<HybridModel&>(model);
            for (auto& patch : level)
            {
                auto& ions       = hybridModel.state.ions;
                auto dataOnPatch = resourcesManager_->setOnPatch(*patch, ions);
                for (auto& pop : ions)
                {
                    auto& levelGhostParticlesOld = pop.levelGhostParticlesOld();
                    auto& levelGhostParticles    = pop.levelGhostParticles();

                    core::empty(levelGhostParticles);
                    std::copy(std::begin(levelGhostParticlesOld), std::end(levelGhostParticlesOld),
                              std::back_inserter(levelGhostParticles));
                }
            }
        }




        void computeIonMoments_(SAMRAI::hier::PatchLevel& level, IPhysicalModel& model)
        {
            auto& hybridModel = static_cast<HybridModel&>(model);
            for (auto& patch : level)
            {
                auto& ions       = hybridModel.state.ions;
                auto dataOnPatch = resourcesManager_->setOnPatch(*patch, ions);
                auto layout      = layoutFromPatch<GridLayoutT>(*patch);

                for (auto& pop : ions)
                {
                    auto& levelGhostParticlesOld = pop.levelGhostParticlesOld();
                    auto& ghosts                 = pop.patchGhostParticles();
                    auto& domain                 = pop.domainParticles();

                    auto& density = pop.density();
                    auto& flux    = pop.flux();
                    interpolate_(std::begin(domain), std::end(domain), density, flux, layout);
                    interpolate_(std::begin(ghosts), std::end(ghosts), density, flux, layout);
                    interpolate_(std::begin(levelGhostParticlesOld),
                                 std::end(levelGhostParticlesOld), density, flux, layout);
                }

                ions.computeDensity();
                ions.computeBulkVelocity();
            }
        }




        double timeInterpCoef_(double const beforePushTime, double const afterPushTime)
        {
            return (afterPushTime - beforePushTime)
                   / (afterPushCoarseTime_ - beforePushCoarseTime_);
        }




        //! keeps a copy of the model electromagnetic field at t=n
        ElectromagT EM_old_{stratName + "_EM_old"}; // TODO needs to be allocated somewhere and
                                                    // updated to t=n before advanceLevel()


        //! ResourceManager shared with other objects (like the HybridModel)
        std::shared_ptr<ResourcesManagerT> resourcesManager_;


        int const firstLevel_;
        double beforePushCoarseTime_;
        double afterPushCoarseTime_;

        core::Interpolator<dimension, interpOrder> interpolate_;


        //! store communicators for magnetic fields that need ghosts to be filled
        Communicators<CommunicatorType::GhostField> magneticGhosts_;

        //! store communicators for magnetic fields that need to be initialized
        Communicators<CommunicatorType::InitField> magneticInit_;

        //! store refiners for electric fields that need ghosts to be filled
        Communicators<CommunicatorType::GhostField> electricGhosts_;

        //! store communicators for electric fields that need to be initializes
        Communicators<CommunicatorType::InitField> electricInit_;

        // algo and schedule used to initialize domain particles
        // from coarser level using particleRefineOp<domain>
        Communicators<CommunicatorType::InitInteriorPart> interiorParticles_;

        //! store communicators for coarse to fine particles old
        Communicators<CommunicatorType::LevelBorderParticles> levelGhostParticlesOld_;

        //! store communicators for coarse to fine particles new
        Communicators<CommunicatorType::LevelBorderParticles> levelGhostParticlesNew_;

        // keys : model particles (initialization and 2nd push), temporaryParticles (firstPush)
        Communicators<CommunicatorType::InteriorGhostParticles> patchGhostParticles_;


        std::shared_ptr<SAMRAI::hier::RefineOperator> fieldRefineOp_{
            std::make_shared<FieldRefineOperator<GridLayoutT, FieldT>>()};

        // field data time op
        std::shared_ptr<SAMRAI::hier::TimeInterpolateOperator> fieldTimeOp_{
            std::make_shared<FieldLinearTimeInterpolate<GridLayoutT, FieldT>>()};


        std::shared_ptr<SAMRAI::hier::RefineOperator> interiorParticleRefineOp_{
            std::make_shared<InteriorParticleRefineOp>()};

        std::shared_ptr<SAMRAI::hier::RefineOperator> levelGhostParticlesOldOp_{
            std::make_shared<CoarseToFineRefineOpOld>()};

        std::shared_ptr<SAMRAI::hier::RefineOperator> levelGhostParticlesNewOp_{
            std::make_shared<CoarseToFineRefineOpNew>()};
    };

    template<typename HybridModel>
    const std::string HybridHybridMessengerStrategy<HybridModel>::stratName
        = "HybridModel-HybridModel";

} // namespace amr_interface

} // namespace PHARE

#endif
