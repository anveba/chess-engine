#include <chrono>
#include <fstream>

#include "board.h"
#include "testing.h"

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "tests/data"
#endif

std::string testing::data_path(const std::string& file)
{
    return std::string(TEST_DATA_DIR) + "/" + file;
}

int main(int argc, char** argv)
{
    precompute_bitboards();
    precompute_board_constants();

    const std::string filter = argc > 1 ? argv[1] : "";
    int run = 0, failed = 0;

    for (const testing::TestCase& test : testing::registry()) {
        if (test.name.find(filter) == std::string::npos)
            continue;

        std::cout << "[ RUN  ] " << test.name << std::endl;
        const int failures_before = testing::failures();
        const auto start = std::chrono::steady_clock::now();

        test.run();

        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        const bool ok = testing::failures() == failures_before;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << test.name << " (" << ms << " ms)" << std::endl;
        run++;
        failed += !ok;
    }

    std::cout << "\n" << run - failed << "/" << run << " tests passed." << std::endl;
    return failed == 0 && run > 0 ? 0 : 1;
}
