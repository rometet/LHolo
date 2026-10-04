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
void writeMaterialTsv(std::filesystem::path const& output, MaterialExportRequest const& request) {
    if (!request.snapshot || request.names.size() != request.snapshot->requirements.size())
        throw std::invalid_argument("Invalid material export snapshot");
    auto const& snapshot = *request.snapshot;
    auto const summary = structure::detail::summarizeMaterials(snapshot);
    writeOutputAtomically(output, [&](auto const& staged) {
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
        stream << "\r\nmeaning\t\"Inventory shortage compares whole-blueprint requirements to carried inventory; it is not Verifier missing blocks. Unknown counts are empty fields. Ignore is a session work selection.\"\r\n";
        stream << "材料名\t材料キー\tブロックID\tアイテムID\t元必要数\t所持数\t在庫不足\t除外\t作業必要数\r\n";
        for (std::size_t index=0; index<snapshot.requirements.size(); ++index) {
            if (!snapshot.matches(index, request.filter)) continue;
            auto const& row = snapshot.requirements[index]; bool const ignored=snapshot.isIgnored(index);
            stream << field(request.names[index]) << '\t' << field(row.key) << '\t' << field(row.typeName)
                << '\t' << field(row.itemId) << '\t' << row.count << '\t';
            if (index<snapshot.available.size() && snapshot.available[index]) stream << std::max(0, *snapshot.available[index]);
            stream << '\t'; if (auto const shortage=snapshot.shortage(index)) stream << *shortage;
            stream << '\t' << (ignored ? "true" : "false") << '\t' << (ignored ? 0 : row.count) << "\r\n";
        }
        stream.flush(); stream.close(); return true;
    });
}
bool MaterialExportJob::start(MaterialExportRequest request, ChooseDestination choose) {
    poll();
    if (mFuture || !request.snapshot || !choose) return false;
    mResult = {MaterialExportPhase::Saving, {}, {}, request.snapshot->scope, request.snapshot->source};
    try { mFuture.emplace(std::async(std::launch::async, [request=std::move(request), choose=std::move(choose)] {
        MaterialExportResult result; result.scope=request.snapshot->scope;result.source=request.snapshot->source;
        try {
            auto selected=choose();
            if (!selected) { result.phase=MaterialExportPhase::Cancelled; return result; }
            result.destination=*selected;
            writeMaterialTsv(*selected, request); result.phase=MaterialExportPhase::Saved;
        } catch (std::exception const& error) { result.phase=MaterialExportPhase::Failed; result.error=error.what(); }
        catch (...) { result.phase=MaterialExportPhase::Failed; result.error="Unknown export error"; }
        return result;
    })); } catch (std::exception const& error) {
        mResult.phase=MaterialExportPhase::Failed;mResult.error=error.what();return false;
    }
    return true;
}
MaterialExportResult MaterialExportJob::poll() {
    if (mFuture && mFuture->wait_for(std::chrono::milliseconds{0})==std::future_status::ready) {
        auto future=std::move(*mFuture);mFuture.reset();
        try { mResult=future.get(); }
        catch (std::exception const& error) { mResult.phase=MaterialExportPhase::Failed;mResult.error=error.what(); }
    }
    return mResult;
}
void MaterialExportJob::shutdown() {
    if (mFuture) { auto future=std::move(*mFuture);mFuture.reset();mResult=future.get(); }
}
MaterialExportJob& materialExportJob() { static MaterialExportJob job; return job; }
} // namespace lholo::io
