#include "../logic/SectionSnapshotChecks.h"
#include <cstdio>
int main() {
    std::size_t checks{}, failures{};
    lholo::tests::runSectionSnapshotChecks([&](bool ok) { ++checks; if (!ok) ++failures; });
    std::printf("SectionSnapshot: %zu checks, %zu failures\n", checks, failures);
    return failures ? 1 : 0;
}
