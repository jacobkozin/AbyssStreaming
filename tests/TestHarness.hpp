#pragma once

// A deliberately tiny test harness. assert() compiles to nothing in Release
// builds (NDEBUG), and both CI and BUILD_AND_RUN.bat build Release, so these
// macros always evaluate their expression and report every failure.

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace abyss::test {

struct TestCase {
    const char *name;
    void (*run)();
};

inline std::vector<TestCase> &registry()
{
    static std::vector<TestCase> cases;
    return cases;
}

inline int &failureCount()
{
    static int failures = 0;
    return failures;
}

struct Registrar {
    Registrar(const char *name, void (*run)()) { registry().push_back({name, run}); }
};

// Reads an environment variable (opt-in integration tests); empty if unset.
inline std::string environment(const char *name)
{
#ifdef _MSC_VER
    char *value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || !value)
        return {};
    std::string result(value);
    free(value);
    return result;
#else
    const char *value = std::getenv(name);
    return value ? value : "";
#endif
}

inline void reportFailure(const char *expression, const char *file, int line)
{
    ++failureCount();
    std::cerr << file << '(' << line << "): CHECK failed: " << expression << '\n';
}

// Runs every registered test and returns the process exit code.
inline int runAll()
{
    for (const auto &test : registry()) {
        const int before = failureCount();
        test.run();
        // Flush per test so a hang or crash still shows how far the run got.
        std::cout << (failureCount() == before ? "[pass] " : "[FAIL] ") << test.name << std::endl;
    }
    std::cout << registry().size() << " test(s), " << failureCount() << " failed check(s)" << std::endl;
    return failureCount() == 0 ? 0 : 1;
}

} // namespace abyss::test

#define ABYSS_TEST_CONCAT_INNER(a, b) a##b
#define ABYSS_TEST_CONCAT(a, b) ABYSS_TEST_CONCAT_INNER(a, b)

#define TEST_CASE(name)                                                                   \
    static void ABYSS_TEST_CONCAT(abyssTest_, __LINE__)();                                \
    static const ::abyss::test::Registrar ABYSS_TEST_CONCAT(abyssTestRegistrar_, __LINE__)( \
        name, &ABYSS_TEST_CONCAT(abyssTest_, __LINE__));                                  \
    static void ABYSS_TEST_CONCAT(abyssTest_, __LINE__)()

#define CHECK(expression)                                                         \
    do {                                                                          \
        if (!(expression))                                                        \
            ::abyss::test::reportFailure(#expression, __FILE__, __LINE__);        \
    } while (false)

// Like CHECK, but stops the current test so later lines cannot dereference
// something the failed check was guarding.
#define REQUIRE(expression)                                                       \
    do {                                                                          \
        if (!(expression)) {                                                      \
            ::abyss::test::reportFailure(#expression, __FILE__, __LINE__);        \
            return;                                                               \
        }                                                                         \
    } while (false)
