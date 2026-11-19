#include "ui/SharedMutationGuard.h"

#include <iostream>

int main() {
    using namespace lholo::ui;
    namespace sync = lholo::sync;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, char const* label) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << "FAIL: " << label << '\n';
        }
    };

    SharedMutationGuard guard;
    sync::Uuid a{}, b{};
    a.bytes[0] = 1;
    b.bytes[0] = 2;
    SharedEditViewState draft;
    draft.name = "test";
    draft.x = 10;
    draft.y = 64;
    draft.z = -20;
    draft.rotation = 2;

    check(!guard.unlocked, "locked initially");
    check(!guard.approveCommit(a, draft), "locked cannot commit");
    guard.resetForSession(1);
    check(!guard.unlocked, "new session locked");

    guard.unlock();
    check(!guard.approveCommit(a, draft), "first click only reviews");
    check(guard.commitArmed(a, draft), "review armed");
    check(!guard.commitArmed(b, draft), "other placement not armed");
    check(guard.approveCommit(a, draft), "second click approves exact draft");
    check(!guard.unlocked, "auto-lock after commit");
    check(!guard.commitArmed(a, draft), "commit token cleared");

    guard.unlock();
    check(!guard.approveCommit(a, draft), "new review");
    draft.x++;
    check(!guard.commitArmed(a, draft), "position change invalidates review");
    check(!guard.approveCommit(a, draft), "changed draft must re-review");
    check(guard.approveCommit(a, draft), "changed draft confirmation");

    guard.unlock();
    check(!guard.approveCommit(a, draft), "review again");
    draft.name += "|new";
    check(!guard.commitArmed(a, draft), "name change invalidates review");
    draft.name = "test";
    draft.x--;
    check(!guard.commitArmed(a, draft), "reverting to original token remains disarmed");

    guard.lock();
    check(!guard.unlocked, "manual lock");
    check(!guard.commitArmed(a, draft), "manual lock clears pending");
    guard.unlock();
    check(!guard.approveCommit(a, draft), "new review after manual lock");
    guard.resetForSession(2);
    check(!guard.unlocked, "world transition automatically locks");
    guard.unlock();
    guard.resetForSession(2);
    check(guard.unlocked, "same world epoch does not relock every frame");
    guard.resetForSession(3);
    check(!guard.unlocked, "new world relocks");

    std::cout << "LHoloSharedGuardTests: " << checks << " checks, "
              << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
