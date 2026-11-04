#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "sync/common/Protocol.h"

// Sending half of a file transfer (§4.7), storage-agnostic. Streams a stored
// object in fixed chunks honouring a window of unacknowledged bytes. On an ACK
// that falls behind the sent offset the stream resends from the acknowledged
// boundary (reliable-ordered transport makes loss rare; this is a safety net).

namespace lholo::sync {

class TransferSender {
public:
    struct Chunk {
        std::uint64_t offset{0};
        std::vector<std::uint8_t> data;
    };

    [[nodiscard]] bool open(
        std::filesystem::path const& path,
        std::uint64_t sizeBytes,
        std::uint32_t chunkBytes,
        std::uint32_t window,
        std::string& error
    );

    // Returns up to the unacknowledged window of chunks, capped by `maxBytes`.
    [[nodiscard]] std::vector<Chunk> poll(std::size_t maxBytes);

    void onAck(std::uint64_t nextOffset, std::uint32_t receivedBitmap);

    // Starts streaming at a resume boundary (both sent and acked move there).
    void setStartOffset(std::uint64_t offset);

    [[nodiscard]] bool allSent() const { return !mOpened || mNextOffset >= mSizeBytes; }
    [[nodiscard]] bool allAcked() const { return !mOpened || mAckedOffset >= mSizeBytes; }
    [[nodiscard]] std::uint64_t sizeBytes() const { return mSizeBytes; }
    [[nodiscard]] bool isOpen() const { return mOpened; }
    [[nodiscard]] std::uint32_t chunkBytes() const { return mChunkBytes; }
    void close();

private:
    std::ifstream mFile;
    bool mOpened{false};
    std::uint64_t mSizeBytes{0};
    std::uint32_t mChunkBytes{kDefaultChunkBytes};
    std::uint32_t mWindow{kDefaultWindow};
    std::uint64_t mNextOffset{0};
    std::uint64_t mAckedOffset{0};
};

} // namespace lholo::sync
