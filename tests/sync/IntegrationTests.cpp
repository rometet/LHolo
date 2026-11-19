// LHolo Integrated Sync — server <-> client data-path integration test.
//
// Drives the actual components the live peers use, without the network
// transport: upload assembly -> FileStore publish -> repository file/placement
// -> LIST_PAGE encode/decode -> SharedCatalog -> CATALOG_DELTA -> placement
// update -> FILE_GET download streaming (TransferSender <-> ChunkAssembly) ->
// ClientCache verification/publish. This proves the payload contracts agree
// between the server and client code paths.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "sync/common/ChunkAssembly.h"
#include "sync/common/ControlMessages.h"
#include "sync/common/FrameCodec.h"
#include "sync/common/PathSupport.h"
#include "sync/common/Protocol.h"
#include "sync/common/Sha256.h"
#include "sync/common/SharedTypes.h"
#include "sync/common/TransferSender.h"

#include "sync/client/ClientCache.h"
#include "sync/client/SharedCatalog.h"

#include "FileStore.h"
#include "PlacementRepository.h"
#include "TransferReceiver.h"

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

std::filesystem::path makeTempRoot() {
    std::random_device device;
    auto root = std::filesystem::temp_directory_path()
        / ("lholo-sync-integration-" + std::to_string(device()) + "-" + std::to_string(device()));
    std::error_code code;
    std::filesystem::remove_all(root, code);
    std::filesystem::create_directories(root, code);
    return root;
}

std::vector<std::uint8_t> makeBytes(std::size_t size, std::uint8_t seed) {
    std::vector<std::uint8_t> bytes(size);
    std::uint32_t state = 0x12345678u ^ seed;
    for (auto& byte : bytes) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 16);
    }
    return bytes;
}

void writeFile(std::filesystem::path const& path, std::vector<std::uint8_t> const& bytes) {
    std::error_code code;
    std::filesystem::create_directories(paths::ioPath(path.parent_path()), code);
    std::ofstream out(paths::ioPath(path), std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> readFile(std::filesystem::path const& path) {
    std::ifstream in(paths::ioPath(path), std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    auto const root = makeTempRoot();
    std::filesystem::path const dbPath = root / "data" / "sync.db";
    std::string error;
    int failuresBefore = gFailures;

    // --- server stores -----------------------------------------------------
    PlacementRepository repo;
    CHECK(repo.open(dbPath, error));
    FileStoreLimits storeLimits;
    FileStore store(root / "data" / "objects", root / "data" / "staging", storeLimits);

    // --- upload: sender -> receiver -> verified publication ----------------
    auto const content = makeBytes(70000, 7);
    auto const hash = sha256::hash(content.data(), content.size());
    auto const sourcePath = root / "client" / "build.mcstructure";
    writeFile(sourcePath, content);
    auto const size = static_cast<std::uint64_t>(content.size());

    TransferReceiver receiver(store, TransferLimits{});
    TransferReceiver::Offer offer;
    CHECK(receiver.prepare(Uuid::random(), hash, SchematicFormat::Mcstructure, size, 49152, offer, error));
    CHECK(offer.needBytes);

    {
        TransferSender sender;
        CHECK(sender.open(sourcePath, size, 49152, 4, error));
        int guard = 0;
        while (!sender.allAcked() && guard++ < 1000) {
            auto const chunks = sender.poll(1u << 20);
            for (auto const& chunk : chunks) {
                auto const result = receiver.onChunk(chunk.offset, chunk.data, error);
                CHECK(result == TransferReceiver::Chunk::Accepted || result == TransferReceiver::Chunk::Duplicate);
            }
            auto const ack = receiver.acknowledgement();
            sender.onAck(ack.nextOffset, ack.receivedBitmap);
        }
    }
    CHECK(receiver.complete(error));
    CHECK(store.hasObject(hash, SchematicFormat::Mcstructure, size));

    bool existed = false;
    FileRecord record;
    record.sha256 = hash;
    record.format = SchematicFormat::Mcstructure;
    record.sizeBytes = size;
    record.createdAtMs = 1000;
    record.validated = true;
    CHECK(repo.insertFile(record, existed, error));
    repo.setFileValidated(hash, true);

    // --- create placement + LIST payload -> client catalog -----------------
    PlacementRepository::PlacementInput input;
    input.fileHash = hash;
    input.format = SchematicFormat::Mcstructure;
    input.displayName = "integration";
    input.dimension = 0;
    input.originX = -120;
    input.originY = 64;
    input.originZ = 300;
    input.rotation = 1;
    input.mirror = 2;
    auto created = repo.createPlacement(input, "owner-a", 2000, error);
    CHECK(created.has_value());

    auto const page = repo.listPage(std::nullopt, 50);
    CHECK(page.items.size() == 1);
    control::ListPage listPage;
    listPage.items = page.items;
    listPage.nextCursor = page.nextCursor;
    listPage.snapshotSequence = page.snapshotSequence;
    listPage.hasMore = page.hasMore;
    auto const listPayload = control::encode(listPage);
    control::ListPage clientPage;
    CHECK(control::decode(listPayload, clientPage).ok);

    SharedCatalog catalog;
    catalog.applySnapshot(clientPage.items, clientPage.snapshotSequence);
    CHECK(catalog.sequence() == page.snapshotSequence);
    auto found = catalog.find(created->placementId);
    CHECK(found.has_value());
    CHECK(found && found->originX == -120 && found->originZ == 300);
    CHECK(found && found->rotation == 1 && found->mirror == 2);
    CHECK(found && found->format == SchematicFormat::Mcstructure);

    // --- CATALOG_DELTA upsert/update agreement -----------------------------
    PlacementRepository::PlacementInput moved = input;
    moved.originX = -110;
    SharedPlacementDto updated;
    PlacementRepository::Mutation mutation{};
    CHECK(repo.updatePlacement(created->placementId, 1, moved, "owner-a", false, 3000, updated, mutation, error));
    CHECK(mutation == PlacementRepository::Mutation::Ok);

    control::CatalogDelta delta;
    delta.sequence = repo.currentSequence();
    delta.action = "upsert";
    delta.placement = updated;
    auto const deltaPayload = control::encode(delta);
    control::CatalogDelta clientDelta;
    CHECK(control::decode(deltaPayload, clientDelta).ok);
    CHECK(catalog.applyDelta(clientDelta) == DeltaResult::Applied);
    CHECK(catalog.sequence() == repo.currentSequence());
    auto afterDelta = catalog.find(created->placementId);
    CHECK(afterDelta && afterDelta->originX == -110);
    CHECK(afterDelta && afterDelta->revision == 2);

    // Replayed delta is ignored.
    CHECK(catalog.applyDelta(clientDelta) == DeltaResult::Ignored);

    // --- download: FILE_GET stream -> client cache ------------------------
    TransferSender downloader;
    CHECK(downloader.open(store.objectPath(hash, SchematicFormat::Mcstructure), size, 49152, 4, error));
    ChunkAssembly assembly;
    ChunkAssemblyLimits assemblyLimits;
    assemblyLimits.chunkBytes = 49152;
    assemblyLimits.window = 4;
    CHECK(assembly.prepare(Uuid::random(), hash, size, 49152, 4, assemblyLimits, error));

    int guard = 0;
    while (!downloader.allSent() || !downloader.allAcked()) {
        if (guard++ > 2000) {
            break;
        }
        auto const chunks = downloader.poll(1u << 20);
        for (auto const& chunk : chunks) {
            // Serialize exactly like FILE_CHUNK: transfer uuid + offset + len + data.
            std::vector<std::uint8_t> wire;
            std::array<std::uint8_t, kUuidBytes> const transfer{};
            CHECK(encodeFileChunk(transfer, chunk.offset, chunk.data, wire));
            FileChunkPayload decodedChunk;
            std::string chunkError;
            CHECK(decodeFileChunk(wire, decodedChunk, chunkError));
            auto const result = assembly.onChunk(decodedChunk.offset, decodedChunk.data, chunkError);
            CHECK(result == ChunkAssembly::Chunk::Accepted || result == ChunkAssembly::Chunk::Duplicate);
        }
        auto const ack = assembly.acknowledgement();
        downloader.onAck(ack.nextOffset, ack.receivedBitmap);
    }
    CHECK(assembly.assembled());
    CHECK(sha256::hash(assembly.bytes().data(), assembly.bytes().size()) == hash);

    // Client cache publishes the verified bytes under the server/world scope.
    ClientCache cache(root / "client" / "schematics");
    Uuid const serverId = Uuid::random();
    Uuid const worldId = Uuid::random();
    std::string const endpoint = ClientCache::endpointKey("127.0.0.1", 19142);
    auto const part = cache.partPath(endpoint, serverId, worldId, hash, SchematicFormat::Mcstructure);
    writeFile(part, assembly.bytes());
    auto const published = cache.publishPart(
        endpoint, serverId, worldId, hash, SchematicFormat::Mcstructure, size, error
    );
    CHECK(published.has_value());
    CHECK(readFile(*published) == content);
    CHECK(cache.findVerified(endpoint, serverId, worldId, hash, SchematicFormat::Mcstructure, size).has_value());

    // --- GC safety: referenced object is never a candidate -----------------
    auto const candidates = repo.gcCandidates(9999999999999ll);
    CHECK(candidates.empty());
    CHECK(repo.fileCount() == 1);
    CHECK(repo.totalPlacementCount() == 1);

    // --- tombstone delete advances revision and leaves the object ----------
    SharedPlacementDto deleted;
    CHECK(repo.deletePlacement(created->placementId, 2, "owner-a", false, 4000, deleted, mutation, error));
    CHECK(mutation == PlacementRepository::Mutation::Ok);
    CHECK(deleted.deleted);
    CHECK(repo.totalPlacementCount() == 0);
    CHECK(store.hasObject(hash, SchematicFormat::Mcstructure, size)); // GC owns removal

    repo.close();
    std::error_code code;
    std::filesystem::remove_all(root, code);

    (void)failuresBefore;
    std::printf("LHoloSyncIntegrationTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
