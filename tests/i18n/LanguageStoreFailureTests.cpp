#include "i18n/LanguageStore.h"
#include "i18n/Translator.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
thread_local bool gTrack{};
thread_local int gCallsUntilFailure{-1};
std::size_t gTrackedOutstanding{};
struct alignas(__STDCPP_DEFAULT_NEW_ALIGNMENT__) AllocationHeader { bool tracked; };
int gChecks{}, gFailures{};
void check(bool ok, char const* name) {
    ++gChecks;
    if (!ok) { ++gFailures; std::fprintf(stderr, "FAIL %s\n", name); }
}
} // namespace

void* operator new(std::size_t size) {
    if (gCallsUntilFailure == 0) {
        gCallsUntilFailure = -1;
        throw std::bad_alloc{};
    }
    if (gCallsUntilFailure > 0) --gCallsUntilFailure;
    if (size > (std::numeric_limits<std::size_t>::max)() - sizeof(AllocationHeader)) throw std::bad_alloc{};
    auto* header = static_cast<AllocationHeader*>(std::malloc(sizeof(AllocationHeader) + (size ? size : 1)));
    if (!header) throw std::bad_alloc{};
    header->tracked = gTrack;
    if (header->tracked) ++gTrackedOutstanding;
    return header + 1;
}
void operator delete(void* memory) noexcept {
    if (!memory) return;
    auto* header = static_cast<AllocationHeader*>(memory) - 1;
    if (header->tracked) --gTrackedOutstanding;
    std::free(header);
}
void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }

int main() {
    using namespace lholo::i18n;
    initLanguageStore(); // Warm process-lifetime tables, including idToKey.
    auto const original = languages();
    auto const text = lookupText(TextKey::PageProjection, defaultLanguage());
    check(!original.empty() && text && *text, "embedded language baseline available");
    for (auto const allowedAllocations : {1, 2, 10, 50, 250}) {
        gTrack = true;
        gCallsUntilFailure = allowedAllocations;
        bool rejected{};
        try { initLanguageStore(); } catch (std::bad_alloc const&) { rejected = true; }
        gTrack = false;
        gCallsUntilFailure = -1;
        check(rejected, "injected language construction allocation failure observed");
        check(gTrackedOutstanding == 0, "failed language initialization releases every partial allocation");
        check(languages().data() == original.data() && lookupText(TextKey::PageProjection, defaultLanguage()) == text,
            "failed language initialization preserves preceding immutable publication");
    }
    std::printf("LHoloLanguageStoreTests: %d checks, %d failures, %zu tracked allocations outstanding\n",
        gChecks, gFailures, gTrackedOutstanding);
    return gFailures ? 1 : 0;
}
