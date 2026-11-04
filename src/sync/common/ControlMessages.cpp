#include "ControlMessages.h"

#include <nlohmann/json.hpp>

#include <set>
#include <stdexcept>

#include "Validation.h"

namespace lholo::sync::control {
namespace {

using nlohmann::json;

[[nodiscard]] Status invalid(std::string message) {
    return Status::failure(ErrorCode::InvalidPayload, std::move(message));
}

// SAX detector: rejects duplicate object keys, excessive nesting and oversized
// arrays without relying on parser-callback depth semantics or trusting the
// (often SIZE_MAX) element-count hint. Also bounds string lengths so a single
// control payload cannot allocate unbounded memory (§4.3).
struct StrictJsonPolicy : nlohmann::json_sax<json> {
    struct Level {
        std::set<std::string> keys;
        std::size_t count{0};
    };

    std::vector<Level> stack;
    std::string error;
    bool failed{false};

    bool fail(std::string message) {
        if (!failed) {
            failed = true;
            error = std::move(message);
        }
        return false;
    }

    bool countValue() {
        if (!stack.empty() && ++stack.back().count > kMaxJsonArrayItems) {
            return fail("json container holds too many items");
        }
        return true;
    }

    bool null() override { return countValue(); }
    bool boolean(bool) override { return countValue(); }
    bool number_integer(number_integer_t) override { return countValue(); }
    bool number_unsigned(number_unsigned_t) override { return countValue(); }
    bool number_float(number_float_t, std::string const&) override { return countValue(); }
    bool binary(json::binary_t&) override { return countValue(); }

    bool string(std::string& value) override {
        if (value.size() > kMaxJsonStringBytes) {
            return fail("json string too long");
        }
        return countValue();
    }

    bool start_object(std::size_t) override {
        if (!countValue()) {
            return false;
        }
        if (stack.size() >= kMaxJsonDepth) {
            return fail("json nesting too deep");
        }
        stack.emplace_back();
        return true;
    }

    bool key(std::string& value) override {
        if (value.size() > kMaxJsonStringBytes) {
            return fail("json key too long");
        }
        if (!stack.empty() && !stack.back().keys.insert(value).second) {
            return fail("duplicate json key: " + value);
        }
        return true;
    }

    bool end_object() override {
        if (!stack.empty()) {
            stack.pop_back();
        }
        return true;
    }

    bool start_array(std::size_t) override {
        if (!countValue()) {
            return false;
        }
        if (stack.size() >= kMaxJsonDepth) {
            return fail("json nesting too deep");
        }
        stack.emplace_back();
        return true;
    }

    bool end_array() override {
        if (!stack.empty()) {
            stack.pop_back();
        }
        return true;
    }

    bool parse_error(std::size_t, std::string const&, nlohmann::detail::exception const& exception) override {
        return fail(std::string{"json parse error: "} + exception.what());
    }
};

// Strict parse: budget, UTF-8, duplicate keys, depth, then a bounds walk.
bool parseStrict(std::span<std::uint8_t const> payload, json& out, std::string& error) {
    if (payload.empty()) {
        error = "empty control payload";
        return false;
    }
    if (payload.size() > kMaxControlJsonBytes) {
        error = "control payload exceeds budget";
        return false;
    }
    std::string text(reinterpret_cast<char const*>(payload.data()), payload.size());
    if (!validation::isValidUtf8(text)) {
        error = "control payload is not valid UTF-8";
        return false;
    }

    StrictJsonPolicy policy;
    if (!json::sax_parse(text, &policy) || policy.failed) {
        error = policy.failed ? policy.error : std::string{"json parse error"};
        return false;
    }

    out = json::parse(text, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        error = "control payload must be a JSON object";
        return false;
    }
    return true;
}

// Recursive bounds check for strings and arrays.
bool walkLimits(json const& value, std::size_t depth, std::string& error) {
    if (depth > kMaxJsonDepth) {
        error = "json nesting too deep";
        return false;
    }
    if (value.is_string()) {
        if (value.get_ref<std::string const&>().size() > kMaxJsonStringBytes) {
            error = "json string too long";
            return false;
        }
        return true;
    }
    if (value.is_array()) {
        if (value.size() > kMaxJsonArrayItems) {
            error = "json array too long";
            return false;
        }
        for (auto const& element : value) {
            if (!walkLimits(element, depth + 1, error)) {
                return false;
            }
        }
        return true;
    }
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!walkLimits(it.value(), depth + 1, error)) {
                return false;
            }
        }
    }
    return true;
}

// --- field extraction helpers ---------------------------------------------

bool requireString(json const& object, char const* key, std::string& out, Status& status) {
    auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        status = invalid(std::string("missing or non-string field: ") + key);
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool optionalString(json const& object, char const* key, std::optional<std::string>& out, Status& status) {
    auto it = object.find(key);
    if (it == object.end()) {
        out.reset();
        return true;
    }
    if (it->is_null()) {
        out.reset();
        return true;
    }
    if (!it->is_string()) {
        status = invalid(std::string("non-string optional field: ") + key);
        return false;
    }
    out = it->get<std::string>();
    return true;
}

bool requireInt64(json const& object, char const* key, std::int64_t& out, Status& status) {
    auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        status = invalid(std::string("missing or non-integer field: ") + key);
        return false;
    }
    out = it->get<std::int64_t>();
    return true;
}

bool requireUint64String(json const& object, char const* key, std::uint64_t& out, Status& status) {
    std::string text;
    if (!requireString(object, key, text, status)) {
        return false;
    }
    if (!validation::parseUint64String(text, out)) {
        status = invalid(std::string("invalid unsigned decimal string: ") + key);
        return false;
    }
    return true;
}

bool requireBool(json const& object, char const* key, bool& out, Status& status) {
    auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        status = invalid(std::string("missing or non-boolean field: ") + key);
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool requireUuid(json const& object, char const* key, Uuid& out, Status& status) {
    std::string text;
    if (!requireString(object, key, text, status)) {
        return false;
    }
    if (!Uuid::parse(text, out)) {
        status = invalid(std::string("invalid uuid field: ") + key);
        return false;
    }
    return true;
}

bool requireSha256(json const& object, char const* key, Sha256Hash& out, Status& status) {
    std::string text;
    if (!requireString(object, key, text, status)) {
        return false;
    }
    if (!Sha256Hash::parse(text, out)) {
        status = invalid(std::string("invalid sha256 field: ") + key);
        return false;
    }
    return true;
}

bool requireFormat(json const& object, char const* key, SchematicFormat& out, Status& status) {
    std::string text;
    if (!requireString(object, key, text, status)) {
        return false;
    }
    if (!parseSchematicFormat(text, out)) {
        status = Status::failure(ErrorCode::UnsupportedFormat, "unsupported schematic format");
        return false;
    }
    return true;
}

bool requireOrigin(json const& object, char const* key, std::int64_t& x, std::int64_t& y, std::int64_t& z, Status& status) {
    auto it = object.find(key);
    if (it == object.end() || !it->is_array() || it->size() != 3) {
        status = invalid(std::string("origin must be a 3-element array: ") + key);
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (!(*it)[i].is_number_integer()) {
            status = invalid("origin components must be integers");
            return false;
        }
    }
    x = (*it)[0].get<std::int64_t>();
    y = (*it)[1].get<std::int64_t>();
    z = (*it)[2].get<std::int64_t>();
    return true;
}

bool isValidDimension(std::int32_t dimension) {
    return dimension == kDimensionOverworld || dimension == kDimensionNether || dimension == kDimensionEnd;
}

[[nodiscard]] bool inOriginRange(std::int64_t x, std::int64_t y, std::int64_t z) {
    return x >= -kMaxOriginXZ && x <= kMaxOriginXZ
        && z >= -kMaxOriginXZ && z <= kMaxOriginXZ
        && y >= -kMaxOriginY && y <= kMaxOriginY;
}

json placementToJson(SharedPlacementDto const& dto) {
    json origin = json::array({dto.originX, dto.originY, dto.originZ});
    json object = json::object();
    object["placement_id"] = dto.placementId.toString();
    object["display_name"] = dto.displayName;
    object["file_sha256"] = dto.fileHash.toHex();
    object["file_format"] = schematicFormatName(dto.format);
    object["dimension"] = dto.dimension;
    object["origin"] = std::move(origin);
    object["rotation"] = dto.rotation;
    object["mirror"] = dto.mirror;
    object["revision"] = validation::toString(dto.revision);
    object["owner_id"] = dto.ownerId;
    object["updated_at_unix_ms"] = validation::toString(dto.updatedAtUnixMs);
    object["deleted"] = dto.deleted;
    return object;
}

Status placementFromJson(json const& object, SharedPlacementDto& out) {
    Status status;
    std::string idText;
    if (!requireString(object, "placement_id", idText, status)) return status;
    if (!Uuid::parse(idText, out.placementId)) return invalid("invalid placement_id");
    if (!requireString(object, "display_name", out.displayName, status)) return status;
    if (!requireSha256(object, "file_sha256", out.fileHash, status)) return status;
    if (!requireFormat(object, "file_format", out.format, status)) return status;
    std::int64_t dimension = 0;
    if (!requireInt64(object, "dimension", dimension, status)) return status;
    if (!isValidDimension(static_cast<std::int32_t>(dimension))) return invalid("dimension out of range");
    out.dimension = static_cast<std::int32_t>(dimension);
    if (!requireOrigin(object, "origin", out.originX, out.originY, out.originZ, status)) return status;
    if (!inOriginRange(out.originX, out.originY, out.originZ)) return invalid("origin out of range");
    std::int64_t rotation = 0;
    std::int64_t mirror = 0;
    if (!requireInt64(object, "rotation", rotation, status)) return status;
    if (!requireInt64(object, "mirror", mirror, status)) return status;
    if (rotation < 0 || rotation > 3) return invalid("rotation out of range");
    if (mirror < 0 || mirror > 2) return invalid("mirror out of range");
    out.rotation = static_cast<std::int32_t>(rotation);
    out.mirror = static_cast<std::int32_t>(mirror);
    if (!requireUint64String(object, "revision", out.revision, status)) return status;
    if (out.revision < 1) return invalid("revision must be >= 1");
    if (!requireString(object, "owner_id", out.ownerId, status)) return status;
    std::uint64_t updatedAt = 0;
    if (!requireUint64String(object, "updated_at_unix_ms", updatedAt, status)) return status;
    out.updatedAtUnixMs = static_cast<std::int64_t>(updatedAt);
    if (!requireBool(object, "deleted", out.deleted, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> toBytes(json const& object) {
    std::string text = object.dump();
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

Status parseObject(std::span<std::uint8_t const> payload, json& out) {
    std::string error;
    if (!parseStrict(payload, out, error)) {
        return invalid(std::move(error));
    }
    if (!walkLimits(out, 0, error)) {
        return invalid(std::move(error));
    }
    return Status::success();
}

} // namespace

std::vector<std::uint8_t> encode(Hello const& value) {
    json object = json::object();
    object["client_version"] = value.clientVersion;
    object["protocol_major"] = value.protocolMajor;
    object["protocol_minor"] = value.protocolMinor;
    object["features"] = value.features;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, Hello& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireString(object, "client_version", out.clientVersion, status)) return status;
    std::int64_t major = 0;
    std::int64_t minor = 0;
    if (!requireInt64(object, "protocol_major", major, status)) return status;
    if (!requireInt64(object, "protocol_minor", minor, status)) return status;
    if (major < 0 || major > 0xffff || minor < 0 || minor > 0xffff) return invalid("protocol version out of range");
    out.protocolMajor = static_cast<std::uint16_t>(major);
    out.protocolMinor = static_cast<std::uint16_t>(minor);
    auto features = object.find("features");
    if (features == object.end() || !features->is_array()) return invalid("features must be an array");
    out.features.clear();
    for (auto const& feature : *features) {
        if (!feature.is_string()) return invalid("feature must be a string");
        out.features.push_back(feature.get<std::string>());
    }
    return Status::success();
}

std::vector<std::uint8_t> encode(HelloAck const& value) {
    json object = json::object();
    object["server_version"] = value.serverVersion;
    object["server_id"] = value.serverId.toString();
    object["world_id"] = value.worldId.toString();
    object["session_id"] = value.sessionId.toString();
    object["features"] = value.features;
    object["role"] = roleName(value.role);
    object["max_file_bytes"] = validation::toString(value.maxFileBytes);
    object["max_active_transfers"] = value.maxActiveTransfers;
    object["catalog_sequence"] = validation::toString(value.catalogSequence);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, HelloAck& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireString(object, "server_version", out.serverVersion, status)) return status;
    if (!requireUuid(object, "server_id", out.serverId, status)) return status;
    if (!requireUuid(object, "world_id", out.worldId, status)) return status;
    if (!requireUuid(object, "session_id", out.sessionId, status)) return status;
    auto features = object.find("features");
    if (features == object.end() || !features->is_array()) return invalid("features must be an array");
    out.features.clear();
    for (auto const& feature : *features) {
        if (!feature.is_string()) return invalid("feature must be a string");
        out.features.push_back(feature.get<std::string>());
    }
    std::string role;
    if (!requireString(object, "role", role, status)) return status;
    if (!parseRole(role, out.role)) return invalid("invalid role");
    if (!requireUint64String(object, "max_file_bytes", out.maxFileBytes, status)) return status;
    std::int64_t maxActive = 0;
    if (!requireInt64(object, "max_active_transfers", maxActive, status)) return status;
    if (maxActive < 1 || maxActive > 100) return invalid("max_active_transfers out of range");
    out.maxActiveTransfers = static_cast<std::uint32_t>(maxActive);
    if (!requireUint64String(object, "catalog_sequence", out.catalogSequence, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(ListRequest const& value) {
    json object = json::object();
    object["cursor"] = value.cursor.has_value() ? json(*value.cursor) : json(nullptr);
    object["page_size"] = value.pageSize;
    object["known_sequence"] = validation::toString(value.knownSequence);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, ListRequest& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!optionalString(object, "cursor", out.cursor, status)) return status;
    std::int64_t pageSize = 0;
    if (!requireInt64(object, "page_size", pageSize, status)) return status;
    if (pageSize < 1 || pageSize > static_cast<std::int64_t>(kMaxCatalogPageSize)) return invalid("page_size out of range");
    out.pageSize = static_cast<std::uint32_t>(pageSize);
    if (!requireUint64String(object, "known_sequence", out.knownSequence, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(ListPage const& value) {
    json items = json::array();
    for (auto const& item : value.items) {
        items.push_back(placementToJson(item));
    }
    json object = json::object();
    object["items"] = std::move(items);
    object["next_cursor"] = value.nextCursor.has_value() ? json(*value.nextCursor) : json(nullptr);
    object["snapshot_sequence"] = validation::toString(value.snapshotSequence);
    object["has_more"] = value.hasMore;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, ListPage& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    auto items = object.find("items");
    if (items == object.end() || !items->is_array()) return invalid("items must be an array");
    if (items->size() > kMaxCatalogPageSize) return invalid("too many items in page");
    out.items.clear();
    for (auto const& element : *items) {
        if (!element.is_object()) return invalid("page item must be an object");
        SharedPlacementDto dto;
        if (Status status = placementFromJson(element, dto); !status.ok) return status;
        out.items.push_back(std::move(dto));
    }
    Status status;
    if (!optionalString(object, "next_cursor", out.nextCursor, status)) return status;
    if (!requireUint64String(object, "snapshot_sequence", out.snapshotSequence, status)) return status;
    if (!requireBool(object, "has_more", out.hasMore, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(CatalogDelta const& value) {
    json object = json::object();
    object["sequence"] = validation::toString(value.sequence);
    object["action"] = value.action;
    object["placement"] = placementToJson(value.placement);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, CatalogDelta& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUint64String(object, "sequence", out.sequence, status)) return status;
    if (!requireString(object, "action", out.action, status)) return status;
    if (out.action != "upsert" && out.action != "delete") return invalid("invalid catalog action");
    auto placement = object.find("placement");
    if (placement == object.end() || !placement->is_object()) return invalid("placement must be an object");
    return placementFromJson(*placement, out.placement);
}

std::vector<std::uint8_t> encodePlacement(SharedPlacementDto const& dto) {
    return toBytes(placementToJson(dto));
}

Status decodePlacement(std::span<std::uint8_t const> payload, SharedPlacementDto& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    return placementFromJson(object, out);
}

std::vector<std::uint8_t> encode(FileOffer const& value) {
    json object = json::object();
    object["client_operation_id"] = value.clientOperationId.toString();
    object["sha256"] = value.sha256.toHex();
    object["format"] = schematicFormatName(value.format);
    object["size_bytes"] = validation::toString(value.sizeBytes);
    object["display_filename"] = value.displayFilename;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, FileOffer& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "client_operation_id", out.clientOperationId, status)) return status;
    if (!requireSha256(object, "sha256", out.sha256, status)) return status;
    if (!requireFormat(object, "format", out.format, status)) return status;
    if (!requireUint64String(object, "size_bytes", out.sizeBytes, status)) return status;
    if (out.sizeBytes == 0) return invalid("size_bytes must be > 0");
    if (!requireString(object, "display_filename", out.displayFilename, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(FileGet const& value) {
    json object = json::object();
    object["placement_id"] = value.placementId.toString();
    object["sha256"] = value.sha256.toHex();
    object["resume_offset"] = validation::toString(value.resumeOffset);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, FileGet& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "placement_id", out.placementId, status)) return status;
    if (!requireSha256(object, "sha256", out.sha256, status)) return status;
    if (!requireUint64String(object, "resume_offset", out.resumeOffset, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(FileAccept const& value) {
    json object = json::object();
    object["status"] = value.status;
    object["transfer_id"] = value.transferId.toString();
    object["need_bytes"] = value.needBytes;
    object["size_bytes"] = validation::toString(value.sizeBytes);
    object["resume_offset"] = validation::toString(value.resumeOffset);
    object["chunk_bytes"] = value.chunkBytes;
    object["window"] = value.window;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, FileAccept& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireString(object, "status", out.status, status)) return status;
    if (out.status != "offer" && out.status != "ready" && out.status != "skip") return invalid("invalid file_accept status");
    if (!requireUuid(object, "transfer_id", out.transferId, status)) return status;
    if (!requireBool(object, "need_bytes", out.needBytes, status)) return status;
    if (!requireUint64String(object, "size_bytes", out.sizeBytes, status)) return status;
    if (!requireUint64String(object, "resume_offset", out.resumeOffset, status)) return status;
    std::int64_t chunk = 0;
    if (!requireInt64(object, "chunk_bytes", chunk, status)) return status;
    if (chunk <= 0 || chunk > static_cast<std::int64_t>(kMaxFileChunkBytes)) return invalid("chunk_bytes out of range");
    out.chunkBytes = static_cast<std::uint32_t>(chunk);
    std::int64_t window = 0;
    if (!requireInt64(object, "window", window, status)) return status;
    if (window < 1 || window > 64) return invalid("window out of range");
    out.window = static_cast<std::uint32_t>(window);
    return Status::success();
}

std::vector<std::uint8_t> encode(FileAck const& value) {
    json object = json::object();
    object["transfer_id"] = value.transferId.toString();
    object["next_offset"] = validation::toString(value.nextOffset);
    object["received_bitmap"] = value.receivedBitmap;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, FileAck& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "transfer_id", out.transferId, status)) return status;
    if (!requireUint64String(object, "next_offset", out.nextOffset, status)) return status;
    std::int64_t bitmap = 0;
    if (!requireInt64(object, "received_bitmap", bitmap, status)) return status;
    if (bitmap < 0 || bitmap > 0xffffffffll) return invalid("received_bitmap out of range");
    out.receivedBitmap = static_cast<std::uint32_t>(bitmap);
    return Status::success();
}

std::vector<std::uint8_t> encode(FileComplete const& value) {
    json object = json::object();
    object["transfer_id"] = value.transferId.toString();
    object["sha256"] = value.sha256.toHex();
    object["status"] = value.status;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, FileComplete& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "transfer_id", out.transferId, status)) return status;
    if (!requireSha256(object, "sha256", out.sha256, status)) return status;
    if (!requireString(object, "status", out.status, status)) return status;
    if (out.status != "request" && out.status != "ok" && out.status != "failed") return invalid("invalid file_complete status");
    return Status::success();
}

std::vector<std::uint8_t> encode(PlacementCreate const& value) {
    json object = json::object();
    object["client_operation_id"] = value.clientOperationId.toString();
    object["display_name"] = value.displayName;
    object["file_sha256"] = value.fileSha256.toHex();
    object["file_format"] = schematicFormatName(value.fileFormat);
    object["dimension"] = value.dimension;
    object["origin"] = json::array({value.originX, value.originY, value.originZ});
    object["rotation"] = value.rotation;
    object["mirror"] = value.mirror;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, PlacementCreate& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "client_operation_id", out.clientOperationId, status)) return status;
    if (!requireString(object, "display_name", out.displayName, status)) return status;
    if (!validation::isValidDisplayName(out.displayName)) return invalid("display_name rejected");
    if (!requireSha256(object, "file_sha256", out.fileSha256, status)) return status;
    if (!requireFormat(object, "file_format", out.fileFormat, status)) return status;
    std::int64_t dimension = 0;
    if (!requireInt64(object, "dimension", dimension, status)) return status;
    if (!isValidDimension(static_cast<std::int32_t>(dimension))) return invalid("dimension out of range");
    out.dimension = static_cast<std::int32_t>(dimension);
    if (!requireOrigin(object, "origin", out.originX, out.originY, out.originZ, status)) return status;
    if (!inOriginRange(out.originX, out.originY, out.originZ)) return invalid("origin out of range");
    std::int64_t rotation = 0;
    std::int64_t mirror = 0;
    if (!requireInt64(object, "rotation", rotation, status)) return status;
    if (!requireInt64(object, "mirror", mirror, status)) return status;
    if (rotation < 0 || rotation > 3 || mirror < 0 || mirror > 2) return invalid("rotation/mirror out of range");
    out.rotation = static_cast<std::int32_t>(rotation);
    out.mirror = static_cast<std::int32_t>(mirror);
    return Status::success();
}

std::vector<std::uint8_t> encode(PlacementUpdate const& value) {
    json object = json::object();
    object["client_operation_id"] = value.clientOperationId.toString();
    object["placement_id"] = value.placementId.toString();
    object["base_revision"] = validation::toString(value.baseRevision);
    object["lock_id"] = value.lockId.toString();
    object["display_name"] = value.displayName;
    object["dimension"] = value.dimension;
    object["origin"] = json::array({value.originX, value.originY, value.originZ});
    object["rotation"] = value.rotation;
    object["mirror"] = value.mirror;
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, PlacementUpdate& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "client_operation_id", out.clientOperationId, status)) return status;
    if (!requireUuid(object, "placement_id", out.placementId, status)) return status;
    if (!requireUint64String(object, "base_revision", out.baseRevision, status)) return status;
    if (!requireUuid(object, "lock_id", out.lockId, status)) return status;
    if (!requireString(object, "display_name", out.displayName, status)) return status;
    if (!validation::isValidDisplayName(out.displayName)) return invalid("display_name rejected");
    std::int64_t dimension = 0;
    if (!requireInt64(object, "dimension", dimension, status)) return status;
    if (!isValidDimension(static_cast<std::int32_t>(dimension))) return invalid("dimension out of range");
    out.dimension = static_cast<std::int32_t>(dimension);
    if (!requireOrigin(object, "origin", out.originX, out.originY, out.originZ, status)) return status;
    if (!inOriginRange(out.originX, out.originY, out.originZ)) return invalid("origin out of range");
    std::int64_t rotation = 0;
    std::int64_t mirror = 0;
    if (!requireInt64(object, "rotation", rotation, status)) return status;
    if (!requireInt64(object, "mirror", mirror, status)) return status;
    if (rotation < 0 || rotation > 3 || mirror < 0 || mirror > 2) return invalid("rotation/mirror out of range");
    out.rotation = static_cast<std::int32_t>(rotation);
    out.mirror = static_cast<std::int32_t>(mirror);
    return Status::success();
}

std::vector<std::uint8_t> encode(PlacementDelete const& value) {
    json object = json::object();
    object["client_operation_id"] = value.clientOperationId.toString();
    object["placement_id"] = value.placementId.toString();
    object["base_revision"] = validation::toString(value.baseRevision);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, PlacementDelete& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "client_operation_id", out.clientOperationId, status)) return status;
    if (!requireUuid(object, "placement_id", out.placementId, status)) return status;
    if (!requireUint64String(object, "base_revision", out.baseRevision, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(EditBegin const& value) {
    json object = json::object();
    object["placement_id"] = value.placementId.toString();
    object["base_revision"] = validation::toString(value.baseRevision);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, EditBegin& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "placement_id", out.placementId, status)) return status;
    if (!requireUint64String(object, "base_revision", out.baseRevision, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(EditGranted const& value) {
    json object = json::object();
    object["placement_id"] = value.placementId.toString();
    object["base_revision"] = validation::toString(value.baseRevision);
    object["lock_id"] = value.lockId.toString();
    object["expires_at_unix_ms"] = validation::toString(value.expiresAtUnixMs);
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, EditGranted& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "placement_id", out.placementId, status)) return status;
    if (!requireUint64String(object, "base_revision", out.baseRevision, status)) return status;
    if (!requireUuid(object, "lock_id", out.lockId, status)) return status;
    std::string expires;
    if (!requireString(object, "expires_at_unix_ms", expires, status)) return status;
    if (!validation::parseInt64String(expires, out.expiresAtUnixMs)) return invalid("invalid expires_at_unix_ms");
    return Status::success();
}

std::vector<std::uint8_t> encode(EditLease const& value) {
    json object = json::object();
    object["placement_id"] = value.placementId.toString();
    object["lock_id"] = value.lockId.toString();
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, EditLease& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireUuid(object, "placement_id", out.placementId, status)) return status;
    if (!requireUuid(object, "lock_id", out.lockId, status)) return status;
    return Status::success();
}

std::vector<std::uint8_t> encode(Result const& value) {
    json object = json::object();
    object["status"] = value.status.empty() ? "ok" : value.status;
    if (value.placementId.has_value()) object["placement_id"] = value.placementId->toString();
    if (value.revision.has_value()) object["revision"] = validation::toString(*value.revision);
    if (value.catalogSequence.has_value()) object["catalog_sequence"] = validation::toString(*value.catalogSequence);
    if (value.transferId.has_value()) object["transfer_id"] = value.transferId->toString();
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, Result& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    if (!requireString(object, "status", out.status, status)) return status;
    auto it = object.find("placement_id");
    if (it != object.end()) {
        if (!it->is_string()) return invalid("placement_id must be a string");
        Uuid id;
        if (!Uuid::parse(it->get<std::string>(), id)) return invalid("invalid placement_id");
        out.placementId = id;
    }
    it = object.find("revision");
    if (it != object.end()) {
        if (!it->is_string()) return invalid("revision must be a decimal string");
        std::uint64_t revision = 0;
        if (!validation::parseUint64String(it->get<std::string>(), revision)) return invalid("invalid revision");
        out.revision = revision;
    }
    it = object.find("catalog_sequence");
    if (it != object.end()) {
        if (!it->is_string()) return invalid("catalog_sequence must be a decimal string");
        std::uint64_t sequence = 0;
        if (!validation::parseUint64String(it->get<std::string>(), sequence)) return invalid("invalid catalog_sequence");
        out.catalogSequence = sequence;
    }
    it = object.find("transfer_id");
    if (it != object.end()) {
        if (!it->is_string()) return invalid("transfer_id must be a string");
        Uuid id;
        if (!Uuid::parse(it->get<std::string>(), id)) return invalid("invalid transfer_id");
        out.transferId = id;
    }
    return Status::success();
}

std::vector<std::uint8_t> encode(ErrorMessage const& value) {
    json object = json::object();
    object["code"] = static_cast<std::uint16_t>(value.code);
    object["name"] = errorCodeName(value.code);
    object["message"] = value.message;
    object["retryable"] = value.retryable;
    if (value.retryAfterMs != 0) {
        object["retry_after_ms"] = value.retryAfterMs;
    }
    return toBytes(object);
}

Status decode(std::span<std::uint8_t const> payload, ErrorMessage& out) {
    json object;
    if (Status status = parseObject(payload, object); !status.ok) return status;
    Status status;
    std::int64_t code = 0;
    if (!requireInt64(object, "code", code, status)) return status;
    if (code < 1000 || code > 2000) return invalid("error code out of range");
    out.code = static_cast<ErrorCode>(code);
    std::string name;
    if (!requireString(object, "name", name, status)) return status;
    if (!requireString(object, "message", out.message, status)) return status;
    if (!requireBool(object, "retryable", out.retryable, status)) return status;
    auto it = object.find("retry_after_ms");
    if (it != object.end()) {
        if (!it->is_number_integer()) return invalid("retry_after_ms must be an integer");
        std::int64_t const value = it->get<std::int64_t>();
        if (value < 0 || value > 3600000) return invalid("retry_after_ms out of range");
        out.retryAfterMs = static_cast<std::uint32_t>(value);
    }
    return Status::success();
}

} // namespace lholo::sync::control
