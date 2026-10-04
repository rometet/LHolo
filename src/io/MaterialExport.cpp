#include "io/MaterialExport.h"
#include "io/AtomicOutput.h"
#include <chrono>
#include <fstream>
#include <stdexcept>

namespace lholo::io {
namespace {
// Quoted TSV follows the CSV quote rule. Embedded tabs/newlines stay inside
// one field, quotes double, records use CRLF. Prefix all text with an apostrophe
// when it could be interpreted as a spreadsheet formula.
std::string field(std::string value) {
    if (!value.empty() && (value[0]=='=' || value[0]=='+' || value[0]=='-' || value[0]=='@'
        || value[0]=='\t' || value[0]=='\r' || value[0]=='\n')) value.insert(value.begin(), '\'');
    std::string result{"\""};
    for (char c : value) { if (c == '\"') result += '\"'; result += c; }
    return result + '\"';
}
}
bool writeMaterialTsv(std::filesystem::path const& output, MaterialExportRequest const& request,
                      MaterialExportCancel const& cancellation) {
    if (!request.snapshot || request.names.size() != request.snapshot->requirements.size())
        throw std::invalid_argument("Invalid material export snapshot");
    auto cancelled = [&] { return cancellation && cancellation->cancelled(); };
    if (cancelled()) return false;
    auto const& snapshot = *request.snapshot;
    auto const summary = structure::detail::summarizeMaterials(snapshot);
    return writeOutputAtomically(output, [&](auto const& staged) {
        std::ofstream stream(staged, std::ios::binary | std::ios::trunc);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        stream << "\xEF\xBB\xBF";
        stream << "LHolo Materials\tUTF-8 quoted TSV\r\n";
        stream << "captured_at\t" << field(request.capturedAt) << "\r\nsource\t" << field(snapshot.source);
        stream << "\r\ngeneration\t" << snapshot.scope.generation << "\r\nsnapshot_revision\t" << snapshot.revision;
        stream << "\r\nfilter\t" << (request.filter==structure::detail::MaterialFilter::Shortage ? "inventory_shortage" :
            request.filter==structure::detail::MaterialFilter::Ignored ? "ignored" : "all");
        stream << "\r\noriginal_total\t" << summary.original << "\r\nworking_total\t" << summary.working
            << "\r\nexcluded_total\t" << summary.excluded << "\r\nexcluded_kinds\t" << summary.excludedKinds;
        stream << "\r\nunknown_inventory_kinds\t" << summary.unknownKinds;
        stream << "\r\nmeaning\t\"Inventory shortage compares whole-blueprint requirements to carried inventory; it is not Verifier missing blocks. Unknown counts are empty fields. Counts include decoded local shulker contents; absent box NBT cannot distinguish empty from unavailable. Ignore is a session work selection.\"\r\n";
        stream << "材料名\t材料キー\tブロックID\tアイテムID\t元必要数\t所持数\t在庫不足\t除外\t作業必要数\r\n";
        for (std::size_t index=0; index<snapshot.requirements.size(); ++index) {
            if (cancelled()) return false;
            if (!snapshot.matches(index, request.filter)) continue;
            auto const& row = snapshot.requirements[index]; bool const ignored=snapshot.isIgnored(index);
            stream << field(request.names[index]) << '\t' << field(row.key) << '\t' << field(row.typeName)
                << '\t' << field(row.itemId) << '\t' << row.count << '\t';
            if (index<snapshot.available.size() && snapshot.available[index]) stream << std::max(0, *snapshot.available[index]);
            stream << '\t'; if (auto const shortage=snapshot.shortage(index)) stream << *shortage;
            stream << '\t' << (ignored ? "true" : "false") << '\t' << (ignored ? 0 : row.count) << "\r\n";
        }
        stream.flush(); stream.close(); return !cancelled();
    });
}
bool MaterialExportJob::start(MaterialExportRequest request, ChooseDestination choose) {
    std::lock_guard lock(mMutex);
    pollLocked();
    if (!mAccepting || mFuture.valid() || !request.snapshot || !choose) return false;
    mResult = {MaterialExportPhase::Saving, {}, {}, request.snapshot->scope, request.snapshot->source};
    try {
        mCancellation=std::make_shared<MaterialExportCancellation>();
        mFuture=std::async(std::launch::async, [request=std::move(request), choose=std::move(choose), cancellation=mCancellation] {
        MaterialExportResult result; result.scope=request.snapshot->scope;result.source=request.snapshot->source;
        try {
            if (cancellation->cancelled()) { result.phase=MaterialExportPhase::Cancelled; return result; }
            auto selected=choose(cancellation);
            if (!selected || cancellation->cancelled()) { result.phase=MaterialExportPhase::Cancelled; return result; }
            result.destination=*selected;
            result.phase=writeMaterialTsv(*selected, request, cancellation)
                ? MaterialExportPhase::Saved : MaterialExportPhase::Cancelled;
        } catch (std::exception const& error) { result.phase=MaterialExportPhase::Failed; result.error=error.what(); }
        catch (...) { result.phase=MaterialExportPhase::Failed; result.error="Unknown export error"; }
        return result;
        }).share();
    } catch (std::exception const& error) {
        mResult.phase=MaterialExportPhase::Failed;mResult.error=error.what();return false;
    }
    return true;
}
void MaterialExportJob::pollLocked() {
    if (mFuture.valid() && mFuture.wait_for(std::chrono::milliseconds{0})==std::future_status::ready) {
        try { mResult=mFuture.get(); }
        catch (std::exception const& error) { mResult.phase=MaterialExportPhase::Failed;mResult.error=error.what(); }
        catch (...) { mResult.phase=MaterialExportPhase::Failed;mResult.error="Unknown export error"; }
        // Readiness of a launch::async future synchronizes with worker thread
        // completion. Release captured functions/context only after that gate.
        mFuture={};mCancellation.reset();
    }
}
MaterialExportResult MaterialExportJob::poll() {
    std::lock_guard lock(mMutex);
    pollLocked();
    auto result=mResult;result.accepting=mAccepting;return result;
}
bool MaterialExportJob::closeAndDrain(std::chrono::milliseconds budget) {
    std::shared_future<MaterialExportResult> future;
    {
        std::lock_guard lock(mMutex);
        mAccepting=false;
        if (mCancellation) mCancellation->cancel();
        future=mFuture;
    }
    // No mutex is held during the bounded wait, so Present/poll cannot wait
    // for the dialog or disk. On timeout the member keeps the shared state
    // owned even after this local copy dies. Never detach/discard that member.
    if (future.valid() && future.wait_for(std::max(std::chrono::milliseconds{0}, budget))
        !=std::future_status::ready) return false;
    std::lock_guard lock(mMutex);
    pollLocked();
    return !mFuture.valid();
}
bool MaterialExportJob::openSession() {
    std::lock_guard lock(mMutex);
    pollLocked();
    if (mFuture.valid()) return false;
    mAccepting=true;
    return true;
}
MaterialExportJob& materialExportJob() { static MaterialExportJob job; return job; }
} // namespace lholo::io
