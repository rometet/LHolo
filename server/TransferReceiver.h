#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"

#include "FileStore.h"

// Server-side (or client-side) receive half of a file transfer (§4.7). Owns the
// staging buffer for one transfer, validates chunk offsets and alignment,
// ignores identical retransmissions, rejects conflicting/overlapping data, and
// only publishes the object after the full length and SHA-256 are verified.

namespace lholo::sync {

struct TransferLimits {
    std::uint32_t chunkBytes{kDefaultChunkBytes};
    std::uint32_t window{kDefaultWindow};
    std::uint64_t maxFileBytes{kDefaultMaxFileBytes};
};

class TransferReceiver {
public:
    TransferReceiver(FileStore& store, TransferLimits limits);

    struct Offer {
        bool needBytes{true};
        std::uint64_t resumeOffset{0};
        std::uint64_t sizeBytes{0};
        std::uint32_t chunkBytes{kDefaultChunkBytes};
        std::uint32_t window{kDefaultWindow};
    };

    // Prepares a transfer. If the store already holds the verified object the
    // caller may skip the bytes (needBytes=false). Returns false on invalid
    // parameters.
    [[nodiscard]] bool prepare(
        Uuid const& transferId,
        Sha256Hash const& hash,
        SchematicFormat format,
        std::uint64_t sizeBytes,
        std::uint32_t chunkBytes,
        Offer& out,
        std::string& error
    );

    enum class Chunk { Accepted, Duplicate, Conflict, OutOfRange, NotPrepared };

    [[nodiscard]] Chunk onChunk(std::uint64_t offset, std::span<std::uint8_t const> data, std::string& error);

    struct Ack {
        std::uint64_t nextOffset{0};
        std::uint32_t receivedBitmap{0};
        bool complete{false};
    };

    [[nodiscard]] Ack acknowledgement() const;

    // Verifies total length + SHA-256 and publishes the object atomically.
    [[nodiscard]] bool complete(std::string& error);

    void cancel();
    [[nodiscard]] bool active() const { return mActive; }
    [[nodiscard]] Uuid transferId() const { return mTransferId; }
    [[nodiscard]] Sha256Hash hash() const { return mHash; }
    [[nodiscard]] SchematicFormat format() const { return mFormat; }
    [[nodiscard]] std::uint64_t sizeBytes() const { return mSizeBytes; }

private:
    [[nodiscard]] std::size_t chunkIndex(std::uint64_t offset) const;

    FileStore& mStore;
    TransferLimits mLimits;

    bool mActive{false};
    Uuid mTransferId{};
    Sha256Hash mHash{};
    SchematicFormat mFormat{SchematicFormat::Unknown};
    std::uint64_t mSizeBytes{0};
    std::uint32_t mChunkBytes{kDefaultChunkBytes};
    std::uint32_t mWindow{kDefaultWindow};
    std::vector<std::uint8_t> mBuffer;
    std::vector<bool> mHave;
    std::size_t mChunkCount{0};
};

} // namespace lholo::sync
