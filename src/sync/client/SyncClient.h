#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "sync/client/ClientCache.h"
#include "sync/client/PlacementBridge.h"
#include "sync/client/SharedCatalog.h"
#include "sync/common/ChunkAssembly.h"
#include "sync/common/EditCompletion.h"
#include "sync/common/Protocol.h"
#include "sync/common/SharedTypes.h"
#include "sync/common/TransferSender.h"

// Client-side sync session owner. Lives inside LHolo.dll.
//
// Thread rules (§3.2):
//   * onFrame() runs on the LeviLamina receive handler thread: it only copies
//     already-validated bytes into a bounded queue. No I/O, no locks held long,
//     no engine/render access.
//   * tick()/onWorldJoin()/onWorldLeave() run on the client game thread and are
//     the only place that mutates session state or sends packets.

namespace lholo::sync {

enum class SessionState : std::uint8_t {
    Disabled = 0,
    AwaitCapability,   // no server response yet (5 s budget)
    HelloSent,
    Ready,
    Unavailable,       // server did not answer: behave like vanilla LHolo
    Closing,
};

[[nodiscard]] char const* sessionStateName(SessionState state) noexcept;

struct ClientSessionSnapshot {
    SessionState state{SessionState::Disabled};
    bool available{false};
    Uuid serverId{};
    Uuid worldId{};
    Uuid sessionId{};
    Role role{Role::Viewer};
    std::string serverVersion;
    std::vector<std::string> features;
    std::uint64_t catalogSequence{0};
    std::uint64_t maxFileBytes{kDefaultMaxFileBytes};
    std::uint32_t maxActiveTransfers{2};
    std::string statusText;             // English, log/UI-adjacent
    std::uint64_t epoch{0};

    // Shared-placement edit view (§4.5). The UI opens its editor only while the
    // phase is Pending/Granted/Committing/Failed and enables commit only while
    // the server lease is actually held (Granted).
    EditPhase editPhase{EditPhase::Idle};
    Uuid editPlacementId{};
    std::uint64_t editRevision{0};
    std::string editMessage;            // phase detail / failure reason (log-oriented)
};

class SyncClient {
public:
    static SyncClient& instance();

    // Wires the packet adapter and client event listeners. Idempotent.
    bool install();
    void uninstall() noexcept;

    // Game-thread lifecycle (driven by client events from the subsystem).
    void onWorldJoin();
    void onWorldLeave() noexcept;
    void tick();

    // Receive-handler entry point (any thread). Enqueue-only.
    void onFrame(std::vector<std::uint8_t> frame);

    [[nodiscard]] ClientSessionSnapshot snapshot() const;

    // User action (game thread): display the shared placement described by the
    // catalog entry, using the verified cache when available.
    [[nodiscard]] bool selectSharedPlacement(Uuid const& placementId, std::string& status);

    // Requests the file for a catalog placement (game thread).
    [[nodiscard]] bool requestDownload(Uuid const& placementId, std::string& status);

    // Shares an active LOCAL placement (§4.7/§4.6): FILE_OFFER -> stream ->
    // PLACEMENT_CREATE. Requires member/admin role.
    [[nodiscard]] bool requestShare(std::uint64_t localPlacementId, std::string& status);

    // Requests edit of a shared placement: EDIT_BEGIN and, on grant, exposes the
    // lock so the UI can commit with PLACEMENT_UPDATE.
    [[nodiscard]] bool requestEdit(Uuid const& placementId, std::uint64_t baseRevision, std::string& status);
    [[nodiscard]] bool commitEdit(
        Uuid const& placementId,
        SharedPlacementDto const& draft,
        std::string& status
    );
    void cancelEdit(Uuid const& placementId);
    [[nodiscard]] bool requestDelete(Uuid const& placementId, std::uint64_t baseRevision, std::string& status);

    // Read-only view of the current catalog for the shared-tab UI.
    [[nodiscard]] std::vector<SharedPlacementDto> catalogItems() const;

    // UI thread -> game thread handoff. The menu renders on the Present thread;
    // these actions run at the start of tick() on the game thread so the session
    // state and projection bridge are never mutated from two threads.
    void enqueueUi(std::function<void()> action);
    void enqueueRefresh();

    // Test/debug: current monotonic millisecond clock used for timeouts.
    [[nodiscard]] static std::uint64_t nowMillis();

private:
    SyncClient() = default;

    void beginSession(std::uint64_t nowMs);
    void endSession(std::string reason);
    void sendHello();
    void requestList();
    void sendListRequest(std::optional<std::string> const& cursor);
    void handleFrame(std::vector<std::uint8_t> const& frame);
    void handleHelloAck(std::span<std::uint8_t const> payload);
    void handleListPage(std::span<std::uint8_t const> payload);
    void handleCatalogDelta(std::span<std::uint8_t const> payload);
    void handleFileAccept(std::span<std::uint8_t const> payload);
    void handleFileChunk(std::span<std::uint8_t const> payload);
    void handleFileAck(std::span<std::uint8_t const> payload);
    void handleFileComplete(std::span<std::uint8_t const> payload);
    void handleResult(std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void handleEditGranted(std::uint64_t requestId, std::span<std::uint8_t const> payload);
    void publishEditState();                 // copies mEdit into the session view
    void applyEditStateLocked();             // caller holds mMutex
    void pollEdit(std::uint64_t nowMs);      // game thread: timeouts + lease renewal
    void completeEdit(std::string message);  // confirmed update clears the edit
    [[nodiscard]] EditInFlight editInFlight() const;       // correlation state of mEdit
    [[nodiscard]] std::uint64_t catalogRevisionFor(Uuid const& placementId);
    void releaseEditLock();                  // sends EDIT_RELEASE when a lock is held
    void failEdit(std::string message);      // reports a failure and drops the lease
    void pumpUpload();
    void sendPlacementCreate();
    [[nodiscard]] std::optional<std::filesystem::path> cachedPathFor(SharedPlacementDto const& placement);

    mutable std::mutex mMutex;
    std::deque<std::vector<std::uint8_t>> mInbound;
    ClientSessionSnapshot mSession;
    std::uint64_t mNextEditRequestId{1};   // nonzero, unique per edit-family request
    std::uint64_t mSessionStartMs{0};
    std::uint64_t mLastHelloMs{0};
    int mHelloAttempts{0};
    bool mInstalled{false};
    bool mInboundOverflowed{false};

    std::unique_ptr<ClientCache> mCache;
    SharedCatalog mCatalog;       // guarded by mMutex
    PlacementBridge mBridge;      // game thread only (called under no lock)
    std::string mEndpointKey;

    // Multi-page LIST accumulation (all guarded by mMutex). A catalog snapshot
    // is applied only once the final page arrives so stale entries never survive.
    std::vector<SharedPlacementDto> mListAccum;
    std::optional<std::string> mListCursor;
    std::optional<std::uint64_t> mListSequence;
    bool mListInProgress{false};
    bool mDeltaDuringList{false};

    struct DownloadState {
        ChunkAssembly assembly;
        Sha256Hash expectedHash{};
        SchematicFormat format{SchematicFormat::Unknown};
        Uuid placementId{};
        bool active{false};
    };
    struct UploadState {
        TransferSender sender;
        std::filesystem::path sourcePath;
        Uuid transferId{};
        Uuid clientOperationId{};
        Sha256Hash hash{};
        SchematicFormat format{SchematicFormat::Unknown};
        SharedPlacementDto placement;   // transform to register on completion
        bool acceptReceived{false};
        bool completeSent{false};
        bool active{false};
    };
    struct EditState {
        Uuid placementId{};
        Uuid lockId{};              // nil until EDIT_GRANTED arrives
        std::uint64_t baseRevision{0};
        std::uint64_t requestMs{0};     // lock request / commit start (timeout base)
        std::uint64_t lastRenewMs{0};   // last lease grant or renewal
        std::uint64_t grantRequestId{0};  // request id of the outstanding EDIT_BEGIN / EDIT_RENEW
        std::uint64_t commitRequestId{0}; // request id of the outstanding PLACEMENT_UPDATE
        EditPhase phase{EditPhase::Idle};
        std::string message;
        bool active{false};             // a request is outstanding or a lock is held
    };
    DownloadState mDownload;
    UploadState mUpload;
    EditState mEdit;

    std::mutex mUiMutex;
    std::deque<std::function<void()>> mUiActions;
};

} // namespace lholo::sync
