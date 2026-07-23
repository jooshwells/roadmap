// Entry point for the simulation unit tests.
//
//   sim_tests                       run everything
//   sim_tests IDM_                  run only tests whose name contains "IDM_"
//   sim_tests --csv results.csv     also write a test_name,result CSV
//
// Exit code is the number of failed tests, so a build step or CI job can gate
// on it directly.

#include "test_harness.h"

#include <string>

int main(int argc, char** argv)
{
    std::string nameFilter;
    std::string csvPath;

    for (int i = 1; i < argc; i++)
    {
        const std::string arg = argv[i];
        if (arg == "--csv" && i + 1 < argc) csvPath = argv[++i];
        else if (arg.rfind("--", 0) != 0) nameFilter = arg;
    }

    return runRegisteredTests(nameFilter, csvPath);
}
