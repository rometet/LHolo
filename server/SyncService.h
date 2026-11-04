#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "mc/network/NetworkIdentifier.h"

#include "sync/common/ControlMessages.h"
#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"
#include "sync/common/TransferSender.h"

#include "FileStore.h"
#include "PlacementRepository.h"
#include "ServerConfig.h"
#include "TransferReceiver.h"

// Server-side sync service: owns identity, the authoritative repository, the
// object store, edit leases and transfer receivers. The receive handler only
// enqueues; tick() drains on the game thread (§3.2). Handlers build responses
// from the tested repository/store/codec layers.

namespace lholo::sync {

struct ServerSessionInfo {
    Uuid sessionId{};
    std::string actorKey;         // server-derived, opaque (never client-supplied)
    Role role{Role::Viewer};
    bool ready{false};
    bool authenticated{false};
    bool isOperator{false};
};

class SyncService {
public:
    static SyncService& instance();

    bool start(
        std::string const& serverVersion,
        ServerIdentity const& identity,
        ServerConfig const& config,
        std::filesystem::path const& dataDir
    );
    void stop() noexcept;

    // Receive-thread entries: validate + copy + enqueue only.
    void enqueueInbound(NetworkIdentifier const& source, std::vector<std::uint8_t> frame);
    void enqueueDisconnect(NetworkIdentifier const& source);

    // Game-thread drain.
    void tick();

    [[nodiscard]] Uuid serverId() const;
    [[nodiscard]] Uuid worldId() const;
    [[nodiscard]] std::size_t sessionCount() const;

    // Administrator operations backing /lholosync (server thread).
    [[nodiscard]] std::string adminStatusText();
    bool adminDelete(Uuid const& placementId, std::string& message);
    bool adminUnlock(Uuid const& placementId, std::string& message);
    bool adminSetRole(std::string const& playerId, std::string const& roleText, std::string& message);
    bool adminGc(std::string const& mode, std::string& message);
    void applyConfig(ServerConfig const& config);

private:
    SyncService() = default;

    struct InboundFrame {
        NetworkIdentifier source;
        std::vector<std::uint8_t> frame;
    };

    // In-memory edit lease (§5.3: never persisted). The holder is identified by
    // the same server-derived actor key used for ownership.
    struct Lease {
        std::string holderKey;
        Uuid lockId{};
        std::uint64_t baseRevision{0};
        std::int64_t expiresAtMs{0};
    };

    // Server -> client download stream (§4.7): the server is the sender.
    // NetworkIdentifier is not default-constructible inside a mod, so the target
    // is held as an optional and copy-assigned when the session starts.
    struct DownloadSession {
        std::optional<NetworkIdentifier> target;
        std::string actorKey;
        Uuid transferId{};
        Sha256Hash hash{};
        TransferSender sender;
        std::int64_t lastActivityMs{0};
        bool completeRequested{false};
    };

    void handleFrame(NetworkIdentifier const& source, std::vector<std::uint8_t> const& frame);
    void handleHello(NetworkIdentifier const& source, std::span<std::uint8_t const> payload);
    void handleListRequest(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleFileOffer(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleFileGet(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleFileChunk(NetworkIdentifier const& source, std::span<std::uint8_t const> payload);
    void handleFileAck(NetworkIdentifier const& source, std::span<std::uint8_t const> payload);
    void handleFileComplete(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handlePlacementCreate(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handlePlacementUpdate(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handlePlacementDelete(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleEditBegin(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleEditRenew(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleEditRelease(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload);

    void sendHelloAck(NetworkIdentifier const& source, ServerSessionInfo& session);
    void sendResult(NetworkIdentifier const& source, std::uint64_t requestId, control::Result const& result);
    void sendError(NetworkIdentifier const& source, std::uint64_t requestId, ErrorCode code, std::string const& message, bool retryable = false);
    void sendFrame(NetworkIdentifier const& source, Opcode opcode, std::uint64_t requestId, std::vector<std::uint8_t> payload);
    void broadcastDelta(std::vector<std::uint8_t> payload);
    void broadcastCatalogEvent(std::string const& action, SharedPlacementDto const& placement);

    [[nodiscard]] Role roleFor(ServerSessionInfo const& session);
    [[nodiscard]] static bool isWriterOpcode(Opcode opcode);
    [[nodiscard]] ServerSessionInfo* sessionFor(NetworkIdentifier const& source);
    [[nodiscard]] std::uint32_t activeTransfersFor(std::string const& actorKey) const;
    [[nodiscard]] bool mayWrite(Role role) const { return role == Role::Member || role == Role::Admin; }
    void releaseLeasesOf(std::string const& actorKey);
    void expireLeases();
    void pumpDownloads();
    void releaseDownloadsOf(NetworkIdentifier const& source);
    void releaseUploadsOf(NetworkIdentifier const& source);

    mutable std::mutex mMutex;
    std::deque<InboundFrame> mInbound;
    std::deque<NetworkIdentifier> mDisconnects;
    std::unordered_map<NetworkIdentifier, ServerSessionInfo> mSessions;

    std::unique_ptr<FileStore> mFileStore;
    std::unique_ptr<PlacementRepository> mRepository;
    std::unordered_map<std::string, TransferReceiver> mReceivers; // keyed by transfer uuid string
    std::unordered_map<std::string, NetworkIdentifier> mReceiverOwners; // upload owner connection
    std::unordered_map<std::string, std::string> mReceiverActors; // upload actor key
    std::unordered_map<std::string, DownloadSession> mDownloads;  // keyed by transfer uuid string

    std::unordered_map<std::string, Lease> mLeases;
    ServerConfig mConfig{};
    Uuid mServerId{};
    Uuid mWorldId{};
    std::string mServerVersion;
    std::uint64_t mMaxFileBytes{kDefaultMaxFileBytes};
    bool mRunning{false};
};

} // namespace lholo::sync
