#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

// Minimal, dependency-free unit test harness for the simulation libraries.
// Deliberately not gtest/Catch2: the sim build pulls in no test framework and
// these tests are not worth adding one. Tests self-register at static-init
// time, so a new test is a single SIM_TEST block in any .cpp linked into the
// sim_tests target -- no list to keep in sync.
//
// Results follow the project's convention: 1 = pass, 0 = fail. The runner
// prints that column, then the detail of every failed check, and returns the
// number of failed tests as the process exit code.

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Collects every failed check of one test. A test that records nothing passes.
struct TestContext
{
    std::vector<std::string> failures;

    void fail(const std::string& message) { failures.push_back(message); }
    bool passed() const { return failures.empty(); }
};

using TestFn = void (*)(TestContext&);

struct TestCase
{
    const char* name;
    const char* description;
    TestFn fn;
};

// Function-local static so the registry is guaranteed to exist before the
// first registrar touches it. Tests live in several translation units, and a
// namespace-scope vector would be at the mercy of static-init order.
inline std::vector<TestCase>& testRegistry()
{
    static std::vector<TestCase> registry;
    return registry;
}

struct TestRegistrar
{
    TestRegistrar(const char* name, const char* description, TestFn fn)
    {
        testRegistry().push_back({name, description, fn});
    }
};

// Declares and registers a test. The body receives a TestContext named 'ctx',
// which every CHECK_* macro below writes its failures into.
#define SIM_TEST(NAME, DESCRIPTION)                                          \
    static void NAME(TestContext& ctx);                                      \
    static TestRegistrar NAME##_registrar(#NAME, DESCRIPTION, &NAME);        \
    static void NAME(TestContext& ctx)

inline std::string testNum(double value)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

// A failing check records its reason and lets the test keep running, so one
// run reports every broken expectation instead of only the first.
#define CHECK(COND, MSG)                                                     \
    do {                                                                     \
        if (!(COND)) {                                                       \
            ctx.fail(std::string(MSG) + "  [failed: " #COND "]");            \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(ACTUAL, EXPECTED, TOL, MSG)                               \
    do {                                                                     \
        const double checkActual = static_cast<double>(ACTUAL);              \
        const double checkExpect = static_cast<double>(EXPECTED);            \
        if (!(std::fabs(checkActual - checkExpect) <= (TOL))) {              \
            ctx.fail(std::string(MSG) + "  (expected " +                     \
                     testNum(checkExpect) + " +/- " + testNum(TOL) +         \
                     ", got " + testNum(checkActual) + ")");                 \
        }                                                                    \
    } while (0)

#define CHECK_CMP(LHS, OP, RHS, MSG)                                         \
    do {                                                                     \
        const double checkLhs = static_cast<double>(LHS);                    \
        const double checkRhs = static_cast<double>(RHS);                    \
        if (!(checkLhs OP checkRhs)) {                                       \
            ctx.fail(std::string(MSG) + "  (expected " + testNum(checkLhs) + \
                     " " #OP " " + testNum(checkRhs) + ")");                 \
        }                                                                    \
    } while (0)

#define CHECK_FINITE(VALUE, MSG)                                             \
    do {                                                                     \
        const double checkValue = static_cast<double>(VALUE);                \
        if (!std::isfinite(checkValue)) {                                    \
            ctx.fail(std::string(MSG) + "  (value is NaN or infinite)");     \
        }                                                                    \
    } while (0)

// Runs every registered test. 'nameFilter' (optional) runs only the tests
// whose name contains it; 'csvPath' (optional) also writes a
// test_name,result CSV for the test report. Returns the failure count.
inline int runRegisteredTests(const std::string& nameFilter = "",
                              const std::string& csvPath = "")
{
    const std::vector<TestCase>& tests = testRegistry();

    std::cout << "============================================================\n";
    std::cout << "  RoadMap Simulation -- Unit Tests\n";
    std::cout << "  Result: 1 = Pass, 0 = Fail\n";
    std::cout << "============================================================\n\n";

    std::vector<std::pair<std::string, int>> results;
    int failed = 0;
    int ran = 0;

    for (const TestCase& test : tests)
    {
        const std::string name = test.name;
        if (!nameFilter.empty() && name.find(nameFilter) == std::string::npos) continue;

        TestContext ctx;
        test.fn(ctx);
        const int result = ctx.passed() ? 1 : 0;

        ran++;
        if (result == 0) failed++;
        results.emplace_back(name, result);

        std::cout << "  " << result << "   " << name << "\n";
        std::cout << "      " << test.description << "\n";
        for (const std::string& reason : ctx.failures)
        {
            std::cout << "      FAIL: " << reason << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "------------------------------------------------------------\n";
    std::cout << "  Passed " << (ran - failed) << " / " << ran << " tests\n";
    std::cout << "------------------------------------------------------------\n";

    if (!csvPath.empty())
    {
        std::ofstream csv(csvPath);
        if (csv.is_open())
        {
            csv << "test_name,result\n";
            for (const auto& row : results) csv << row.first << "," << row.second << "\n";
            std::cout << "  Results written to " << csvPath << "\n";
        }
        else
        {
            std::cout << "  [WARNING] Could not open " << csvPath << " for writing\n";
        }
    }

    return failed;
}

#endif
