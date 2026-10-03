#pragma once

#include "structure/VerificationSelection.h"

namespace lholo::structure::schematic {
enum class VerificationPhase { NotVerified, Queued, Running, Completed, Cancelled };

// Value-only control shared by UI requests and the game tick. Native jobs stay
// on the tick thread. Neither a completed tick nor a menu/filter change can
// create a request. A queued request waits for its structure to finish loading.
class ManualVerificationControl {
    VerificationPhase phase_{VerificationPhase::NotVerified};
    ReportStamp context_;
    std::uint64_t serial_{};
    static bool matches(ReportStamp a, ReportStamp b, bool loading) {
        a.reportRevision=b.reportRevision=0;
        a.filterRevision=b.filterRevision=0;
        if (loading && !a.loadedGeneration) a.loadedGeneration=b.loadedGeneration;
        return a==b;
    }
public:
    VerificationPhase phase() const { return phase_; }
    bool busy() const { return phase_==VerificationPhase::Queued || phase_==VerificationPhase::Running; }
    bool request(ReportStamp const& context) {
        if (busy() || !context.worldEpoch || !context.placementId) return false;
        context_=context;
        if (++serial_==0) ++serial_;
        phase_=VerificationPhase::Queued;
        return true;
    }
    bool belongsTo(ReportStamp const& context) const {
        return matches(context_,context,phase_==VerificationPhase::Queued);
    }
    std::uint64_t start(ReportStamp const& context) {
        if (phase_!=VerificationPhase::Queued || !context.loadedGeneration || !belongsTo(context)) return 0;
        context_=context;
        phase_=VerificationPhase::Running;
        return serial_;
    }
    bool current(std::uint64_t serial, ReportStamp const& context) const {
        return phase_==VerificationPhase::Running && serial==serial_ && belongsTo(context);
    }
    bool finish(std::uint64_t serial, ReportStamp const& context) {
        if (!current(serial,context)) return false;
        phase_=VerificationPhase::Completed;
        return true;
    }
    void cancel() {
        if (!busy()) return;
        ++serial_;
        phase_=VerificationPhase::Cancelled;
    }
    void invalidate() {
        ++serial_;
        context_={};
        phase_=VerificationPhase::NotVerified;
    }
};
} // namespace lholo::structure::schematic
