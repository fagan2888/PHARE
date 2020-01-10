#ifndef PHARE_TEST_DIAGNOSTIC_INCLUDE_H
#define PHARE_TEST_DIAGNOSTIC_INCLUDE_H

#include "tests/simulator/per_test.h"

#include "diagnostic/detail/highfive.h"
#include "diagnostic/detail/types/electromag.h"
#include "diagnostic/detail/types/particle.h"
#include "diagnostic/detail/types/fluid.h"

using namespace PHARE::diagnostic;
using namespace PHARE::diagnostic::h5;

constexpr unsigned NEW_HI5_FILE
    = HighFive::File::ReadWrite | HighFive::File::Create | HighFive::File::Truncate;

template<typename HiFile, typename Field>
void checkField(HiFile& file, Field& field, std::string path)
{
    std::vector<float> fieldV;
    file.getDataSet(path).read(fieldV);
    EXPECT_EQ(fieldV.size(), field.size());

    for (size_t i = 0; i < fieldV.size(); i++)
    {
        if (!std::isnan(fieldV[i]))
        {
            EXPECT_FLOAT_EQ(fieldV[i], field.data()[i]);
        }
    }
}

template<typename HiFile, typename VecField>
void checkVecField(HiFile& file, VecField& vecField, std::string fieldPath)
{
    for (auto& [id, type] : PHARE::core::Components::componentMap)
    {
        checkField(file, vecField.getComponent(type), fieldPath + "/" + id);
    }
}

template<typename Simulator>
struct Hi5Diagnostic
{
    using HybridModelT        = typename Simulator::HybridModel;
    using DiagnosticModelView = AMRDiagnosticModelView<Simulator, HybridModelT>;
    using DiagnosticWriter    = HighFiveDiagnostic<DiagnosticModelView>;

    Hi5Diagnostic(Simulator& simulator, std::string fileName, unsigned flags)
        : simulator_{simulator}
        , file_{fileName}
        , flags_{flags}
    {
    }
    ~Hi5Diagnostic() {}

    auto dict(std::string&& type, std::string& subtype)
    {
        PHARE::initializer::PHAREDict dict;
        dict["name"]            = type;
        dict["type"]            = type;
        dict["subtype"]         = subtype;
        dict["compute_every"]   = std::size_t{1};
        dict["write_every"]     = std::size_t{1};
        dict["start_iteration"] = std::size_t{0};
        dict["last_iteration"]  = std::numeric_limits<std::size_t>::max();
        return dict;
    }
    auto electromag(std::string&& subtype) { return dict("electromag", subtype); }
    auto particles(std::string&& subtype) { return dict("particle", subtype); }
    auto fluid(std::string&& subtype) { return dict("fluid", subtype); }

    std::string filename() const { return file_ + "_hi5_test.5"; }

    std::string getPatchPath(int level, PHARE::amr::SAMRAI_Types::patch_t& patch)
    {
        std::stringstream globalId;
        globalId << patch.getGlobalId();
        return writer.getPatchPath("time", level, globalId.str());
    }

    Simulator& simulator_;
    std::string file_;
    unsigned flags_;
    DiagnosticModelView modelView{simulator_, *simulator_.getHybridModel()};
    DiagnosticWriter writer{modelView, filename(), flags_};
    DiagnosticsManager<DiagnosticWriter> dMan{writer};
};


#endif /*PHARE_TEST_DIAGNOSTIC_INCLUDE_H*/