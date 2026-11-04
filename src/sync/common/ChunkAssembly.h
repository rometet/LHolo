#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"

// Receiving half of a file transfer (§4.7), storage-agnostic. Validates chunk
// alignment/bounds, ignores identical retransmissions, rejects conflicting or
// overlapping data, and tracks the sliding-window acknowledgement. The caller
// verifies and publishes the assembled bytes (server: FileStore, client:
// ClientCache). This type does no file or network I/O.

namespace lholo::sync {

struct ChunkAssemblyLimits {
    std::uint32_t chunkBytes{kDefaultChunkBytes};
    std::uint32_t window{kDefaultWindow};
    std::uint64_t maxFileBytes{kDefaultMaxFileBytes};
};

class ChunkAssembly {
public:
    struct Offer {
        bool needBytes{true};
        std::uint64_t resumeOffset{0};
        std::uint64_t sizeBytes{0};
        std::uint32_t chunkBytes{kDefaultChunkBytes};
        std::uint32_t window{kDefaultWindow};
    };

    enum class Chunk { Accepted, Duplicate, Conflict, OutOfRange, NotPrepared };

    [[nodiscard]] bool prepare(
        Uuid const& transferId,
        Sha256Hash const& hash,
        std::uint64_t sizeBytes,
        std::uint32_t chunkBytes,
        std::uint32_t window,
        ChunkAssemblyLimits const& limits,
        std::string& error
    );

    [[nodiscard]] Chunk onChunk(std::uint64_t offset, std::span<std::uint8_t const> data, std::string& error);

    struct Ack {
        std::uint64_t nextOffset{0};
        std::uint32_t receivedBitmap{0};
        bool complete{false};
    };
    [[nodiscard]] Ack acknowledgement() const;

    [[nodiscard]] bool active() const { return mActive; }
    [[nodiscard]] bool assembled() const;
    [[nodiscard]] std::uint64_t sizeBytes() const { return mSizeBytes; }
    [[nodiscard]] Uuid const& transferId() const { return mTransferId; }
    [[nodiscard]] Sha256Hash const& hash() const { return mHash; }
    [[nodiscard]] std::vector<std::uint8_t> const& bytes() const { return mBuffer; }
    [[nodiscard]] std::vector<std::uint8_t> takeBytes();

    void reset();

private:
    [[nodiscard]] std::size_t chunkIndex(std::uint64_t offset) const;

    bool mActive{false};
    Uuid mTransferId{};
    Sha256Hash mHash{};
    std::uint64_t mSizeBytes{0};
    std::uint32_t mChunkBytes{kDefaultChunkBytes};
    std::uint32_t mWindow{kDefaultWindow};
    std::vector<std::uint8_t> mBuffer;
    std::vector<bool> mHave;
    std::size_t mChunkCount{0};
};

} // namespace lholo::sync
