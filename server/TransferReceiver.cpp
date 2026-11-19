#include "TransferReceiver.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "sync/common/PathSupport.h"
#include "sync/common/Sha256.h"

namespace lholo::sync {

TransferReceiver::TransferReceiver(FileStore& store, TransferLimits limits)
: mStore(store),
  mLimits(limits) {}

std::size_t TransferReceiver::chunkIndex(std::uint64_t offset) const {
    return static_cast<std::size_t>(offset / mChunkBytes);
}

bool TransferReceiver::prepare(
    Uuid const& transferId,
    Sha256Hash const& hash,
    SchematicFormat format,
    std::uint64_t sizeBytes,
    std::uint32_t chunkBytes,
    Offer& out,
    std::string& error
) {
    if (sizeBytes == 0) {
        error = "zero-length transfer rejected";
        return false;
    }
    if (sizeBytes > mLimits.maxFileBytes) {
        error = "transfer exceeds file limit";
        return false;
    }
    if (chunkBytes == 0 || chunkBytes > kMaxFileChunkBytes) {
        chunkBytes = mLimits.chunkBytes;
    }
    if (chunkBytes == 0) {
        chunkBytes = kDefaultChunkBytes;
    }

    out.chunkBytes = chunkBytes;
    out.window = mLimits.window;
    out.sizeBytes = sizeBytes;

    // Already verified and stored: the peer can skip the bytes entirely.
    if (mStore.hasObject(hash, format, sizeBytes)) {
        out.needBytes = false;
        out.resumeOffset = sizeBytes;
        return true;
    }

    mTransferId = transferId;
    mHash = hash;
    mFormat = format;
    mSizeBytes = sizeBytes;
    mChunkBytes = chunkBytes;
    mWindow = mLimits.window;
    mChunkCount = static_cast<std::size_t>((sizeBytes + chunkBytes - 1) / chunkBytes);
    mBuffer.assign(static_cast<std::size_t>(sizeBytes), 0);
    mHave.assign(mChunkCount, false);
    mActive = true;

    out.needBytes = true;
    out.resumeOffset = 0;
    error.clear();
    return true;
}

TransferReceiver::Chunk TransferReceiver::onChunk(
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
    // Offsets must be chunk-aligned; only the final chunk may be short.
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
    // Non-final chunks must be exactly chunkBytes long.
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

TransferReceiver::Ack TransferReceiver::acknowledgement() const {
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

bool TransferReceiver::complete(std::string& error) {
    if (!mActive) {
        error = "no active transfer";
        return false;
    }
    for (std::size_t i = 0; i < mChunkCount; ++i) {
        if (!mHave[i]) {
            error = "transfer incomplete";
            return false;
        }
    }
    if (mBuffer.size() != mSizeBytes) {
        error = "assembled length mismatch";
        return false;
    }
    auto const actual = sha256::hash(mBuffer.data(), mBuffer.size());
    if (actual != mHash) {
        error = "sha256 mismatch";
        // Fail closed: never publish, and drop the poisoned buffer.
        cancel();
        return false;
    }

    // Stage the verified bytes and publish atomically through the store.
    auto staging = mStore.createStagingFile(error);
    if (!staging) {
        return false;
    }
    {
        std::ofstream file(paths::ioPath(*staging), std::ios::binary | std::ios::trunc);
        if (!file) {
            error = "cannot open staging file for writing";
            return false;
        }
        file.write(
            reinterpret_cast<char const*>(mBuffer.data()), static_cast<std::streamsize>(mBuffer.size())
        );
        if (!file) {
            error = "short write to staging file";
            return false;
        }
    }

    auto published = mStore.publish(*staging, mHash, mFormat, mSizeBytes, error);
    std::error_code code;
    std::filesystem::remove(paths::ioPath(*staging), code);
    if (!published) {
        return false;
    }
    cancel();
    return true;
}

void TransferReceiver::cancel() {
    mActive = false;
    mBuffer.clear();
    mBuffer.shrink_to_fit();
    mHave.clear();
    mChunkCount = 0;
}

} // namespace lholo::sync
