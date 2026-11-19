#include "sync/common/ChunkAssembly.h"

#include <cstring>

namespace lholo::sync {

std::size_t ChunkAssembly::chunkIndex(std::uint64_t offset) const {
    return static_cast<std::size_t>(offset / mChunkBytes);
}

bool ChunkAssembly::prepare(
    Uuid const& transferId,
    Sha256Hash const& hash,
    std::uint64_t sizeBytes,
    std::uint32_t chunkBytes,
    std::uint32_t window,
    ChunkAssemblyLimits const& limits,
    std::string& error
) {
    if (sizeBytes == 0) {
        error = "zero-length transfer rejected";
        return false;
    }
    if (sizeBytes > limits.maxFileBytes) {
        error = "transfer exceeds file limit";
        return false;
    }
    if (chunkBytes == 0 || chunkBytes > kMaxFileChunkBytes) {
        chunkBytes = limits.chunkBytes;
    }
    if (chunkBytes == 0) {
        chunkBytes = kDefaultChunkBytes;
    }
    if (window == 0 || window > 64) {
        window = limits.window == 0 ? kDefaultWindow : limits.window;
    }

    mTransferId = transferId;
    mHash = hash;
    mSizeBytes = sizeBytes;
    mChunkBytes = chunkBytes;
    mWindow = window;
    mChunkCount = static_cast<std::size_t>((sizeBytes + chunkBytes - 1) / chunkBytes);
    mBuffer.assign(static_cast<std::size_t>(sizeBytes), 0);
    mHave.assign(mChunkCount, false);
    mActive = true;
    error.clear();
    return true;
}

ChunkAssembly::Chunk ChunkAssembly::onChunk(
    std::uint64_t offset,
    std::span<std::uint8_t const> data,
    std::string& error
) {
    if (!mActive) {
        error = "no active transfer";
        return Chunk::NotPrepared;
    }
    if (data.empty()) {
        error = "empty chunk";
        return Chunk::OutOfRange;
    }
    if (data.size() > kMaxFileChunkBytes) {
        error = "chunk exceeds protocol maximum";
        return Chunk::OutOfRange;
    }
    if (offset % mChunkBytes != 0) {
        if (offset + data.size() != mSizeBytes) {
            error = "chunk offset is not aligned";
            return Chunk::OutOfRange;
        }
    }
    if (offset >= mSizeBytes || data.size() > mSizeBytes - offset) {
        error = "chunk extends beyond the object";
        return Chunk::OutOfRange;
    }
    std::size_t const index = chunkIndex(offset);
    if (index >= mChunkCount) {
        error = "chunk index out of range";
        return Chunk::OutOfRange;
    }
    bool const isFinal = (offset + data.size() == mSizeBytes);
    if (!isFinal && data.size() != mChunkBytes) {
        error = "non-final chunk has wrong length";
        return Chunk::OutOfRange;
    }

    auto* destination = mBuffer.data() + static_cast<std::size_t>(offset);
    if (mHave[index]) {
        if (std::memcmp(destination, data.data(), data.size()) == 0) {
            return Chunk::Duplicate;
        }
        error = "conflicting retransmission for an already-received chunk";
        return Chunk::Conflict;
    }
    std::memcpy(destination, data.data(), data.size());
    mHave[index] = true;
    error.clear();
    return Chunk::Accepted;
}

ChunkAssembly::Ack ChunkAssembly::acknowledgement() const {
    Ack ack;
    if (!mActive) {
        return ack;
    }
    std::size_t next = 0;
    while (next < mChunkCount && mHave[next]) {
        ++next;
    }
    ack.complete = next >= mChunkCount;
    ack.nextOffset = static_cast<std::uint64_t>(next) * mChunkBytes;
    if (ack.nextOffset > mSizeBytes) {
        ack.nextOffset = mSizeBytes;
    }
    for (std::uint32_t bit = 0; bit < mWindow && bit < 32; ++bit) {
        std::size_t const index = next + bit;
        if (index < mChunkCount && mHave[index]) {
            ack.receivedBitmap |= (1u << bit);
        }
    }
    return ack;
}

bool ChunkAssembly::assembled() const {
    if (!mActive) {
        return false;
    }
    for (std::size_t i = 0; i < mChunkCount; ++i) {
        if (!mHave[i]) {
            return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> ChunkAssembly::takeBytes() {
    auto result = std::move(mBuffer);
    mBuffer.clear();
    return result;
}

void ChunkAssembly::reset() {
    mActive = false;
    mBuffer.clear();
    mBuffer.shrink_to_fit();
    mHave.clear();
    mChunkCount = 0;
}

} // namespace lholo::sync
