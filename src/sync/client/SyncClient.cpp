#include "sync/client/SyncClient.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>

#include "structure/PlacementSession.h"
#include "structure/SchematicRuntime.h"

#include "sync/client/ClientPacketAdapter.h"
#include "sync/common/ControlMessages.h"
#include "sync/common/FrameCodec.h"
#include "sync/common/PathSupport.h"
#include "sync/common/Sha256.h"

namespace lholo::sync {

namespace {

constexpr std::size_t kMaxInboundFrames = 64;

// Shared-placement edit budgets (§4.5). The server lease defaults to 120 s, so
// renewing on a 30 s cadence keeps a lock alive through a long edit session;
// the request/commit budgets bound how long the UI may show a spinner.
constexpr std::uint64_t kEditGrantTimeoutMs = 10000;
constexpr std::uint64_t kEditCommitTimeoutMs = 10000;
constexpr std::uint64_t kEditRenewIntervalMs = 30000;

std::vector<std::uint8_t> encodeFrameBytes(
    Opcode opcode,
    std::uint64_t requestId,
    std::array<std::uint8_t, kSessionIdBytes> const& sessionId,
    std::vector<std::uint8_t> payload
) {
    Frame frame;
    frame.opcode = static_cast<std::uint16_t>(opcode);
    frame.flags = 0;
    frame.requestId = requestId;
    frame.sessionId = sessionId;
    frame.payload = std::move(payload);
    std::vector<std::uint8_t> out;
    if (!encodeFrame(frame, out)) {
        out.clear();
    }
    return out;
}

SchematicFormat formatFromPath(std::filesystem::path const& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".litematic") {
        return SchematicFormat::Litematic;
    }
    if (ext == ".mcstructure") {
        return SchematicFormat::Mcstructure;
    }
    return SchematicFormat::Unknown;
}

std::optional<Sha256Hash> hashLocalFile(std::filesystem::path const& path) {
    std::ifstream in(paths::ioPath(path), std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    sha256::Hasher hasher;
    std::array<char, 1u << 16> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        std::streamsize const got = in.gcount();
        if (got > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(got));
        }
    }
    if (in.bad()) {
        return std::nullopt;
    }
    return hasher.finish();
}

} // namespace

char const* sessionStateName(SessionState state) noexcept {
    switch (state) {
        case SessionState::Disabled: return "disabled";
        case SessionState::AwaitCapability: return "await_capability";
        case SessionState::HelloSent: return "hello_sent";
        case SessionState::Ready: return "ready";
        case SessionState::Unavailable: return "unavailable";
        case SessionState::Closing: return "closing";
    }
    return "unknown";
}

SyncClient& SyncClient::instance() {
    static SyncClient client;
    return client;
}

std::uint64_t SyncClient::nowMillis() {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool SyncClient::install() {
    if (mInstalled) {
        return true;
    }
    if (!installClientPacketAdapter()) {
        return false;
    }
    mInstalled = true;
    return true;
}

void SyncClient::uninstall() noexcept {
    uninstallClientPacketAdapter();
    std::lock_guard lock{mMutex};
    mInbound.clear();
    mSession = ClientSessionSnapshot{};
    mSession.state = SessionState::Disabled;
    mInstalled = false;
}

void SyncClient::onWorldJoin() {
    std::uint64_t const now = nowMillis();
    std::lock_guard lock{mMutex};
    beginSession(now);
}

void SyncClient::onWorldLeave() noexcept {
    std::lock_guard lock{mMutex};
    endSession("world leave");
}

void SyncClient::beginSession(std::uint64_t nowMs) {
    ++mSession.epoch;
    mSession.state = SessionState::AwaitCapability;
    mSession.available = false;
    mSession.serverId = Uuid{};
    mSession.worldId = Uuid{};
    mSession.sessionId = Uuid{};
    mSession.role = Role::Viewer;
    mSession.serverVersion.clear();
    mSession.features.clear();
    mSession.catalogSequence = 0;
    mSession.statusText = "awaiting server capability";
    mSessionStartMs = nowMs;
    mLastHelloMs = 0;
    mHelloAttempts = 0;
    mInboundOverflowed = false;
    mInbound.clear();
    mCatalog.clear();
    mCache.reset();
    mEndpointKey.clear();
    mDownload = DownloadState{};
    mUpload = UploadState{};
    mEdit = EditState{};
    applyEditStateLocked();
}

void SyncClient::endSession(std::string reason) {
    if (mSession.state == SessionState::Disabled) {
        return;
    }
    mSession.state = SessionState::Closing;
    mSession.available = false;
    mSession.statusText = std::move(reason);
    mInbound.clear();
    mCatalog.clear();
    mCache.reset();
    mBridge.retireAll();
    mDownload = DownloadState{};
    mUpload = UploadState{};
    mEdit = EditState{};
    applyEditStateLocked();
    mSession.state = SessionState::Disabled;
}

void SyncClient::onFrame(std::vector<std::uint8_t> frame) {
    std::lock_guard lock{mMutex};
    if (mSession.state == SessionState::Disabled || mSession.state == SessionState::Closing) {
        return; // stale frames from a previous session are discarded
    }
    if (mInbound.size() >= kMaxInboundFrames) {
        mInbound.pop_front();
        mInboundOverflowed = true;
    }
    mInbound.push_back(std::move(frame));
}

void SyncClient::tick() {
    std::uint64_t const now = nowMillis();

    std::deque<std::function<void()>> uiActions;
    {
        std::lock_guard lock{mUiMutex};
        uiActions.swap(mUiActions);
    }
    for (auto& action : uiActions) {
        if (action) {
            action();
        }
    }

    std::deque<std::vector<std::uint8_t>> inbound;
    bool overflowed = false;
    {
        std::lock_guard lock{mMutex};
        inbound.swap(mInbound);
        overflowed = mInboundOverflowed;
        mInboundOverflowed = false;
    }

    if (overflowed) {
        syncLogWarn("LHolo sync: inbound frame queue overflowed; dropping oldest frames");
    }

    for (auto const& frame : inbound) {
        handleFrame(frame);
    }

    std::int64_t pendingHello = -1;
    {
        std::lock_guard lock{mMutex};
        if (mSession.state == SessionState::AwaitCapability || mSession.state == SessionState::HelloSent) {
            if (now - mSessionStartMs > static_cast<std::uint64_t>(kPeerCapabilityTimeoutMs)
                && mHelloAttempts > kRequestRetries) {
                mSession.state = SessionState::Unavailable;
                mSession.available = false;
                mSession.statusText = "server did not respond; sync unavailable";
                syncLogInfo("LHolo sync: server capability probe timed out; running as plain LHolo");
            } else if (mLastHelloMs == 0
                || now - mLastHelloMs > static_cast<std::uint64_t>(kRequestTimeoutMs)) {
                pendingHello = static_cast<std::int64_t>(now);
            }
        }
    }

    if (pendingHello >= 0) {
        {
            std::lock_guard lock{mMutex};
            mLastHelloMs = static_cast<std::uint64_t>(pendingHello);
            ++mHelloAttempts;
            mSession.state = SessionState::HelloSent;
        }
        sendHello();
    }

    pollEdit(now);

    // Game-thread bridge work (deferred dimension waits / post-load transform).
    mBridge.tick();
    pumpUpload();
}

void SyncClient::sendHello() {
    control::Hello hello;
    hello.clientVersion = "LHolo/26.51.2-ja.12-rc.1";
    hello.protocolMajor = kProtocolMajor;
    hello.protocolMinor = kProtocolMinor;
    hello.features = {"files", "placements", "edit_lock"};

    std::vector<std::uint8_t> frame = encodeFrameBytes(Opcode::Hello, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(hello));
    if (frame.empty()) {
        syncLogError("LHolo sync: failed to encode HELLO frame");
        return;
    }
    if (sendFrameToServer(frame)) {
        syncLogInfo("LHolo sync: sent HELLO to server");
    }
}

void SyncClient::handleFrame(std::vector<std::uint8_t> const& frame) {
    Frame decoded;
    std::string error;
    DecodeResult const result = decodeFrame(frame, decoded, error);
    if (result == DecodeResult::IncompatibleVersion) {
        std::lock_guard lock{mMutex};
        mSession.state = SessionState::Unavailable;
        mSession.available = false;
        mSession.statusText = "incompatible protocol version";
        syncLogWarn("LHolo sync: server sent incompatible protocol version");
        return;
    }
    if (result != DecodeResult::Ok) {
        syncLogWarn("LHolo sync: rejected inbound frame: " + error);
        return;
    }

    switch (decoded.op()) {
        case Opcode::HelloAck:
            handleHelloAck(decoded.payload);
            break;
        case Opcode::ListPage:
            handleListPage(decoded.payload);
            break;
        case Opcode::CatalogDelta:
            handleCatalogDelta(decoded.payload);
            break;
        case Opcode::FileAccept:
            handleFileAccept(decoded.payload);
            break;
        case Opcode::FileChunk:
            handleFileChunk(decoded.payload);
            break;
        case Opcode::FileAck:
            handleFileAck(decoded.payload);
            break;
        case Opcode::FileComplete:
            handleFileComplete(decoded.payload);
            break;
        case Opcode::Result:
            handleResult(decoded.requestId, decoded.payload);
            break;
        case Opcode::EditGranted:
            handleEditGranted(decoded.requestId, decoded.payload);
            break;
        case Opcode::Error: {
            control::ErrorMessage message;
            if (Status status = control::decode(decoded.payload, message); status.ok) {
                syncLogWarn("LHolo sync: server error " + std::string(errorCodeName(message.code)) + ": " + message.message);
                // An edit in flight correlates by placement only, so the
                // edit-specific codes fail the current edit session.
                // Only an error answering the request the edit is waiting for may
                // fail it: a renewal that raced the update must not turn a
                // successful commit into a reported failure.
                if (editOutcomeFromError(editInFlight(), decoded.requestId) == EditOutcome::Failed) {
                    failEdit(std::string(errorCodeName(message.code)) + ": " + message.message);
                }
                std::lock_guard lock{mMutex};
                if (message.code == ErrorCode::UnsupportedVersion || message.code == ErrorCode::Unauthorized) {
                    mSession.state = SessionState::Unavailable;
                    mSession.available = false;
                    mSession.statusText = "server rejected sync";
                }
            }
            break;
        }
        default:
            break;
    }
}

void SyncClient::handleHelloAck(std::span<std::uint8_t const> payload) {
    control::HelloAck ack;
    if (Status status = control::decode(payload, ack); !status.ok) {
        syncLogWarn("LHolo sync: invalid HELLO_ACK: " + status.message);
        return;
    }
    {
        std::lock_guard lock{mMutex};
        mSession.serverId = ack.serverId;
        mSession.worldId = ack.worldId;
        mSession.sessionId = ack.sessionId;
        mSession.role = ack.role;
        mSession.serverVersion = ack.serverVersion;
        mSession.features = ack.features;
        mSession.catalogSequence = ack.catalogSequence;
        mSession.maxFileBytes = ack.maxFileBytes;
        mSession.maxActiveTransfers = ack.maxActiveTransfers;
        mSession.state = SessionState::Ready;
        mSession.available = true;
        mSession.statusText = "connected";
    }
    syncLogInfo(
        "LHolo sync: HELLO_ACK received (server " + ack.serverVersion + ", role "
        + std::string(roleName(ack.role)) + ", catalog " + std::to_string(ack.catalogSequence) + ")"
    );

    // The cache root is the LHolo schematic library, so verified downloads land
    // where the projection loader expects them (§5.4). The endpoint key is
    // derived from the server identity here; connection-address keying is a
    // follow-up noted in GOAL_STATUS.
    auto const snap = structure::schematic::snapshot();
    if (!snap.library.empty()) {
        mEndpointKey = ClientCache::endpointKey(ack.serverId.toString(), 0);
        mCache = std::make_unique<ClientCache>(std::filesystem::path(snap.library));
    }
    requestList();
}

void SyncClient::requestList() {
    {
        std::lock_guard lock{mMutex};
        mListAccum.clear();
        mListCursor.reset();
        mListSequence.reset();
        mListInProgress = true;
        mDeltaDuringList = false;
    }
    sendListRequest(std::nullopt);
}

void SyncClient::sendListRequest(std::optional<std::string> const& cursor) {
    control::ListRequest request;
    request.cursor = cursor;
    request.pageSize = kMaxCatalogPageSize;
    {
        std::lock_guard lock{mMutex};
        request.knownSequence = mListSequence.value_or(mCatalog.sequence());
    }
    sendFrameToServer(encodeFrameBytes(Opcode::ListRequest, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(request)));
}

void SyncClient::handleListPage(std::span<std::uint8_t const> payload) {
    control::ListPage page;
    if (Status status = control::decode(payload, page); !status.ok) {
        syncLogWarn("LHolo sync: invalid LIST_PAGE: " + status.message);
        return;
    }
    bool restart = false;
    bool relist = false;
    bool pageMore = false;
    std::optional<std::string> nextCursor;
    {
        std::lock_guard lock{mMutex};
        if (!mListInProgress) {
            // Unsolicited page: treat it as the complete snapshot.
            mCatalog.applySnapshot(std::move(page.items), page.snapshotSequence);
            mSession.catalogSequence = page.snapshotSequence;
            return;
        }
        if (!mListSequence.has_value()) {
            mListSequence = page.snapshotSequence;
        }
        if (page.snapshotSequence != *mListSequence) {
            // The catalog changed between pages; restart from a clean snapshot.
            mListAccum.clear();
            mListCursor.reset();
            mListSequence.reset();
            mListInProgress = false;
            restart = true;
        } else {
            for (auto& item : page.items) {
                mListAccum.push_back(std::move(item));
            }
            if (page.hasMore && page.nextCursor.has_value()) {
                mListCursor = page.nextCursor;
                nextCursor = page.nextCursor;
                pageMore = true;
            } else {
                // Apply the assembled snapshot atomically so stale entries never
                // survive a reload.
                mCatalog.applySnapshot(std::move(mListAccum), *mListSequence);
                mSession.catalogSequence = *mListSequence;
                mListAccum.clear();
                mListCursor.reset();
                mListInProgress = false;
                if (mDeltaDuringList) {
                    mDeltaDuringList = false;
                    relist = true;
                }
            }
        }
    }
    if (restart || relist) {
        requestList();
    } else if (pageMore) {
        sendListRequest(nextCursor);
    }
}

void SyncClient::handleCatalogDelta(std::span<std::uint8_t const> payload) {
    control::CatalogDelta delta;
    if (Status status = control::decode(payload, delta); !status.ok) {
        syncLogWarn("LHolo sync: invalid CATALOG_DELTA: " + status.message);
        return;
    }
    DeltaResult result = DeltaResult::Gap;
    {
        std::lock_guard lock{mMutex};
        if (mListInProgress) {
            // A multi-page snapshot is in flight; reconcile once it settles.
            mDeltaDuringList = true;
            return;
        }
        result = mCatalog.applyDelta(delta);
        if (result == DeltaResult::Applied) {
            mSession.catalogSequence = mCatalog.sequence();
        }
    }
    if (result == DeltaResult::Gap) {
        syncLogInfo("LHolo sync: catalog gap detected; requesting full list");
        requestList();
    }
}

std::optional<std::filesystem::path> SyncClient::cachedPathFor(SharedPlacementDto const& placement) {
    if (!mCache) {
        return std::nullopt;
    }
    auto const session = snapshot();
    if (session.serverId.isNil() || session.worldId.isNil()) {
        return std::nullopt;
    }
    return mCache->findVerified(
        mEndpointKey, session.serverId, session.worldId, placement.fileHash, placement.format, 0
    );
}

bool SyncClient::selectSharedPlacement(Uuid const& placementId, std::string& status) {
    SharedPlacementDto placement;
    {
        std::lock_guard lock{mMutex};
        auto found = mCatalog.find(placementId);
        if (!found) {
            status = "placement not in catalog";
            return false;
        }
        placement = *found;
    }
    auto const cached = cachedPathFor(placement);
    if (!cached) {
        status = "shared file is not cached yet";
        return false;
    }
    auto const result = mBridge.activate(placement, *cached);
    status = result.status;
    return result.placed || result.deferredDimension;
}

std::vector<SharedPlacementDto> SyncClient::catalogItems() const {
    std::lock_guard lock{mMutex};
    return mCatalog.items();
}

void SyncClient::enqueueUi(std::function<void()> action) {
    if (!action) {
        return;
    }
    std::lock_guard lock{mUiMutex};
    if (mUiActions.size() >= 64) {
        mUiActions.pop_front();
    }
    mUiActions.push_back(std::move(action));
}

void SyncClient::enqueueRefresh() {
    enqueueUi([this] { requestList(); });
}

bool SyncClient::requestDownload(Uuid const& placementId, std::string& status) {
    SharedPlacementDto placement;
    {
        std::lock_guard lock{mMutex};
        auto found = mCatalog.find(placementId);
        if (!found) {
            status = "placement not in catalog";
            return false;
        }
        placement = *found;
    }
    if (cachedPathFor(placement)) {
        status = "already cached";
        return true;
    }
    auto const session = snapshot();
    if (!session.available) {
        status = "sync not connected";
        return false;
    }

    mDownload = DownloadState{};
    mDownload.expectedHash = placement.fileHash;
    mDownload.format = placement.format;
    mDownload.placementId = placement.placementId;
    mDownload.active = true;

    control::FileGet request;
    request.placementId = placement.placementId;
    request.sha256 = placement.fileHash;
    request.resumeOffset = 0;
    sendFrameToServer(encodeFrameBytes(Opcode::FileGet, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(request)));
    status = "requested";
    syncLogInfo("LHolo sync: requested shared file download");
    return true;
}

bool SyncClient::requestShare(std::uint64_t localPlacementId, std::string& status) {
    auto const session = snapshot();
    if (!session.available) {
        status = "sync not connected";
        return false;
    }
    if (session.role == Role::Viewer) {
        status = "upload not permitted";
        return false;
    }
    auto const snap = structure::schematic::snapshot();
    if (snap.library.empty()) {
        status = "no LHolo library";
        return false;
    }
    structure::SavedPlacement const* chosen = nullptr;
    for (auto const& placement : snap.session.document.placements) {
        if (placement.id == localPlacementId) {
            chosen = &placement;
            break;
        }
    }
    if (!chosen) {
        status = "local placement not found";
        return false;
    }
    auto const path = std::filesystem::path(snap.library) / chosen->file;
    std::error_code code;
    if (!std::filesystem::is_regular_file(paths::ioPath(path), code)) {
        status = "local file missing";
        return false;
    }
    auto const size = static_cast<std::uint64_t>(std::filesystem::file_size(paths::ioPath(path), code));
    auto const hash = hashLocalFile(path);
    if (!hash) {
        status = "cannot hash local file";
        return false;
    }
    auto const format = formatFromPath(path);
    if (format == SchematicFormat::Unknown) {
        status = "unsupported format";
        return false;
    }
    if (size == 0 || size > session.maxFileBytes) {
        status = "file size rejected";
        return false;
    }

    mUpload = UploadState{};
    mUpload.active = true;
    mUpload.sourcePath = path;
    mUpload.hash = *hash;
    mUpload.format = format;
    mUpload.clientOperationId = Uuid::random();
    mUpload.placement.fileHash = *hash;
    mUpload.placement.format = format;
    mUpload.placement.displayName = chosen->name;
    mUpload.placement.dimension = chosen->dimension;
    mUpload.placement.originX = chosen->origin.x;
    mUpload.placement.originY = chosen->origin.y;
    mUpload.placement.originZ = chosen->origin.z;
    mUpload.placement.rotation = chosen->rotation;
    mUpload.placement.mirror = chosen->mirror;

    control::FileOffer offer;
    offer.clientOperationId = mUpload.clientOperationId;
    offer.sha256 = *hash;
    offer.format = format;
    offer.sizeBytes = size;
    offer.displayFilename = chosen->name;
    sendFrameToServer(encodeFrameBytes(Opcode::FileOffer, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(offer)));
    status = "offering";
    syncLogInfo("LHolo sync: shared local placement offer sent");
    return true;
}

bool SyncClient::requestEdit(Uuid const& placementId, std::uint64_t baseRevision, std::string& status) {
    auto const session = snapshot();
    if (!session.available) {
        status = "sync not connected";
        return false;
    }
    if (session.role == Role::Viewer) {
        status = "edit not permitted";
        return false;
    }
    // Switching targets: drop a lease held for a different placement first so the
    // server never keeps an idle lock on the previous entry.
    if (mEdit.active && mEdit.phase == EditPhase::Granted && !(mEdit.placementId == placementId)) {
        releaseEditLock();
    }
    mEdit = EditState{};
    mEdit.placementId = placementId;
    mEdit.baseRevision = baseRevision;
    mEdit.requestMs = nowMillis();
    mEdit.grantRequestId = mNextEditRequestId++;
    mEdit.phase = EditPhase::Pending;
    mEdit.message = "requesting lock";
    mEdit.active = true;
    control::EditBegin begin;
    begin.placementId = placementId;
    begin.baseRevision = baseRevision;
    sendFrameToServer(encodeFrameBytes(Opcode::EditBegin, mEdit.grantRequestId, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(begin)));
    publishEditState();
    status = "requesting lock";
    return true;
}

bool SyncClient::commitEdit(Uuid const& placementId, SharedPlacementDto const& draft, std::string& status) {
    if (!mEdit.active || !(mEdit.placementId == placementId) || mEdit.lockId.isNil()) {
        status = "no edit lock";
        return false;
    }
    if (mEdit.phase != EditPhase::Granted) {
        status = "edit lock not held";
        return false;
    }
    // The commit supersedes any outstanding renewal: replies to older request
    // ids can no longer fail or complete this edit.
    mEdit.commitRequestId = mNextEditRequestId++;
    mEdit.grantRequestId = 0;
    control::PlacementUpdate update;
    update.clientOperationId = Uuid::random();
    update.placementId = placementId;
    update.baseRevision = mEdit.baseRevision;
    update.lockId = mEdit.lockId;
    update.displayName = draft.displayName;
    update.dimension = draft.dimension;
    update.originX = draft.originX;
    update.originY = draft.originY;
    update.originZ = draft.originZ;
    update.rotation = draft.rotation;
    update.mirror = draft.mirror;
    sendFrameToServer(encodeFrameBytes(Opcode::PlacementUpdate, mEdit.commitRequestId, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(update)));
    mEdit.phase = EditPhase::Committing;
    mEdit.requestMs = nowMillis();
    mEdit.message = "committing";
    publishEditState();
    status = "committing";
    return true;
}

void SyncClient::cancelEdit(Uuid const& placementId) {
    // Also dismisses a failed attempt, which has no outstanding request.
    if (!(mEdit.placementId == placementId)) {
        return;
    }
    releaseEditLock();
    mEdit = EditState{};
    mEdit.placementId = placementId;
    mEdit.message = "edit cancelled";
    publishEditState();
}
void SyncClient::applyEditStateLocked() {
    mSession.editPhase = mEdit.phase;
    mSession.editPlacementId = mEdit.placementId;
    mSession.editRevision = mEdit.baseRevision;
    mSession.editMessage = mEdit.message;
}

void SyncClient::publishEditState() {
    std::lock_guard lock{mMutex};
    applyEditStateLocked();
}

EditInFlight SyncClient::editInFlight() const {
    EditInFlight inFlight;
    inFlight.active = mEdit.active;
    inFlight.committing = mEdit.phase == EditPhase::Committing;
    inFlight.lockHeld = !mEdit.lockId.isNil();
    inFlight.placementId = mEdit.placementId;
    inFlight.grantRequestId = mEdit.grantRequestId;
    inFlight.commitRequestId = mEdit.commitRequestId;
    inFlight.baseRevision = mEdit.baseRevision;
    return inFlight;
}

void SyncClient::completeEdit(std::string message) {
    // The server erased the lease with the update, so there is nothing to release.
    mEdit.active = false;
    mEdit.lockId = Uuid{};
    mEdit.grantRequestId = 0;
    mEdit.commitRequestId = 0;
    mEdit.phase = EditPhase::Applied;
    mEdit.message = std::move(message);
    publishEditState();
    syncLogInfo("LHolo sync: " + mEdit.message);
}

std::uint64_t SyncClient::catalogRevisionFor(Uuid const& placementId) {
    std::lock_guard lock{mMutex};
    auto const found = mCatalog.find(placementId);
    return found ? found->revision : 0;
}

void SyncClient::releaseEditLock() {
    if (mEdit.lockId.isNil()) {
        return;
    }
    control::EditLease release;
    release.placementId = mEdit.placementId;
    release.lockId = mEdit.lockId;
    sendFrameToServer(encodeFrameBytes(Opcode::EditRelease, mNextEditRequestId++, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(release)));
    mEdit.lockId = Uuid{};
}

void SyncClient::failEdit(std::string message) {
    releaseEditLock();
    mEdit.active = false;
    mEdit.phase = EditPhase::Failed;
    mEdit.message = std::move(message);
    publishEditState();
    syncLogWarn("LHolo sync: " + mEdit.message);
}

void SyncClient::pollEdit(std::uint64_t nowMs) {
    if (!mEdit.active) {
        return;
    }
    if (mEdit.phase == EditPhase::Pending) {
        if (nowMs - mEdit.requestMs > kEditGrantTimeoutMs) {
            failEdit("edit lock request timed out");
        }
        return;
    }
    if (mEdit.phase == EditPhase::Committing) {
        if (nowMs - mEdit.requestMs > kEditCommitTimeoutMs) {
            // Still committing at the deadline means no ERROR answered this
            // commit, so the catalog is trustworthy: the server broadcasts the
            // applied placement right after the RESULT, and a revision past the
            // base proves the update landed. Report success instead of a timeout
            // the player cannot act on.
            if (deltaRevisionConfirmsCommit(editInFlight(), catalogRevisionFor(mEdit.placementId))) {
                completeEdit("update applied (catalog)");
            } else {
                failEdit("edit commit timed out");
            }
        }
        return;
    }
    if (mEdit.phase == EditPhase::Granted && nowMs - mEdit.lastRenewMs > kEditRenewIntervalMs) {
        control::EditLease renew;
        renew.placementId = mEdit.placementId;
        renew.lockId = mEdit.lockId;
        mEdit.grantRequestId = mNextEditRequestId++;
        sendFrameToServer(encodeFrameBytes(Opcode::EditRenew, mEdit.grantRequestId, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(renew)));
        mEdit.lastRenewMs = nowMs;
    }
}

bool SyncClient::requestDelete(Uuid const& placementId, std::uint64_t baseRevision, std::string& status) {
    auto const session = snapshot();
    if (!session.available) {
        status = "sync not connected";
        return false;
    }
    control::PlacementDelete remove;
    remove.clientOperationId = Uuid::random();
    remove.placementId = placementId;
    remove.baseRevision = baseRevision;
    sendFrameToServer(encodeFrameBytes(Opcode::PlacementDelete, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(remove)));
    status = "delete requested";
    return true;
}

void SyncClient::handleFileAccept(std::span<std::uint8_t const> payload) {
    control::FileAccept accept;
    if (Status status = control::decode(payload, accept); !status.ok) {
        syncLogWarn("LHolo sync: invalid FILE_ACCEPT: " + status.message);
        return;
    }
    if (mUpload.active && !mUpload.acceptReceived && (accept.status == "offer" || accept.status == "skip")) {
        mUpload.acceptReceived = true;
        mUpload.transferId = accept.transferId;
        if (accept.status == "skip") {
            sendPlacementCreate();
            return;
        }
        std::string error;
        if (!mUpload.sender.open(mUpload.sourcePath, accept.sizeBytes, accept.chunkBytes, accept.window, error)) {
            syncLogWarn("LHolo sync: upload stream open failed: " + error);
            mUpload = UploadState{};
        }
        return;
    }
    if (mDownload.active && accept.status == "offer") {
        ChunkAssemblyLimits limits;
        limits.chunkBytes = accept.chunkBytes;
        limits.window = accept.window;
        limits.maxFileBytes = snapshot().maxFileBytes;
        std::string error;
        if (!mDownload.assembly.prepare(
                accept.transferId, mDownload.expectedHash, accept.sizeBytes, accept.chunkBytes, accept.window, limits, error
            )) {
            syncLogWarn("LHolo sync: download prepare failed: " + error);
            mDownload = DownloadState{};
        }
    }
}

void SyncClient::handleFileChunk(std::span<std::uint8_t const> payload) {
    if (!mDownload.active) {
        return;
    }
    FileChunkPayload chunk;
    std::string error;
    if (!decodeFileChunk(payload, chunk, error)) {
        syncLogWarn("LHolo sync: invalid FILE_CHUNK: " + error);
        return;
    }
    Uuid transferId;
    std::memcpy(transferId.bytes.data(), chunk.transferId.data(), kUuidBytes);
    if (!(transferId == mDownload.assembly.transferId())) {
        return;
    }
    auto const result = mDownload.assembly.onChunk(chunk.offset, chunk.data, error);
    if (result == ChunkAssembly::Chunk::Accepted || result == ChunkAssembly::Chunk::Duplicate) {
        auto const ack = mDownload.assembly.acknowledgement();
        control::FileAck reply;
        reply.transferId = transferId;
        reply.nextOffset = ack.nextOffset;
        reply.receivedBitmap = ack.receivedBitmap;
        sendFrameToServer(encodeFrameBytes(Opcode::FileAck, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(reply)));
    } else if (result == ChunkAssembly::Chunk::Conflict || result == ChunkAssembly::Chunk::OutOfRange) {
        syncLogWarn("LHolo sync: download chunk rejected: " + error);
    }
}

void SyncClient::handleFileAck(std::span<std::uint8_t const> payload) {
    control::FileAck ack;
    if (Status status = control::decode(payload, ack); !status.ok) {
        return;
    }
    if (mUpload.active && mUpload.transferId == ack.transferId) {
        mUpload.sender.onAck(ack.nextOffset, ack.receivedBitmap);
    }
}

void SyncClient::handleFileComplete(std::span<std::uint8_t const> payload) {
    control::FileComplete complete;
    if (Status status = control::decode(payload, complete); !status.ok) {
        return;
    }
    if (complete.status == "request") {
        if (!mDownload.active) {
            return;
        }
        bool ok = false;
        if (mDownload.assembly.assembled()) {
            auto const& bytes = mDownload.assembly.bytes();
            auto const actual = sha256::hash(bytes.data(), bytes.size());
            auto const session = snapshot();
            if (actual == mDownload.expectedHash && mCache && !session.serverId.isNil() && !session.worldId.isNil()) {
                auto const part = mCache->partPath(
                    mEndpointKey, session.serverId, session.worldId, mDownload.expectedHash, mDownload.format
                );
                std::error_code code;
                std::filesystem::create_directories(paths::ioPath(part.parent_path()), code);
                {
                    std::ofstream out(paths::ioPath(part), std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                }
                std::string error;
                auto published = mCache->publishPart(
                    mEndpointKey, session.serverId, session.worldId, mDownload.expectedHash, mDownload.format,
                    bytes.size(), error
                );
                ok = published.has_value();
                if (!ok) {
                    syncLogWarn("LHolo sync: cache publish failed: " + error);
                }
            }
        }
        control::FileComplete reply;
        reply.transferId = complete.transferId;
        reply.sha256 = mDownload.expectedHash;
        reply.status = ok ? "ok" : "failed";
        sendFrameToServer(encodeFrameBytes(Opcode::FileComplete, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(reply)));
        if (ok) {
            syncLogInfo("LHolo sync: shared file downloaded and cached");
        }
        mDownload = DownloadState{};
        return;
    }
    if (mUpload.active && mUpload.completeSent && mUpload.transferId == complete.transferId) {
        if (complete.status == "ok") {
            sendPlacementCreate();
        } else {
            syncLogWarn("LHolo sync: server rejected the uploaded file");
            mUpload = UploadState{};
        }
    }
}

void SyncClient::handleResult(std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::Result result;
    if (Status status = control::decode(payload, result); !status.ok) {
        return;
    }
        auto const outcome = editOutcomeFromResult(
        editInFlight(), requestId, result.placementId.has_value(), result.placementId.value_or(Uuid{}), result.status
    );
    if (outcome == EditOutcome::Applied) {
        completeEdit("update applied");
    } else if (outcome == EditOutcome::Failed) {
        failEdit("edit rejected: " + result.status);
    }
    syncLogInfo(
        "LHolo sync: result " + result.status
        + (result.revision ? (" rev " + std::to_string(*result.revision)) : std::string{})
        + (result.catalogSequence ? (" seq " + std::to_string(*result.catalogSequence)) : std::string{})
    );
}

void SyncClient::handleEditGranted(std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::EditGranted granted;
    if (Status status = control::decode(payload, granted); !status.ok) {
        return;
    }
    if (editOutcomeFromGranted(editInFlight(), requestId, granted.placementId) == EditOutcome::Granted) {
        mEdit.lockId = granted.lockId;
        mEdit.baseRevision = granted.baseRevision;
        mEdit.lastRenewMs = nowMillis();
        mEdit.phase = EditPhase::Granted;
        mEdit.message = "edit lock held";
        publishEditState();
        syncLogInfo("LHolo sync: edit lock granted");
    }
}

void SyncClient::pumpUpload() {
    if (!mUpload.active || !mUpload.acceptReceived || mUpload.completeSent) {
        return;
    }
    auto const chunks = mUpload.sender.poll(kMaxFileChunkBytes * 2);
    for (auto const& chunk : chunks) {
        std::vector<std::uint8_t> out;
        if (encodeFileChunk(mUpload.transferId.bytes, chunk.offset, chunk.data, out)) {
            sendFrameToServer(
                encodeFrameBytes(Opcode::FileChunk, 0, std::array<std::uint8_t, kSessionIdBytes>{}, std::move(out))
            );
        }
    }
    if (mUpload.sender.allAcked()) {
        control::FileComplete complete;
        complete.transferId = mUpload.transferId;
        complete.sha256 = mUpload.hash;
        complete.status = "request";
        sendFrameToServer(encodeFrameBytes(Opcode::FileComplete, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(complete)));
        mUpload.completeSent = true;
    }
}

void SyncClient::sendPlacementCreate() {
    if (!mUpload.active) {
        return;
    }
    control::PlacementCreate create;
    create.clientOperationId = mUpload.clientOperationId;
    create.displayName = mUpload.placement.displayName;
    create.fileSha256 = mUpload.hash;
    create.fileFormat = mUpload.format;
    create.dimension = mUpload.placement.dimension;
    create.originX = mUpload.placement.originX;
    create.originY = mUpload.placement.originY;
    create.originZ = mUpload.placement.originZ;
    create.rotation = mUpload.placement.rotation;
    create.mirror = mUpload.placement.mirror;
    sendFrameToServer(encodeFrameBytes(Opcode::PlacementCreate, 0, std::array<std::uint8_t, kSessionIdBytes>{}, control::encode(create)));
    syncLogInfo("LHolo sync: placement create sent");
    mUpload = UploadState{};
}

ClientSessionSnapshot SyncClient::snapshot() const {
    std::lock_guard lock{mMutex};
    return mSession;
}

} // namespace lholo::sync
