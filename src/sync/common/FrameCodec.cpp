#include "FrameCodec.h"

#include <cstring>

namespace lholo::sync {

bool encodeFrame(Frame const& frame, std::vector<std::uint8_t>& out) {
    if (frame.payload.size() > kMaxPayloadBytes) {
        return false;
    }
    if (frame.payload.size() > kMaxFrameBytes - kFrameHeaderSize) {
        return false;
    }
    out.clear();
    out.reserve(kFrameHeaderSize + frame.payload.size());

    out.insert(out.end(), kFrameMagic, kFrameMagic + 4);
    le::writeU16(out, kProtocolMajor);
    le::writeU16(out, kProtocolMinor);
    le::writeU16(out, frame.opcode);
    le::writeU16(out, frame.flags);
    le::writeU64(out, frame.requestId);
    out.insert(out.end(), frame.sessionId.begin(), frame.sessionId.end());
    le::writeU32(out, static_cast<std::uint32_t>(frame.payload.size()));
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    return true;
}

DecodeResult decodeFrame(std::span<std::uint8_t const> bytes, Frame& out, std::string& error) {
    if (bytes.size() < kFrameHeaderSize) {
        error = "frame shorter than header";
        return DecodeResult::Invalid;
    }
    if (bytes.size() > kMaxFrameBytes) {
        error = "frame exceeds maximum size";
        return DecodeResult::Invalid;
    }
    if (std::memcmp(bytes.data(), kFrameMagic, 4) != 0) {
        error = "bad frame magic";
        return DecodeResult::Invalid;
    }

    std::uint8_t const* p = bytes.data();
    std::uint16_t const major = le::readU16(p + 4);
    std::uint16_t const minor = le::readU16(p + 6);
    if (major != kProtocolMajor) {
        error = "unsupported protocol major";
        return DecodeResult::IncompatibleVersion;
    }
    if (minor > kProtocolMinor) {
        error = "unsupported protocol minor";
        return DecodeResult::IncompatibleVersion;
    }

    std::uint16_t const opcode = le::readU16(p + 8);
    if (!isKnownOpcode(opcode)) {
        error = "unknown opcode";
        return DecodeResult::Invalid;
    }
    std::uint16_t const flags = le::readU16(p + 10);
    if (flags != 0) {
        error = "non-zero flags";
        return DecodeResult::Invalid;
    }

    std::uint64_t const requestId = le::readU64(p + 12);
    std::array<std::uint8_t, kSessionIdBytes> sessionId{};
    std::memcpy(sessionId.data(), p + 20, kSessionIdBytes);

    std::uint32_t const payloadLength = le::readU32(p + 36);
    if (payloadLength > kMaxPayloadBytes) {
        error = "payload length exceeds maximum";
        return DecodeResult::Invalid;
    }
    if (kFrameHeaderSize + static_cast<std::size_t>(payloadLength) != bytes.size()) {
        error = "payload length does not match frame size";
        return DecodeResult::Invalid;
    }

    out.opcode = opcode;
    out.flags = flags;
    out.requestId = requestId;
    out.sessionId = sessionId;
    out.payload.assign(bytes.begin() + kFrameHeaderSize, bytes.end());
    error.clear();
    return DecodeResult::Ok;
}

bool encodeFileChunk(
    std::array<std::uint8_t, kUuidBytes> const& transferId,
    std::uint64_t offset,
    std::span<std::uint8_t const> data,
    std::vector<std::uint8_t>& out
) {
    if (data.empty() || data.size() > kMaxFileChunkBytes) {
        return false;
    }
    out.clear();
    out.reserve(kFileChunkPrefixBytes + data.size());
    out.insert(out.end(), transferId.begin(), transferId.end());
    le::writeU64(out, offset);
    le::writeU32(out, static_cast<std::uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    return true;
}

bool decodeFileChunk(std::span<std::uint8_t const> bytes, FileChunkPayload& chunk, std::string& error) {
    if (bytes.size() < kFileChunkPrefixBytes) {
        error = "file chunk shorter than prefix";
        return false;
    }
    std::memcpy(chunk.transferId.data(), bytes.data(), kUuidBytes);
    chunk.offset = le::readU64(bytes.data() + 16);
    std::uint32_t const length = le::readU32(bytes.data() + 24);
    if (length == 0 || length > kMaxFileChunkBytes) {
        error = "file chunk length out of range";
        return false;
    }
    if (kFileChunkPrefixBytes + static_cast<std::size_t>(length) != bytes.size()) {
        error = "file chunk length does not match payload";
        return false;
    }
    chunk.length = length;
    chunk.data = bytes.subspan(kFileChunkPrefixBytes, length);
    error.clear();
    return true;
}

} // namespace lholo::sync
