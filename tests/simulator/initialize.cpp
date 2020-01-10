
#include "kul/io.hpp"
#include "kul/os.hpp"
#include "kul/log.hpp"

#include "tests/simulator/per_test.h"

static kul::File bin;
static std::string cwd;

TYPED_TEST(SimulatorTest, fuzzPythonInput)
{
    int mpi_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_rank);

    // __FILE__ may be relative, or concrete
    kul::Dir simDir = std::string{__FILE__}[0] == '/'
                          ? kul::File{__FILE__}.dir()
                          : kul::File{kul::File{__FILE__, cwd}.real()}.dir();

    kul::Dir pyDir{simDir.join("py")};

    for (auto& file : kul::Dir{pyDir.join("sims")}.files())
    {
        if (mpi_rank == 0)
        {
            kul::File job{"job.py"};
            kul::File{"job.header.py.txt", pyDir}.cp(job);

            kul::io::Writer writer{job, /*append = */ true};
            const char* line = nullptr;
            kul::io::Reader middle{file};
            while ((line = middle.readLine()) != nullptr)
                writer << line << kul::os::EOL();
            kul::io::Reader footer{kul::File{"job.footer.py.txt", pyDir}};
            while ((line = footer.readLine()) != nullptr)
                writer << line << kul::os::EOL();
        }
        TypeParam{}.dumpDiagnostics();
    }
}

int main(int argc, char** argv)
{
    bin = kul::File(argv[0]);
    cwd = kul::env::CWD();
    kul::env::CWD(bin.dir());

    ::testing::InitGoogleTest(&argc, argv);
    PHARE_test::SamraiLifeCycle samsam(argc, argv);
    return RUN_ALL_TESTS();
}
