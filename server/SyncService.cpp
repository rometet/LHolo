#include "SyncService.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "ServerPacketAdapter.h"

#include "PlayerIdentity.h"

#include "sync/common/ControlMessages.h"
#include "sync/common/FrameCodec.h"

namespace lholo::sync {
namespace {

constexpr std::size_t kMaxInboundFrames = 256;

std::int64_t nowUnixMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::vector<std::uint8_t> encodeFrameBytes(
    Opcode opcode,
    std::uint64_t requestId,
    std::array<std::uint8_t, kSessionIdBytes> const& sessionId,
    std::vector<std::uint8_t> payload
) {
    Frame frame;
    frame.opcode = static_cast<std::uint16_t>(opcode);
    frame.requestId = requestId;
    frame.sessionId = sessionId;
    frame.payload = std::move(payload);
    std::vector<std::uint8_t> out;
    if (!encodeFrame(frame, out)) {
        out.clear();
    }
    return out;
}

Uuid uuidFromArray(std::array<std::uint8_t, kUuidBytes> const& bytes) {
    Uuid id;
    std::memcpy(id.bytes.data(), bytes.data(), kUuidBytes);
    return id;
}

} // namespace

SyncService& SyncService::instance() {
    static SyncService service;
    return service;
}

bool SyncService::start(
    std::string const& serverVersion,
    ServerIdentity const& identity,
    ServerConfig const& config,
    std::filesystem::path const& dataDir
) {
    std::lock_guard lock{mMutex};
    mServerId = identity.serverId;
    mWorldId = identity.worldId;
    mServerVersion = serverVersion;
    mConfig = config;
    mMaxFileBytes = config.limits.maxFileMiB * 1024ull * 1024ull;
    if (mMaxFileBytes == 0) {
        mMaxFileBytes = kDefaultMaxFileBytes;
    }

    mRepository = std::make_unique<PlacementRepository>();
    std::string error;
    if (!mRepository->open(dataDir / "sync.db", error)) {
        serverLogError("LHoloSyncServer: cannot open database: " + error);
        mRepository.reset();
        return false;
    }

    FileStoreLimits limits;
    limits.maxFileBytes = mMaxFileBytes;
    limits.maxStagingBytes = config.limits.maxStagingMiB * 1024ull * 1024ull;
    mFileStore = std::make_unique<FileStore>(dataDir / "objects", dataDir / "staging", limits);

    mSessions.clear();
    mInbound.clear();
    mDisconnects.clear();
    mReceivers.clear();
    mReceiverOwners.clear();
    mReceiverActors.clear();
    mDownloads.clear();
    mLeases.clear();
    mRunning = true;
    return true;
}

void SyncService::stop() noexcept {
    std::lock_guard lock{mMutex};
    mRunning = false;
    mInbound.clear();
    mDisconnects.clear();
    mSessions.clear();
    mReceivers.clear();
    mReceiverOwners.clear();
    mReceiverActors.clear();
    for (auto& entry : mDownloads) {
        entry.second.sender.close();
    }
    mDownloads.clear();
    mLeases.clear();
    if (mRepository) {
        mRepository->close();
        mRepository.reset();
    }
    mFileStore.reset();
}

void SyncService::enqueueInbound(NetworkIdentifier const& source, std::vector<std::uint8_t> frame) {
    std::lock_guard lock{mMutex};
    if (!mRunning) {
        return;
    }
    if (mInbound.size() >= kMaxInboundFrames) {
        mInbound.pop_front();
    }
    mInbound.push_back(InboundFrame{source, std::move(frame)});
}

void SyncService::enqueueDisconnect(NetworkIdentifier const& source) {
    std::lock_guard lock{mMutex};
    if (!mRunning) {
        return;
    }
    mDisconnects.push_back(source);
}

void SyncService::tick() {
    std::deque<InboundFrame> inbound;
    std::deque<NetworkIdentifier> disconnects;
    {
        std::lock_guard lock{mMutex};
        if (!mRunning) {
            return;
        }
        inbound.swap(mInbound);
        disconnects.swap(mDisconnects);
    }

    for (auto const& source : disconnects) {
        std::string actorKey;
        if (auto const it = mSessions.find(source); it != mSessions.end()) {
            actorKey = it->second.actorKey;
        }
        PlayerIdentityResolver::instance().erase(source);
        mSessions.erase(source);
        releaseLeasesOf(actorKey);
        releaseDownloadsOf(source);
        releaseUploadsOf(source);
    }
    for (auto const& entry : inbound) {
        handleFrame(entry.source, entry.frame);
    }
    expireLeases();
    pumpDownloads();
}

void SyncService::handleFrame(NetworkIdentifier const& source, std::vector<std::uint8_t> const& frame) {
    Frame decoded;
    std::string error;
    DecodeResult const result = decodeFrame(frame, decoded, error);
    if (result == DecodeResult::IncompatibleVersion) {
        sendError(source, decoded.requestId, ErrorCode::UnsupportedVersion, "unsupported protocol version");
        return;
    }
    if (result != DecodeResult::Ok) {
        serverLogWarn("LHoloSyncServer: rejected inbound frame: " + error);
        return;
    }

    // Never process post-handshake requests from a peer with no live session.
    if (decoded.op() != Opcode::Hello) {
        auto const session = mSessions.find(source);
        if (session == mSessions.end() || !session->second.ready) {
            sendError(source, decoded.requestId, ErrorCode::Unauthorized, "no active sync session");
            return;
        }
        // Centralised write gate: mutation requires a stable authenticated
        // identity AND a writer role. Defensive even if a handler forgets.
        if (isWriterOpcode(decoded.op())) {
            if (!session->second.authenticated) {
                sendError(source, decoded.requestId, ErrorCode::Unauthorized, "authenticated identity required");
                return;
            }
            if (!mayWrite(session->second.role)) {
                sendError(source, decoded.requestId, ErrorCode::Forbidden, "write not permitted for role");
                return;
            }
        }
    }

    switch (decoded.op()) {
        case Opcode::Hello: handleHello(source, decoded.payload); break;
        case Opcode::ListRequest: handleListRequest(source, decoded.requestId, decoded.payload); break;
        case Opcode::FileOffer: handleFileOffer(source, decoded.requestId, decoded.payload); break;
        case Opcode::FileGet: handleFileGet(source, decoded.requestId, decoded.payload); break;
        case Opcode::FileChunk: handleFileChunk(source, decoded.payload); break;
        case Opcode::FileAck: handleFileAck(source, decoded.payload); break;
        case Opcode::FileComplete: handleFileComplete(source, decoded.requestId, decoded.payload); break;
        case Opcode::PlacementCreate: handlePlacementCreate(source, decoded.requestId, decoded.payload); break;
        case Opcode::PlacementUpdate: handlePlacementUpdate(source, decoded.requestId, decoded.payload); break;
        case Opcode::PlacementDelete: handlePlacementDelete(source, decoded.requestId, decoded.payload); break;
        case Opcode::EditBegin: handleEditBegin(source, decoded.requestId, decoded.payload); break;
        case Opcode::EditRenew: handleEditRenew(source, decoded.requestId, decoded.payload); break;
        case Opcode::EditRelease: handleEditRelease(source, decoded.requestId, decoded.payload); break;
        default: sendError(source, decoded.requestId, ErrorCode::InvalidPayload, "unexpected opcode"); break;
    }
}

void SyncService::handleHello(NetworkIdentifier const& source, std::span<std::uint8_t const> payload) {
    control::Hello hello;
    if (Status status = control::decode(payload, hello); !status.ok) {
        sendError(source, 0, ErrorCode::InvalidPayload, status.message);
        return;
    }
    serverLogInfo(
        "LHoloSyncServer: HELLO (client " + hello.clientVersion + ", protocol "
        + std::to_string(hello.protocolMajor) + "." + std::to_string(hello.protocolMinor) + ")"
    );
    if (hello.protocolMajor != kProtocolMajor) {
        sendError(source, 0, ErrorCode::UnsupportedVersion, "protocol major mismatch");
        return;
    }
    auto const identity = PlayerIdentityResolver::instance().resolve(source);
    ServerSessionInfo& session = mSessions[source];
    session.sessionId = Uuid::random();
    session.authenticated = identity.has_value() && identity->authenticated();
    session.isOperator = identity.has_value() && identity->isOperator;
    session.actorKey = session.authenticated ? identity->actorKey() : std::string{};
    session.role = roleFor(session);
    session.ready = true;
    sendHelloAck(source, session);
}

Role SyncService::roleFor(ServerSessionInfo const& session) {
    // No stable server-authenticated identity -> read-only. Never trust a
    // connection-derived (IP:port) value for ownership or privilege.
    if (!session.authenticated) {
        return Role::Viewer;
    }
    if (mRepository) {
        if (auto role = mRepository->getRole(session.actorKey); role.has_value()) {
            return *role;
        }
    }
    if (mConfig.operatorIsAdmin && session.isOperator) {
        return Role::Admin;
    }
    if (mConfig.allowAuthenticatedUploads) {
        return Role::Member;
    }
    Role role = Role::Viewer;
    parseRole(mConfig.defaultRole, role);
    return role;
}

bool SyncService::isWriterOpcode(Opcode opcode) {
    switch (opcode) {
        case Opcode::FileOffer:
        case Opcode::FileChunk:
        case Opcode::FileComplete:
        case Opcode::PlacementCreate:
        case Opcode::PlacementUpdate:
        case Opcode::PlacementDelete:
        case Opcode::EditBegin:
        case Opcode::EditRenew:
        case Opcode::EditRelease:
            return true;
        default:
            return false;
    }
}

ServerSessionInfo* SyncService::sessionFor(NetworkIdentifier const& source) {
    auto it = mSessions.find(source);
    return it == mSessions.end() ? nullptr : &it->second;
}

std::uint32_t SyncService::activeTransfersFor(std::string const& actorKey) const {
    if (actorKey.empty()) {
        return 0;
    }
    std::uint32_t count = 0;
    for (auto const& entry : mReceiverActors) {
        if (entry.second == actorKey) {
            ++count;
        }
    }
    for (auto const& entry : mDownloads) {
        if (entry.second.actorKey == actorKey) {
            ++count;
        }
    }
    return count;
}

void SyncService::sendHelloAck(NetworkIdentifier const& source, ServerSessionInfo& session) {
    control::HelloAck ack;
    ack.serverVersion = mServerVersion;
    ack.serverId = mServerId;
    ack.worldId = mWorldId;
    ack.sessionId = session.sessionId;
    ack.features = {"files", "placements", "edit_lock"};
    ack.role = session.role;
    ack.maxFileBytes = mMaxFileBytes;
    ack.maxActiveTransfers = static_cast<std::uint32_t>(mConfig.limits.maxTransfersPerUser);
    ack.catalogSequence = mRepository ? mRepository->currentSequence() : 0;
    sendFrame(source, Opcode::HelloAck, 0, control::encode(ack));
}

void SyncService::handleListRequest(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    if (!mRepository) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    control::ListRequest request;
    if (Status status = control::decode(payload, request); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const page = mRepository->listPage(request.cursor, request.pageSize);
    control::ListPage out;
    out.items = page.items;
    out.nextCursor = page.nextCursor;
    out.snapshotSequence = page.snapshotSequence;
    out.hasMore = page.hasMore;
    sendFrame(source, Opcode::ListPage, requestId, control::encode(out));
}

void SyncService::handleFileOffer(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::FileOffer offer;
    if (Status status = control::decode(payload, offer); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    if (!mFileStore) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end() || !sessionIt->second.ready) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    if (!mayWrite(sessionIt->second.role)) {
        sendError(source, requestId, ErrorCode::Forbidden, "upload not permitted");
        return;
    }
    if (offer.sizeBytes > mMaxFileBytes) {
        sendError(source, requestId, ErrorCode::FileTooLarge, "file exceeds limit");
        return;
    }

    if (activeTransfersFor(sessionIt->second.actorKey) >= mConfig.limits.maxTransfersPerUser
        || mReceivers.size() + mDownloads.size() >= mConfig.limits.maxTotalTransfers) {
        sendError(source, requestId, ErrorCode::CapacityExceeded, "transfer quota reached");
        return;
    }

    bool const present = mFileStore->hasObject(offer.sha256, offer.format, offer.sizeBytes);
    if (!present && mFileStore->totalObjectBytes() + offer.sizeBytes
                        > mConfig.limits.maxTotalStoreMiB * 1024ull * 1024ull) {
        sendError(source, requestId, ErrorCode::CapacityExceeded, "server storage quota reached");
        return;
    }
    control::FileAccept accept;
    accept.transferId = Uuid::random();
    accept.status = present ? "skip" : "offer";
    accept.needBytes = !present;
    accept.sizeBytes = offer.sizeBytes;
    accept.resumeOffset = 0;
    accept.chunkBytes = mConfig.transfer.chunkBytes;
    accept.window = mConfig.transfer.window;

    if (!present) {
        TransferLimits limits;
        limits.chunkBytes = mConfig.transfer.chunkBytes;
        limits.window = mConfig.transfer.window;
        limits.maxFileBytes = mMaxFileBytes;
        TransferReceiver receiver(*mFileStore, limits);
        TransferReceiver::Offer prepared;
        std::string error;
        if (!receiver.prepare(
                accept.transferId, offer.sha256, offer.format, offer.sizeBytes, mConfig.transfer.chunkBytes,
                prepared, error
            )) {
            sendError(source, requestId, ErrorCode::InvalidPayload, error);
            return;
        }
        accept.chunkBytes = prepared.chunkBytes;
        accept.window = prepared.window;
        std::string const transferKey = accept.transferId.toString();
        mReceivers.try_emplace(transferKey, std::move(receiver));
        mReceiverOwners.insert_or_assign(transferKey, source);
        mReceiverActors.insert_or_assign(transferKey, sessionIt->second.actorKey);
    }
    sendFrame(source, Opcode::FileAccept, requestId, control::encode(accept));
}

void SyncService::handleFileGet(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::FileGet request;
    if (Status status = control::decode(payload, request); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    if (!mFileStore || !mRepository) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end() || !sessionIt->second.ready) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no active sync session");
        return;
    }
    if (activeTransfersFor(sessionIt->second.actorKey) >= mConfig.limits.maxTransfersPerUser
        || mReceivers.size() + mDownloads.size() >= mConfig.limits.maxTotalTransfers) {
        sendError(source, requestId, ErrorCode::CapacityExceeded, "transfer quota reached");
        return;
    }
    // The client may only fetch files referenced by a placement it can see; an
    // arbitrary hash must not reveal another player's object (§4.6/§7.3).
    auto const placement = mRepository->getPlacement(request.placementId);
    if (!placement || placement->deleted || placement->fileHash != request.sha256) {
        sendError(source, requestId, ErrorCode::NotFound, "file not available");
        return;
    }
    auto const record = mRepository->getFile(request.sha256);
    if (!record || !mFileStore->hasObject(request.sha256, record->format, record->sizeBytes)) {
        sendError(source, requestId, ErrorCode::NotFound, "file not available");
        return;
    }

    std::uint32_t const chunkBytes = mConfig.transfer.chunkBytes == 0 ? kDefaultChunkBytes : mConfig.transfer.chunkBytes;
    std::uint32_t const window = mConfig.transfer.window == 0 ? kDefaultWindow : mConfig.transfer.window;
    std::uint64_t resume = request.resumeOffset;
    if (resume > record->sizeBytes) {
        resume = 0;
    }
    resume -= resume % chunkBytes; // resume only on fixed chunk boundaries

    TransferSender sender;
    std::string error;
    if (!sender.open(
            mFileStore->objectPath(request.sha256, record->format), record->sizeBytes, chunkBytes, window, error
        )) {
        sendError(source, requestId, ErrorCode::StorageError, error);
        return;
    }
    sender.setStartOffset(resume);

    Uuid const transferId = Uuid::random();
    DownloadSession session;
    session.target = source;
    session.actorKey = sessionIt->second.actorKey;
    session.transferId = transferId;
    session.hash = request.sha256;
    session.sender = std::move(sender);
    session.lastActivityMs = nowUnixMs();
    mDownloads[transferId.toString()] = std::move(session);

    control::FileAccept accept;
    accept.transferId = transferId;
    accept.status = "offer";
    accept.needBytes = true;
    accept.sizeBytes = record->sizeBytes;
    accept.resumeOffset = resume;
    accept.chunkBytes = chunkBytes;
    accept.window = window;
    sendFrame(source, Opcode::FileAccept, requestId, control::encode(accept));
    serverLogInfo("LHoloSyncServer: download offer " + transferId.toString());
}

void SyncService::handleFileChunk(NetworkIdentifier const& source, std::span<std::uint8_t const> payload) {
    FileChunkPayload chunk;
    std::string error;
    if (!decodeFileChunk(payload, chunk, error)) {
        sendError(source, 0, ErrorCode::InvalidPayload, error);
        return;
    }
    Uuid const transferId = uuidFromArray(chunk.transferId);
    auto it = mReceivers.find(transferId.toString());
    if (it == mReceivers.end()) {
        sendError(source, 0, ErrorCode::NotFound, "unknown transfer");
        return;
    }
    auto const owner = mReceiverOwners.find(transferId.toString());
    if (owner == mReceiverOwners.end() || !(owner->second == source)) {
        sendError(source, 0, ErrorCode::Forbidden, "transfer belongs to another connection");
        return;
    }
    auto const result = it->second.onChunk(chunk.offset, chunk.data, error);
    switch (result) {
        case TransferReceiver::Chunk::Accepted:
        case TransferReceiver::Chunk::Duplicate: {
            auto const ack = it->second.acknowledgement();
            control::FileAck ackPayload;
            ackPayload.transferId = transferId;
            ackPayload.nextOffset = ack.nextOffset;
            ackPayload.receivedBitmap = ack.receivedBitmap;
            sendFrame(source, Opcode::FileAck, 0, control::encode(ackPayload));
            break;
        }
        case TransferReceiver::Chunk::Conflict:
        case TransferReceiver::Chunk::OutOfRange:
        case TransferReceiver::Chunk::NotPrepared:
            sendError(source, 0, ErrorCode::InvalidPayload, error);
            break;
    }
}

void SyncService::handleFileAck(NetworkIdentifier const& source, std::span<std::uint8_t const> payload) {
    control::FileAck ack;
    if (Status status = control::decode(payload, ack); !status.ok) {
        return;
    }
    auto it = mDownloads.find(ack.transferId.toString());
    if (it == mDownloads.end() || !it->second.target || !(*it->second.target == source)) {
        return;
    }
    it->second.sender.onAck(ack.nextOffset, ack.receivedBitmap);
    it->second.lastActivityMs = nowUnixMs();
}

void SyncService::handleFileComplete(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::FileComplete complete;
    if (Status status = control::decode(payload, complete); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    // Receiver verdict for a server->client download.
    if (complete.status == "ok" || complete.status == "failed") {
        auto it = mDownloads.find(complete.transferId.toString());
        if (it != mDownloads.end() && it->second.target && *it->second.target == source) {
            it->second.sender.close();
            mDownloads.erase(it);
            serverLogInfo(
                "LHoloSyncServer: download finished (" + complete.status + ") " + complete.transferId.toString()
            );
        }
        return;
    }
    if (complete.status != "request") {
        return;
    }
    auto it = mReceivers.find(complete.transferId.toString());
    if (it == mReceivers.end()) {
        sendError(source, requestId, ErrorCode::NotFound, "unknown transfer");
        return;
    }
    auto const owner = mReceiverOwners.find(complete.transferId.toString());
    if (owner == mReceiverOwners.end() || !(owner->second == source)) {
        sendError(source, requestId, ErrorCode::Forbidden, "transfer belongs to another connection");
        return;
    }
    Sha256Hash const hash = it->second.hash();
    SchematicFormat const format = it->second.format();
    std::uint64_t const sizeBytes = it->second.sizeBytes();

    std::string error;
    bool const ok = it->second.complete(error);
    control::FileComplete reply;
    reply.transferId = complete.transferId;
    reply.sha256 = hash;
    reply.status = ok ? "ok" : "failed";
    mReceivers.erase(it);
    mReceiverOwners.erase(complete.transferId.toString());
    mReceiverActors.erase(complete.transferId.toString());

    if (ok && mRepository) {
        FileRecord record;
        record.sha256 = hash;
        record.format = format;
        record.sizeBytes = sizeBytes;
        record.createdAtMs = nowUnixMs();
        record.validated = true;
        bool existed = false;
        std::string dbError;
        if (!mRepository->insertFile(record, existed, dbError)) {
            serverLogWarn("LHoloSyncServer: file record insert failed: " + dbError);
        }
        mRepository->setFileValidated(hash, true);
    }
    sendFrame(source, Opcode::FileComplete, requestId, control::encode(reply));
    if (!ok) {
        serverLogWarn("LHoloSyncServer: transfer failed verification: " + error);
    }
}

void SyncService::handlePlacementCreate(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    if (!mRepository || !mFileStore) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    control::PlacementCreate create;
    if (Status status = control::decode(payload, create); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end()) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    if (!mayWrite(sessionIt->second.role)) {
        sendError(source, requestId, ErrorCode::Forbidden, "not permitted");
        return;
    }
    if (mRepository->activePlacementCount(sessionIt->second.actorKey) >= mConfig.limits.maxOwnerPlacements) {
        sendError(source, requestId, ErrorCode::CapacityExceeded, "per-user placement quota reached");
        return;
    }
    if (mRepository->totalPlacementCount() >= mConfig.limits.maxTotalPlacements) {
        sendError(source, requestId, ErrorCode::CapacityExceeded, "server placement quota reached");
        return;
    }
    auto const fileRecord = mRepository->getFile(create.fileSha256);
    if (!fileRecord || !fileRecord->validated
        || !mFileStore->hasObject(create.fileSha256, fileRecord->format, fileRecord->sizeBytes)) {
        sendError(source, requestId, ErrorCode::NotFound, "file not uploaded");
        return;
    }

    PlacementRepository::PlacementInput input;
    input.fileHash = create.fileSha256;
    input.format = fileRecord->format;
    input.displayName = create.displayName;
    input.dimension = create.dimension;
    input.originX = create.originX;
    input.originY = create.originY;
    input.originZ = create.originZ;
    input.rotation = create.rotation;
    input.mirror = create.mirror;

    std::string error;
    auto created = mRepository->createPlacement(input, sessionIt->second.actorKey, nowUnixMs(), error);
    if (!created) {
        sendError(source, requestId, ErrorCode::StorageError, error);
        return;
    }
    control::Result result;
    result.status = "ok";
    result.placementId = created->placementId;
    result.revision = created->revision;
    result.catalogSequence = mRepository->currentSequence();
    sendResult(source, requestId, result);
    broadcastCatalogEvent("upsert", *created);
}

void SyncService::handleEditBegin(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    if (!mRepository) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    control::EditBegin begin;
    if (Status status = control::decode(payload, begin); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end()) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    auto const placement = mRepository->getPlacement(begin.placementId);
    if (!placement || placement->deleted) {
        sendError(source, requestId, ErrorCode::NotFound, "placement not found");
        return;
    }
    bool const isAdmin = sessionIt->second.role == Role::Admin;
    if (placement->ownerId != sessionIt->second.actorKey && !isAdmin) {
        sendError(source, requestId, ErrorCode::Forbidden, "not owner");
        return;
    }
    if (placement->revision != begin.baseRevision) {
        sendError(source, requestId, ErrorCode::RevisionConflict, "stale revision");
        return;
    }
    std::string const key = begin.placementId.toString();
    std::int64_t const now = nowUnixMs();
    auto const existing = mLeases.find(key);
    if (existing != mLeases.end() && existing->second.expiresAtMs > now
        && existing->second.holderKey != sessionIt->second.actorKey) {
        sendError(source, requestId, ErrorCode::Locked, "locked by another editor");
        return;
    }
    Lease lease;
    lease.holderKey = sessionIt->second.actorKey;
    lease.lockId = Uuid::random();
    lease.baseRevision = begin.baseRevision;
    lease.expiresAtMs = now + static_cast<std::int64_t>(mConfig.editLockSeconds) * 1000;
    mLeases[key] = lease;

    control::EditGranted granted;
    granted.placementId = begin.placementId;
    granted.baseRevision = begin.baseRevision;
    granted.lockId = lease.lockId;
    granted.expiresAtUnixMs = lease.expiresAtMs;
    sendFrame(source, Opcode::EditGranted, requestId, control::encode(granted));
}

void SyncService::handleEditRenew(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::EditLease renew;
    if (Status status = control::decode(payload, renew); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end()) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    auto it = mLeases.find(renew.placementId.toString());
    if (it == mLeases.end() || it->second.holderKey != sessionIt->second.actorKey
        || !(it->second.lockId == renew.lockId)) {
        sendError(source, requestId, ErrorCode::Locked, "no lease");
        return;
    }
    it->second.expiresAtMs = nowUnixMs() + static_cast<std::int64_t>(mConfig.editLockSeconds) * 1000;
    control::EditGranted granted;
    granted.placementId = renew.placementId;
    granted.baseRevision = it->second.baseRevision;
    granted.lockId = it->second.lockId;
    granted.expiresAtUnixMs = it->second.expiresAtMs;
    sendFrame(source, Opcode::EditGranted, requestId, control::encode(granted));
}

void SyncService::handleEditRelease(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    control::EditLease release;
    if (Status status = control::decode(payload, release); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end()) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    auto it = mLeases.find(release.placementId.toString());
    if (it != mLeases.end() && it->second.holderKey == sessionIt->second.actorKey && it->second.lockId == release.lockId) {
        mLeases.erase(it);
    }
    control::Result result;
    result.status = "ok";
    sendResult(source, requestId, result);
}

void SyncService::handlePlacementUpdate(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    if (!mRepository) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    control::PlacementUpdate update;
    if (Status status = control::decode(payload, update); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end()) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    auto const leaseIt = mLeases.find(update.placementId.toString());
    if (leaseIt == mLeases.end() || leaseIt->second.holderKey != sessionIt->second.actorKey
        || !(leaseIt->second.lockId == update.lockId) || leaseIt->second.expiresAtMs <= nowUnixMs()
        || leaseIt->second.baseRevision != update.baseRevision) {
        sendError(source, requestId, ErrorCode::Locked, "no valid lease");
        return;
    }
    auto const existing = mRepository->getPlacement(update.placementId);
    if (!existing) {
        sendError(source, requestId, ErrorCode::NotFound, "placement not found");
        return;
    }
    PlacementRepository::PlacementInput input;
    input.fileHash = existing->fileHash;
    input.format = existing->format;
    input.displayName = update.displayName;
    input.dimension = update.dimension;
    input.originX = update.originX;
    input.originY = update.originY;
    input.originZ = update.originZ;
    input.rotation = update.rotation;
    input.mirror = update.mirror;

    bool const isAdmin = sessionIt->second.role == Role::Admin;
    std::string error;
    SharedPlacementDto out;
    PlacementRepository::Mutation mutation{};
    if (!mRepository->updatePlacement(
            update.placementId, update.baseRevision, input, sessionIt->second.actorKey, isAdmin, nowUnixMs(), out,
            mutation, error
        )) {
        sendError(source, requestId, ErrorCode::StorageError, error);
        return;
    }
    switch (mutation) {
        case PlacementRepository::Mutation::Ok: break;
        case PlacementRepository::Mutation::NotFound:
            sendError(source, requestId, ErrorCode::NotFound, "placement not found");
            return;
        case PlacementRepository::Mutation::Forbidden:
            sendError(source, requestId, ErrorCode::Forbidden, "not owner");
            return;
        case PlacementRepository::Mutation::Conflict:
            sendError(source, requestId, ErrorCode::RevisionConflict, "stale revision");
            return;
    }
    mLeases.erase(update.placementId.toString());
    control::Result result;
    result.status = "ok";
    result.placementId = out.placementId;
    result.revision = out.revision;
    result.catalogSequence = mRepository->currentSequence();
    sendResult(source, requestId, result);
    broadcastCatalogEvent("upsert", out);
}

void SyncService::handlePlacementDelete(NetworkIdentifier const& source, std::uint64_t requestId, std::span<std::uint8_t const> payload) {
    if (!mRepository) {
        sendError(source, requestId, ErrorCode::StorageError, "storage unavailable");
        return;
    }
    control::PlacementDelete remove;
    if (Status status = control::decode(payload, remove); !status.ok) {
        sendError(source, requestId, ErrorCode::InvalidPayload, status.message);
        return;
    }
    auto const sessionIt = mSessions.find(source);
    if (sessionIt == mSessions.end()) {
        sendError(source, requestId, ErrorCode::Unauthorized, "no session");
        return;
    }
    bool const isAdmin = sessionIt->second.role == Role::Admin;
    std::string error;
    SharedPlacementDto out;
    PlacementRepository::Mutation mutation{};
    if (!mRepository->deletePlacement(
            remove.placementId, remove.baseRevision, sessionIt->second.actorKey, isAdmin, nowUnixMs(), out, mutation,
            error
        )) {
        sendError(source, requestId, ErrorCode::StorageError, error);
        return;
    }
    switch (mutation) {
        case PlacementRepository::Mutation::Ok: break;
        case PlacementRepository::Mutation::NotFound:
            sendError(source, requestId, ErrorCode::NotFound, "placement not found");
            return;
        case PlacementRepository::Mutation::Forbidden:
            sendError(source, requestId, ErrorCode::Forbidden, "not owner");
            return;
        case PlacementRepository::Mutation::Conflict:
            sendError(source, requestId, ErrorCode::RevisionConflict, "stale revision");
            return;
    }
    mLeases.erase(remove.placementId.toString());
    control::Result result;
    result.status = "ok";
    result.placementId = out.placementId;
    result.revision = out.revision;
    result.catalogSequence = mRepository->currentSequence();
    sendResult(source, requestId, result);
    broadcastCatalogEvent("delete", out);
}

void SyncService::sendResult(NetworkIdentifier const& source, std::uint64_t requestId, control::Result const& result) {
    sendFrame(source, Opcode::Result, requestId, control::encode(result));
}

void SyncService::sendError(
    NetworkIdentifier const& source,
    std::uint64_t requestId,
    ErrorCode code,
    std::string const& message,
    bool retryable
) {
    control::ErrorMessage payload;
    payload.code = code;
    payload.message = message;
    payload.retryable = retryable;
    sendFrame(source, Opcode::Error, requestId, control::encode(payload));
}

void SyncService::sendFrame(
    NetworkIdentifier const& source,
    Opcode opcode,
    std::uint64_t requestId,
    std::vector<std::uint8_t> payload
) {
    std::array<std::uint8_t, kSessionIdBytes> sessionId{};
    auto const it = mSessions.find(source);
    if (it != mSessions.end()) {
        sessionId = it->second.sessionId.bytes;
    }
    auto frame = encodeFrameBytes(opcode, requestId, sessionId, std::move(payload));
    if (!frame.empty()) {
        static_cast<void>(sendFrameToClient(source, frame));
    }
}

void SyncService::broadcastDelta(std::vector<std::uint8_t> payload) {
    for (auto const& entry : mSessions) {
        if (!entry.second.ready) {
            continue;
        }
        auto frame = encodeFrameBytes(Opcode::CatalogDelta, 0, entry.second.sessionId.bytes, payload);
        if (!frame.empty()) {
            static_cast<void>(sendFrameToClient(entry.first, frame));
        }
    }
}

void SyncService::broadcastCatalogEvent(std::string const& action, SharedPlacementDto const& placement) {
    if (!mRepository) {
        return;
    }
    control::CatalogDelta delta;
    delta.sequence = mRepository->currentSequence();
    delta.action = action;
    delta.placement = placement;
    broadcastDelta(control::encode(delta));
}

void SyncService::releaseLeasesOf(std::string const& actorKey) {
    if (actorKey.empty()) {
        return;
    }
    std::erase_if(mLeases, [&](auto const& entry) { return entry.second.holderKey == actorKey; });
}

void SyncService::releaseDownloadsOf(NetworkIdentifier const& source) {
    for (auto it = mDownloads.begin(); it != mDownloads.end();) {
        if (it->second.target && *it->second.target == source) {
            it->second.sender.close();
            it = mDownloads.erase(it);
        } else {
            ++it;
        }
    }
}

void SyncService::releaseUploadsOf(NetworkIdentifier const& source) {
    for (auto it = mReceiverOwners.begin(); it != mReceiverOwners.end();) {
        if (it->second == source) {
            mReceiverActors.erase(it->first);
            mReceivers.erase(it->first);
            it = mReceiverOwners.erase(it);
        } else {
            ++it;
        }
    }
}

void SyncService::pumpDownloads() {
    if (mDownloads.empty()) {
        return;
    }
    // Per tick (~20/s) send budget: the configured per-user bandwidth, with a
    // floor of one chunk so a slow budget still progresses (§4.8).
    std::size_t const perTick = std::max<std::size_t>(
        kMaxFileChunkBytes,
        static_cast<std::size_t>((static_cast<std::uint64_t>(mConfig.limits.userBandwidthMiBps) * 1024ull * 1024ull) / 20)
    );
    std::int64_t const now = nowUnixMs();
    std::int64_t const idleLimitMs = static_cast<std::int64_t>(mConfig.transfer.idleTimeoutSeconds) * 1000;

    for (auto it = mDownloads.begin(); it != mDownloads.end();) {
        auto& session = it->second;
        if (!session.target) {
            it = mDownloads.erase(it);
            continue;
        }
        if (now - session.lastActivityMs > idleLimitMs) {
            session.sender.close();
            it = mDownloads.erase(it);
            continue;
        }
        if (!session.completeRequested) {
            auto const chunks = session.sender.poll(perTick);
            for (auto const& chunk : chunks) {
                std::vector<std::uint8_t> out;
                if (!encodeFileChunk(session.transferId.bytes, chunk.offset, chunk.data, out)) {
                    continue;
                }
                sendFrame(*session.target, Opcode::FileChunk, 0, std::move(out));
            }
            if (session.sender.allSent() && session.sender.allAcked()) {
                control::FileComplete complete;
                complete.transferId = session.transferId;
                complete.sha256 = session.hash;
                complete.status = "request";
                sendFrame(*session.target, Opcode::FileComplete, 0, control::encode(complete));
                session.completeRequested = true;
                session.lastActivityMs = now;
            }
        }
        ++it;
    }
}

void SyncService::expireLeases() {
    std::int64_t const now = nowUnixMs();
    std::erase_if(mLeases, [&](auto const& entry) { return entry.second.expiresAtMs <= now; });
}

Uuid SyncService::serverId() const {
    std::lock_guard lock{mMutex};
    return mServerId;
}

Uuid SyncService::worldId() const {
    std::lock_guard lock{mMutex};
    return mWorldId;
}

std::size_t SyncService::sessionCount() const {
    std::lock_guard lock{mMutex};
    return mSessions.size();
}

std::string SyncService::adminStatusText() {
    std::lock_guard lock{mMutex};
    std::uint64_t const sequence = mRepository ? mRepository->currentSequence() : 0;
    std::uint64_t const placements = mRepository ? mRepository->totalPlacementCount() : 0;
    std::uint64_t const files = mRepository ? mRepository->fileCount() : 0;
    std::size_t const downloads = mDownloads.size();
    std::size_t const sessions = mSessions.size();
    return "LHoloSyncServer sessions=" + std::to_string(sessions) + " sequence=" + std::to_string(sequence)
        + " placements=" + std::to_string(placements) + " files=" + std::to_string(files)
        + " transfers=" + std::to_string(downloads) + " running=" + (mRunning ? "1" : "0");
}

bool SyncService::adminDelete(Uuid const& placementId, std::string& message) {
    std::lock_guard lock{mMutex};
    if (!mRepository) {
        message = "storage unavailable";
        return false;
    }
    auto const current = mRepository->getPlacement(placementId);
    if (!current || current->deleted) {
        message = "placement not found";
        return false;
    }
    SharedPlacementDto out;
    PlacementRepository::Mutation mutation{};
    std::string error;
    if (!mRepository->deletePlacement(
            placementId, current->revision, "admin", true, nowUnixMs(), out, mutation, error
        )) {
        message = error;
        return false;
    }
    if (mutation != PlacementRepository::Mutation::Ok) {
        message = "delete rejected";
        return false;
    }
    mLeases.erase(placementId.toString());
    broadcastCatalogEvent("delete", out);
    message = "deleted " + placementId.toString();
    return true;
}

bool SyncService::adminUnlock(Uuid const& placementId, std::string& message) {
    std::lock_guard lock{mMutex};
    auto const removed = mLeases.erase(placementId.toString());
    message = removed ? "unlocked" : "no active lock";
    return true;
}

bool SyncService::adminSetRole(std::string const& playerId, std::string const& roleText, std::string& message) {
    std::lock_guard lock{mMutex};
    if (!mRepository) {
        message = "storage unavailable";
        return false;
    }
    Role role;
    if (!parseRole(roleText, role)) {
        message = "role must be viewer|member|admin";
        return false;
    }
    // Ownership keys are the stable "xuid:<id>" form; accept a bare XUID too.
    std::string key = playerId;
    if (key.rfind("xuid:", 0) != 0) {
        key = "xuid:" + key;
    }
    std::string error;
    if (!mRepository->setRole(key, role, nowUnixMs(), error)) {
        message = error;
        return false;
    }
    message = "role set";
    return true;
}

bool SyncService::adminGc(std::string const& mode, std::string& message) {
    std::lock_guard lock{mMutex};
    if (!mRepository || !mFileStore) {
        message = "storage unavailable";
        return false;
    }
    constexpr std::int64_t kGraceMs = 7ll * 24 * 60 * 60 * 1000;
    std::int64_t const cutoff = nowUnixMs() - kGraceMs;
    auto const candidates = mRepository->gcCandidates(cutoff);

    if (mode == "dry-run") {
        std::uint64_t bytes = 0;
        for (auto const& candidate : candidates) {
            bytes += candidate.sizeBytes;
        }
        message = "gc dry-run: " + std::to_string(candidates.size()) + " object(s), " + std::to_string(bytes)
            + " bytes eligible";
        return true;
    }
    if (mode == "confirm") {
        std::uint64_t removed = 0;
        for (auto const& candidate : candidates) {
            // Never delete a referenced object: gcCandidates already excludes
            // anything referenced by a placement row (including tombstones).
            static_cast<void>(mFileStore->removeObject(candidate.sha256, candidate.format));
            std::string error;
            if (mRepository->removeFile(candidate.sha256, error)) {
                ++removed;
            }
        }
        message = "gc confirm: removed " + std::to_string(removed) + " object(s)";
        return true;
    }
    message = "usage: gc dry-run|confirm";
    return false;
}

void SyncService::applyConfig(ServerConfig const& config) {
    std::lock_guard lock{mMutex};
    mConfig = config;
    mMaxFileBytes = config.limits.maxFileMiB * 1024ull * 1024ull;
    if (mMaxFileBytes == 0) {
        mMaxFileBytes = kDefaultMaxFileBytes;
    }
}

} // namespace lholo::sync
