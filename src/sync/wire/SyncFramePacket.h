#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "ll/api/network/packet/Packet.h"
#include "mc/deps/core/utility/BinaryStream.h"
#include "mc/deps/core/utility/ReadOnlyBinaryStream.h"

#include "sync/common/Protocol.h"

// The single low-level transport packet registered by both the LHolo client and
// the LHoloSyncServer plugin (§4.2). It carries exactly one opaque LHS1 frame.
//
// This header is shared between the client (LHolo) and server (LHoloSyncServer)
// targets so the LL runtime id derived from the C++ type name is identical on
// both sides. It includes only shared engine headers (ll/api/network/packet and
// mc/*BinaryStream), never client-only or server-only headers.
//
// On LL 26.51.x there is no Optional packet negotiation (that API arrived in
// 26.60); custom packets ride on MinecraftPacketIds::LeviLaminaRuntimePacket
// (201), inside the vanilla "title-specific" id range, so peers lacking the
// handler ignore them. Discovery is done at the application layer by the LHS1
// HELLO / HELLO_ACK handshake.

namespace lholo::sync {

class SyncFramePacket : public ll::network::PacketBase<SyncFramePacket> {
public:
    std::vector<std::uint8_t> frame;

    SyncFramePacket() = default;
    explicit SyncFramePacket(std::vector<std::uint8_t> payload) : frame(std::move(payload)) {}

    void write(::BinaryStream& stream) const override {
        stream.writeUnsignedVarInt(static_cast<uint>(frame.size()), nullptr, nullptr);
        if (!frame.empty()) {
            stream.write(reinterpret_cast<char const*>(frame.data()), static_cast<uint64>(frame.size()));
        }
    }

    [[nodiscard]] ::Bedrock::Result<void> read(::ReadOnlyBinaryStream& stream) override {
        auto lengthResult = stream.getUnsignedVarInt();
        if (!lengthResult) {
            return nonstd::make_unexpected(lengthResult.error());
        }
        uint const length = *lengthResult;
        // Strict bound before any allocation: a frame is never larger than the
        // protocol ceiling (§3.2, §4.3).
        if (length == 0 || length > kMaxFrameBytes) {
            return nonstd::make_unexpected(
                Bedrock::ErrorInfo<std::error_code>{std::make_error_code(std::errc::value_too_large)}
            );
        }
        frame.assign(length, 0);
        auto readResult = stream.read(frame.data(), static_cast<uint64>(length));
        if (!readResult) {
            return readResult;
        }
        return {};
    }
};

} // namespace lholo::sync
