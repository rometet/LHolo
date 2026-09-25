// Build: c++ -std=c++20 -pthread -Isrc tests/manual_placement/portable_main.cpp src/place/PlacementState.cpp -o manual-placement-tests
#include "../logic/ManualPlacementChecks.h"
#include <cstdio>
int main() {
    int checks = 0, failures = 0;
    lholo::tests::runManualPlacementChecks([&](bool ok) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "Check %d failed\n", checks); }
    });
    std::printf("ManualPlacementChecks: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
