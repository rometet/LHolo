// LHolo Integrated Sync — client cache and catalog state tests.
//
// ClientCache: content-addressed paths, size/SHA verification, atomic publish,
// deduplication, containment. SharedCatalog: snapshot baseline, ordered delta
// application, duplicate/stale ignore and gap detection (§4.6, §5.4).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "sync/common/ChunkAssembly.h"
#include "sync/common/PathSupport.h"
#include "sync/common/Sha256.h"
#include "sync/common/SharedTypes.h"
#include "sync/common/TransferSender.h"
#include "sync/client/ClientCache.h"
#include "sync/client/SharedCatalog.h"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, char const* expression, char const* file, int line) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL %s:%d: %s\n", file, line, expression);
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)

using namespace lholo::sync;
using lholo::sync::control::CatalogDelta;

std::filesystem::path makeTempRoot() {
    std::random_device device;
    auto root = std::filesystem::temp_directory_path()
        / ("lholo-sync-clienttest-" + std::to_string(device()) + "-" + std::to_string(device()));
    std::error_code code;
    std::filesystem::remove_all(root, code);
    std::filesystem::create_directories(root, code);
    return root;
}

void writeFile(std::filesystem::path const& path, std::string const& content) {
    std::error_code code;
    std::filesystem::create_directories(paths::ioPath(path.parent_path()), code);
    std::ofstream out(paths::ioPath(path), std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

Sha256Hash hashOf(std::string const& content) {
    return sha256::hash(content.data(), content.size());
}

SharedPlacementDto makePlacement(std::string const& name, bool deleted = false) {
    SharedPlacementDto dto;
    dto.placementId = Uuid::random();
    dto.fileHash = hashOf(name);
    dto.format = SchematicFormat::Litematic;
    dto.displayName = name;
    dto.dimension = 0;
    dto.ownerId = "owner";
    dto.revision = 1;
    dto.updatedAtUnixMs = 1000;
    dto.deleted = deleted;
    return dto;
}

void testClientCache(std::filesystem::path const& root) {
    ClientCache cache(root / "schematics");
    CHECK(ClientCache::endpointKey("127.0.0.1", 19142) == ClientCache::endpointKey("127.0.0.1", 19142));
    CHECK(ClientCache::endpointKey("127.0.0.1", 19142) != ClientCache::endpointKey("127.0.0.1", 19143));
    CHECK(ClientCache::endpointKey("127.0.0.1", 19142).size() == 16);

    Uuid const serverId = Uuid::random();
    Uuid const worldId = Uuid::random();
    std::string const endpoint = ClientCache::endpointKey("127.0.0.1", 19142);

    std::string const content = "litematic-bytes-0123456789";
    auto const hash = hashOf(content);

    auto const part = cache.partPath(endpoint, serverId, worldId, hash, SchematicFormat::Litematic);
    CHECK(!part.empty());
    writeFile(part, content);

    std::string error;
    auto published = cache.publishPart(
        endpoint, serverId, worldId, hash, SchematicFormat::Litematic, content.size(), error
    );
    if (!published) {
        std::printf("publishPart error: %s\n", error.c_str());
    }
    CHECK(published.has_value());
    if (!published) {
        return;
    }
    CHECK(std::filesystem::is_regular_file(paths::ioPath(*published)));
    CHECK(!std::filesystem::exists(paths::ioPath(part))); // .part removed after publish

    auto verified = cache.findVerified(
        endpoint, serverId, worldId, hash, SchematicFormat::Litematic, content.size()
    );
    CHECK(verified.has_value() && *verified == *published);
    // Wrong expected size is not accepted.
    CHECK(!cache.findVerified(endpoint, serverId, worldId, hash, SchematicFormat::Litematic, content.size() + 1).has_value());

    // Re-download with identical content deduplicates and keeps one object.
    writeFile(cache.partPath(endpoint, serverId, worldId, hash, SchematicFormat::Litematic), content);
    auto again = cache.publishPart(
        endpoint, serverId, worldId, hash, SchematicFormat::Litematic, content.size(), error
    );
    CHECK(again.has_value() && *again == *published);

    // Tampered .part is rejected and never published.
    std::string const tampered = content + "X";
    auto const badHash = hashOf(tampered);
    writeFile(cache.partPath(endpoint, serverId, worldId, badHash, SchematicFormat::Litematic), content);
    CHECK(!cache.publishPart(
        endpoint, serverId, worldId, badHash, SchematicFormat::Litematic, content.size(), error
    ).has_value());

    // A corrupted existing object fails verification on lookup.
    {
        std::ofstream out(paths::ioPath(*published), std::ios::binary | std::ios::trunc);
        out << "corrupted";
    }
    CHECK(!cache.findVerified(endpoint, serverId, worldId, hash, SchematicFormat::Litematic, content.size()).has_value());

    // Invalid components never produce paths.
    CHECK(cache.partPath("bad/key", serverId, worldId, hash, SchematicFormat::Litematic).empty());
    CHECK(cache.partPath(endpoint, Uuid{}, worldId, hash, SchematicFormat::Litematic).empty());

    // Removing a cached object only touches the cache copy.
    writeFile(cache.partPath(endpoint, serverId, worldId, hash, SchematicFormat::Litematic), content);
    CHECK(cache.publishPart(endpoint, serverId, worldId, hash, SchematicFormat::Litematic, content.size(), error).has_value());
    CHECK(cache.removeCached(endpoint, serverId, worldId, hash, SchematicFormat::Litematic));
}

void testSharedCatalog() {
    SharedCatalog catalog;

    // Deltas before a baseline require a reload.
    CatalogDelta early;
    early.sequence = 1;
    early.action = "upsert";
    early.placement = makePlacement("early");
    CHECK(catalog.applyDelta(early) == DeltaResult::Gap);

    auto a = makePlacement("alpha");
    auto b = makePlacement("beta");
    auto tombstone = makePlacement("gone", true);
    catalog.applySnapshot({a, b, tombstone}, 10);
    CHECK(catalog.hasSnapshot());
    CHECK(catalog.sequence() == 10);
    CHECK(catalog.activeCount() == 2); // tombstone filtered
    CHECK(catalog.find(a.placementId).has_value());
    CHECK(!catalog.find(tombstone.placementId).has_value());

    // Next sequence upserts a new placement.
    auto c = makePlacement("gamma");
    CatalogDelta add;
    add.sequence = 11;
    add.action = "upsert";
    add.placement = c;
    CHECK(catalog.applyDelta(add) == DeltaResult::Applied);
    CHECK(catalog.sequence() == 11);
    CHECK(catalog.activeCount() == 3);

    // Update to an existing placement replaces fields.
    auto updated = a;
    updated.displayName = "alpha-renamed";
    updated.revision = 2;
    CatalogDelta update;
    update.sequence = 12;
    update.action = "upsert";
    update.placement = updated;
    CHECK(catalog.applyDelta(update) == DeltaResult::Applied);
    CHECK(catalog.find(a.placementId)->displayName == "alpha-renamed");

    // Duplicate and stale are ignored without changing the sequence.
    CHECK(catalog.applyDelta(update) == DeltaResult::Ignored);
    CatalogDelta stale;
    stale.sequence = 5;
    stale.action = "delete";
    stale.placement = b;
    CHECK(catalog.applyDelta(stale) == DeltaResult::Ignored);
    CHECK(catalog.sequence() == 12);

    // A gap is reported and nothing is applied.
    CatalogDelta gap;
    gap.sequence = 14;
    gap.action = "delete";
    gap.placement = b;
    CHECK(catalog.applyDelta(gap) == DeltaResult::Gap);
    CHECK(catalog.sequence() == 12);
    CHECK(catalog.find(b.placementId).has_value());

    // Delete at the exact next sequence removes the entry.
    CatalogDelta removeB;
    removeB.sequence = 13;
    removeB.action = "delete";
    removeB.placement = b;
    CHECK(catalog.applyDelta(removeB) == DeltaResult::Applied);
    CHECK(!catalog.find(b.placementId).has_value());
    CHECK(catalog.sequence() == 13);

    // Unknown action is rejected.
    CatalogDelta bogus;
    bogus.sequence = 14;
    bogus.action = "frobnicate";
    bogus.placement = b;
    CHECK(catalog.applyDelta(bogus) == DeltaResult::Invalid);

    catalog.clear();
    CHECK(!catalog.hasSnapshot());
    CHECK(catalog.activeCount() == 0);
}

void testTransferPipeline(std::filesystem::path const& root) {
    std::string content(5000, 'z');
    for (std::size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<char>('A' + (i % 26));
    }
    auto const path = root / "upload.bin";
    writeFile(path, content);
    auto const hash = hashOf(content);

    TransferSender sender;
    std::string error;
    CHECK(sender.open(path, content.size(), 1024, 4, error));

    ChunkAssembly assembly;
    ChunkAssemblyLimits limits;
    limits.chunkBytes = 1024;
    limits.window = 4;
    CHECK(assembly.prepare(Uuid::random(), hash, content.size(), 1024, 4, limits, error));

    int guard = 0;
    while (!sender.allAcked() && guard++ < 1000) {
        auto const chunks = sender.poll(4096);
        for (auto const& chunk : chunks) {
            auto const result = assembly.onChunk(chunk.offset, chunk.data, error);
            CHECK(result == ChunkAssembly::Chunk::Accepted || result == ChunkAssembly::Chunk::Duplicate);
        }
        auto const ack = assembly.acknowledgement();
        sender.onAck(ack.nextOffset, ack.receivedBitmap);
        if (chunks.empty() && sender.allSent()) {
            break;
        }
    }
    CHECK(assembly.assembled());
    CHECK(assembly.bytes() == std::vector<std::uint8_t>(content.begin(), content.end()));
    CHECK(sender.allAcked());
    CHECK(sha256::hash(assembly.bytes().data(), assembly.bytes().size()) == hash);

    // Resume starts at the requested chunk boundary.
    TransferSender resumed;
    CHECK(resumed.open(path, content.size(), 1024, 4, error));
    resumed.setStartOffset(2048);
    auto const first = resumed.poll(4096);
    CHECK(!first.empty());
    CHECK(!first.empty() && first.front().offset == 2048);
}

} // namespace

int main() {
    auto const root = makeTempRoot();
    testClientCache(root);
    testSharedCatalog();
    testTransferPipeline(root);
    std::error_code code;
    std::filesystem::remove_all(root, code);

    std::printf("LHoloSyncClientTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
