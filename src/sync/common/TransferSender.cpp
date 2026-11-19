#include "sync/common/TransferSender.h"

#include <algorithm>

#include "sync/common/PathSupport.h"

namespace lholo::sync {

bool TransferSender::open(
    std::filesystem::path const& path,
    std::uint64_t sizeBytes,
    std::uint32_t chunkBytes,
    std::uint32_t window,
    std::string& error
) {
    close();
    if (sizeBytes == 0) {
        error = "zero-length object";
        return false;
    }
    if (chunkBytes == 0 || chunkBytes > kMaxFileChunkBytes) {
        chunkBytes = kDefaultChunkBytes;
    }
    if (window == 0 || window > 64) {
        window = kDefaultWindow;
    }
    mFile.open(paths::ioPath(path), std::ios::binary);
    if (!mFile) {
        error = "cannot open object for streaming: " + path.string();
        return false;
    }
    mOpened = true;
    mSizeBytes = sizeBytes;
    mChunkBytes = chunkBytes;
    mWindow = window;
    mNextOffset = 0;
    mAckedOffset = 0;
    error.clear();
    return true;
}

std::vector<TransferSender::Chunk> TransferSender::poll(std::size_t maxBytes) {
    std::vector<Chunk> chunks;
    if (!mOpened) {
        return chunks;
    }
    std::uint64_t const windowBytes = static_cast<std::uint64_t>(mChunkBytes) * mWindow;
    std::size_t used = 0;
    while (mNextOffset < mSizeBytes && (mNextOffset - mAckedOffset) < windowBytes) {
        std::size_t const remaining = static_cast<std::size_t>(mSizeBytes - mNextOffset);
        std::size_t const length = std::min<std::size_t>(mChunkBytes, remaining);
        if (!chunks.empty() && used + length > maxBytes) {
            break;
        }
        Chunk chunk;
        chunk.offset = mNextOffset;
        chunk.data.resize(length);
        mFile.clear();
        mFile.seekg(static_cast<std::streamoff>(mNextOffset), std::ios::beg);
        mFile.read(reinterpret_cast<char*>(chunk.data.data()), static_cast<std::streamsize>(length));
        if (static_cast<std::size_t>(mFile.gcount()) != length) {
            // Treat a short read as a fatal stream error: stop advertising data.
            mOpened = false;
            chunks.clear();
            return chunks;
        }
        used += length;
        mNextOffset += length;
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

void TransferSender::onAck(std::uint64_t nextOffset, std::uint32_t /*receivedBitmap*/) {
    if (!mOpened) {
        return;
    }
    std::uint64_t const acked = std::min(nextOffset, mSizeBytes);
    if (acked > mAckedOffset) {
        mAckedOffset = acked;
    }
    // If the peer reports it is behind what we have sent (a loss), rewind so the
    // missing bytes are retransmitted within the window.
    if (acked < mNextOffset) {
        mNextOffset = acked;
    }
}

void TransferSender::setStartOffset(std::uint64_t offset) {
    if (!mOpened) {
        return;
    }
    std::uint64_t const start = std::min(offset, mSizeBytes);
    mNextOffset = start;
    mAckedOffset = start;
}

void TransferSender::close() {
    if (mFile.is_open()) {
        mFile.close();
    }
    mOpened = false;
    mSizeBytes = 0;
    mNextOffset = 0;
    mAckedOffset = 0;
}

} // namespace lholo::sync
