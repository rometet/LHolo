// LHolo Integrated Sync — server storage tests (spec §12.1).
//
// T03 file deduplication, T04 partial/mismatched data never published, T05
// two placements sharing one object, T06 revision conflict, T07 owner/admin
// authority, tombstone delete, SQLite restart persistence and client-operation
// idempotency. Uses a temporary directory; no Minecraft runtime involved.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "sync/common/SharedTypes.h"
#include "sync/common/Sha256.h"

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
        / ("lholo-sync-storetest-" + std::to_string(device()) + "-" + std::to_string(device()));
    std::error_code code;
    std::filesystem::remove_all(root, code);
    std::filesystem::create_directories(root, code);
    return root;
}

void writeFile(std::filesystem::path const& path, std::string const& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

Sha256Hash hashOf(std::string const& content) {
    return sha256::hash(content.data(), content.size());
}

PlacementRepository::PlacementInput makeInput(std::string const& name, Sha256Hash const& hash, std::int64_t x) {
    PlacementRepository::PlacementInput input;
    input.fileHash = hash;
    input.format = SchematicFormat::Mcstructure;
    input.displayName = name;
    input.dimension = 0;
    input.originX = x;
    input.originY = 64;
    input.originZ = 200;
    input.rotation = 0;
    input.mirror = 0;
    return input;
}

void testFileStoreDedupeAndRejection(std::filesystem::path const& root) {
    FileStore store(root / "objects", root / "staging", FileStoreLimits{});

    std::string const content = "fake-mcstructure-bytes";
    auto const hash = hashOf(content);
    auto const source = root / "source.mcstructure";
    writeFile(source, content);

    std::string error;
    auto published = store.publish(source, hash, SchematicFormat::Mcstructure, content.size(), error);
    CHECK(published.has_value());
    CHECK(store.hasObject(hash, SchematicFormat::Mcstructure, content.size()));
    CHECK(store.objectCount() == 1);

    // T03 — identical object is deduplicated, not rewritten.
    auto again = store.publish(source, hash, SchematicFormat::Mcstructure, content.size(), error);
    CHECK(again.has_value());
    CHECK(store.objectCount() == 1);

    // T04 — declared hash mismatch is rejected and publishes nothing new.
    auto wrongHash = hashOf("different");
    auto bad = store.publish(source, wrongHash, SchematicFormat::Mcstructure, content.size(), error);
    CHECK(!bad.has_value());
    CHECK(!store.hasObject(wrongHash, SchematicFormat::Mcstructure, content.size()));
    CHECK(store.objectCount() == 1);

    // Declared size mismatch is rejected.
    auto wrongSize = store.publish(source, hash, SchematicFormat::Mcstructure, content.size() + 1, error);
    CHECK(!wrongSize.has_value());

    // Empty object rejected.
    auto empty = store.publish(source, hash, SchematicFormat::Mcstructure, 0, error);
    CHECK(!empty.has_value());

    // hasObject requires the exact expected size.
    CHECK(!store.hasObject(hash, SchematicFormat::Mcstructure, content.size() + 5));

    // hashFile agrees with the in-memory hash.
    auto fileHash = FileStore::hashFile(source, error);
    CHECK(fileHash.has_value() && *fileHash == hash);
}

void testRepositoryLifecycle(std::filesystem::path const& root) {
    auto const dbPath = root / "data" / "sync.db";
    std::string const content = "structure-payload";
    auto const hash = hashOf(content);

    {
        PlacementRepository repo;
        std::string error;
        CHECK(repo.open(dbPath, error));
        CHECK(repo.isOpen());

        bool existed = false;
        FileRecord record;
        record.sha256 = hash;
        record.format = SchematicFormat::Mcstructure;
        record.sizeBytes = content.size();
        record.createdAtMs = 1000;
        CHECK(repo.insertFile(record, existed, error));
        CHECK(!existed);
        // T03 — inserting the same file hash reports it already existed.
        CHECK(repo.insertFile(record, existed, error));
        CHECK(existed);

        // Create two placements sharing the same object (T05/T12).
        auto first = repo.createPlacement(makeInput("A", hash, 100), "owner-a", 2000, error);
        CHECK(first.has_value());
        CHECK(first->revision == 1);
        CHECK(repo.currentSequence() == 1);

        auto second = repo.createPlacement(makeInput("B", hash, -50), "owner-b", 2001, error);
        CHECK(second.has_value());
        CHECK(second->placementId != first->placementId);
        CHECK(repo.currentSequence() == 2);

        // T06 — stale base revision is a conflict and leaves the row unchanged.
        auto base = makeInput("A2", hash, 110);
        SharedPlacementDto out;
        PlacementRepository::Mutation mutation{};
        CHECK(repo.updatePlacement(first->placementId, 99, base, "owner-a", false, 3000, out, mutation, error));
        CHECK(mutation == PlacementRepository::Mutation::Conflict);
        auto unchanged = repo.getPlacement(first->placementId);
        CHECK(unchanged.has_value() && unchanged->revision == 1 && unchanged->displayName == "A");

        // T07 — non-owner without admin is forbidden.
        CHECK(repo.updatePlacement(first->placementId, 1, base, "intruder", false, 3000, out, mutation, error));
        CHECK(mutation == PlacementRepository::Mutation::Forbidden);

        // Owner edit succeeds and bumps the revision/sequence.
        CHECK(repo.updatePlacement(first->placementId, 1, base, "owner-a", false, 3001, out, mutation, error));
        CHECK(mutation == PlacementRepository::Mutation::Ok);
        CHECK(out.revision == 2);
        CHECK(out.originX == 110);
        CHECK(repo.currentSequence() == 3);

        // Admin may edit another owner's placement.
        CHECK(repo.updatePlacement(second->placementId, 1, base, "admin", true, 3002, out, mutation, error));
        CHECK(mutation == PlacementRepository::Mutation::Ok);

        // A third placement for paging (distinct updated_at ordering).
        auto third = repo.createPlacement(makeInput("C", hash, 0), "owner-a", 3003, error);
        CHECK(third.has_value());

        // Tombstone delete by owner.
        CHECK(repo.deletePlacement(third->placementId, 1, "owner-a", false, 3004, out, mutation, error));
        CHECK(mutation == PlacementRepository::Mutation::Ok);
        CHECK(out.deleted);
        auto deleted = repo.getPlacement(third->placementId);
        CHECK(deleted.has_value() && deleted->deleted);

        // Paging excludes tombstones and honours the page size.
        auto page1 = repo.listPage(std::nullopt, 1);
        CHECK(page1.items.size() == 1);
        CHECK(page1.hasMore);
        CHECK(page1.nextCursor.has_value());
        auto page2 = repo.listPage(page1.nextCursor, 10);
        CHECK(page2.items.size() == 1);
        CHECK(!page2.hasMore);
        CHECK(page1.items[0].placementId != page2.items[0].placementId);

        // Permissions round trip.
        CHECK(repo.setRole("owner-a", Role::Member, 4000, error));
        auto role = repo.getRole("owner-a");
        CHECK(role.has_value() && *role == Role::Member);
        CHECK(!repo.getRole("unknown").has_value());

        // Client-operation idempotency.
        CHECK(!repo.lookupOperation("owner-a", "op-1").has_value());
        CHECK(repo.recordOperation("owner-a", "op-1", "create", "{\"status\":\"ok\"}", 5000, error));
        auto stored = repo.lookupOperation("owner-a", "op-1");
        CHECK(stored.has_value() && *stored == "{\"status\":\"ok\"}");
        // Recording the same operation again is a no-op.
        CHECK(repo.recordOperation("owner-a", "op-1", "create", "{\"status\":\"changed\"}", 5001, error));
        auto storedAgain = repo.lookupOperation("owner-a", "op-1");
        CHECK(storedAgain.has_value() && *storedAgain == "{\"status\":\"ok\"}");

        repo.close();
    }

    // Restart persistence: reopening the database preserves placements, files,
    // permissions and the catalog sequence.
    {
        PlacementRepository repo;
        std::string error;
        CHECK(repo.open(dbPath, error));
        auto file = repo.getFile(hash);
        CHECK(file.has_value() && file->sizeBytes == content.size());
        CHECK(repo.currentSequence() == 6);
        auto role = repo.getRole("owner-a");
        CHECK(role.has_value() && *role == Role::Member);
        auto page = repo.listPage(std::nullopt, 10);
        CHECK(page.items.size() == 2);
        auto op = repo.lookupOperation("owner-a", "op-1");
        CHECK(op.has_value());
        repo.close();
    }
}

void testOwnershipGuardOnCreate(std::filesystem::path const& root) {
    // A placement cannot be created for a file hash with no files row
    // (foreign key), which prevents referencing an unwritten/unowned object.
    PlacementRepository repo;
    std::string error;
    CHECK(repo.open(root / "fk" / "sync.db", error));
    auto phantom = hashOf("never-stored");
    auto created = repo.createPlacement(makeInput("X", phantom, 0), "owner", 100, error);
    CHECK(!created.has_value());
    repo.close();
}

void testTransferReceiver(std::filesystem::path const& root) {
    FileStore store(root / "tobj", root / "tstage", FileStoreLimits{});
    TransferLimits limits;
    limits.chunkBytes = 1024;
    limits.window = 4;
    TransferReceiver receiver(store, limits);

    std::string content(2500, 'q');
    for (std::size_t i = 0; i < content.size(); ++i) {
        content[i] = static_cast<char>('a' + (i % 26));
    }
    auto const hash = hashOf(content);
    auto const id = Uuid::random();

    TransferReceiver::Offer offer;
    std::string error;
    CHECK(receiver.prepare(id, hash, SchematicFormat::Mcstructure, content.size(), 1024, offer, error));
    CHECK(offer.needBytes);
    CHECK(offer.resumeOffset == 0);
    CHECK(offer.chunkBytes == 1024);

    auto const chunk = [&](std::size_t offset, std::size_t length) {
        auto const* start = reinterpret_cast<std::uint8_t const*>(content.data()) + offset;
        return receiver.onChunk(offset, std::span<std::uint8_t const>(start, length), error);
    };

    // Out-of-order: receive chunk 1 and 2 first.
    CHECK(chunk(1024, 1024) == TransferReceiver::Chunk::Accepted);
    CHECK(chunk(2048, 452) == TransferReceiver::Chunk::Accepted);
    auto partial = receiver.acknowledgement();
    CHECK(partial.nextOffset == 0);       // chunk 0 still missing
    CHECK(partial.receivedBitmap == 0b110u); // bits 1 and 2 present
    CHECK(!partial.complete);

    // Duplicate identical retransmission is ignored.
    CHECK(chunk(1024, 1024) == TransferReceiver::Chunk::Duplicate);

    // Misaligned offset for a non-final chunk is rejected.
    {
        auto const* start = reinterpret_cast<std::uint8_t const*>(content.data());
        CHECK(receiver.onChunk(100, std::span<std::uint8_t const>(start, 200), error)
              == TransferReceiver::Chunk::OutOfRange);
    }
    // Offset beyond the object is rejected.
    CHECK(chunk(2048, 452) == TransferReceiver::Chunk::Duplicate); // final chunk duplicate is fine
    CHECK(receiver.onChunk(2500, std::span<std::uint8_t const>(
              reinterpret_cast<std::uint8_t const*>(content.data()), 1), error)
          == TransferReceiver::Chunk::OutOfRange);

    // Receive chunk 0, then a conflicting retransmission of it.
    CHECK(chunk(0, 1024) == TransferReceiver::Chunk::Accepted);
    std::string conflicting = content;
    conflicting[0] = 'Z';
    CHECK(receiver.onChunk(0, std::span<std::uint8_t const>(
              reinterpret_cast<std::uint8_t const*>(conflicting.data()), 1024), error)
          == TransferReceiver::Chunk::Conflict);

    auto full = receiver.acknowledgement();
    CHECK(full.complete);
    CHECK(full.nextOffset == content.size());

    CHECK(receiver.complete(error));
    CHECK(store.hasObject(hash, SchematicFormat::Mcstructure, content.size()));

    // Preparing again now that the object exists reports no bytes needed.
    TransferReceiver::Offer again;
    CHECK(receiver.prepare(Uuid::random(), hash, SchematicFormat::Mcstructure, content.size(), 1024, again, error));
    CHECK(!again.needBytes);

    // Hash mismatch fails closed and publishes nothing.
    std::string const tampered = content.substr(0, content.size() - 1) + "X";
    auto const tamperedHash = hashOf(tampered);
    CHECK(receiver.prepare(Uuid::random(), tamperedHash, SchematicFormat::Mcstructure, content.size(), 1024, again, error));
    CHECK(chunk(0, 1024) == TransferReceiver::Chunk::Accepted);
    CHECK(chunk(1024, 1024) == TransferReceiver::Chunk::Accepted);
    CHECK(chunk(2048, 452) == TransferReceiver::Chunk::Accepted);
    CHECK(!receiver.complete(error));
    CHECK(!store.hasObject(tamperedHash, SchematicFormat::Mcstructure, content.size()));
}

} // namespace

int main() {
    auto const root = makeTempRoot();
    testFileStoreDedupeAndRejection(root);
    testRepositoryLifecycle(root);
    testOwnershipGuardOnCreate(root);
    testTransferReceiver(root);
    std::error_code code;
    std::filesystem::remove_all(root, code);

    std::printf("LHoloSyncStoreTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
