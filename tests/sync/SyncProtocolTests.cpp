// LHolo Integrated Sync — shared protocol layer tests (spec §12.1).
//
// Covers frame codec exactness and hostile-input rejection (T01/T02), SHA-256
// vectors, UUID/hash parsing, file-chunk codec bounds (T14-adjacent) and control
// message round-trips. These run without Minecraft/LeviLamina runtime objects.

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "sync/common/ControlMessages.h"
#include "sync/common/FrameCodec.h"
#include "sync/common/Protocol.h"
#include "sync/common/Sha256.h"
#include "sync/common/SharedTypes.h"
#include "sync/common/Validation.h"

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, char const* expression, char const* file, int line) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("FAIL %s:%d: %s\n", file, line, expression);
    }
}

#define CHECK(...) check((__VA_ARGS__), #__VA_ARGS__, __FILE__, __LINE__)

using namespace lholo::sync;

std::vector<std::uint8_t> bytes(std::string const& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

std::vector<std::uint8_t> makeFrame(Opcode op, std::uint64_t requestId, std::vector<std::uint8_t> payload) {
    Frame frame;
    frame.opcode = static_cast<std::uint16_t>(op);
    frame.flags = 0;
    frame.requestId = requestId;
    frame.sessionId = std::array<std::uint8_t, kSessionIdBytes>{};
    frame.payload = std::move(payload);
    std::vector<std::uint8_t> out;
    CHECK(encodeFrame(frame, out));
    return out;
}

// T01 — exact encode/decode round trip with fixed little-endian layout.
void testFrameRoundTrip() {
    auto encoded = makeFrame(Opcode::ListRequest, 0x0102030405060708ull, bytes("hello"));
    CHECK(encoded.size() == kFrameHeaderSize + 5);
    CHECK(encoded[0] == 'L' && encoded[1] == 'H' && encoded[2] == 'S' && encoded[3] == '1');
    // little-endian major/minor/opcode/flags
    CHECK(encoded[4] == 1 && encoded[5] == 0);
    CHECK(encoded[6] == 0 && encoded[7] == 0);
    CHECK(encoded[8] == static_cast<std::uint8_t>(Opcode::ListRequest) && encoded[9] == 0);
    CHECK(encoded[10] == 0 && encoded[11] == 0);
    // request id LE
    CHECK(encoded[12] == 0x08 && encoded[19] == 0x01);
    // payload length LE at offset 36
    CHECK(encoded[36] == 5 && encoded[37] == 0 && encoded[38] == 0 && encoded[39] == 0);

    Frame decoded;
    std::string error;
    CHECK(decodeFrame(encoded, decoded, error) == DecodeResult::Ok);
    CHECK(decoded.opcode == static_cast<std::uint16_t>(Opcode::ListRequest));
    CHECK(decoded.requestId == 0x0102030405060708ull);
    CHECK(decoded.sessionId == std::array<std::uint8_t, kSessionIdBytes>{});
    CHECK(decoded.payload == bytes("hello"));
}

// T02 — hostile frames are rejected without allocating unbounded memory.
void testFrameRejection() {
    Frame decoded;
    std::string error;

    // Too short.
    CHECK(decodeFrame(std::vector<std::uint8_t>(10, 0), decoded, error) == DecodeResult::Invalid);

    // Bad magic.
    auto badMagic = makeFrame(Opcode::Hello, 0, {});
    badMagic[0] = 'X';
    CHECK(decodeFrame(badMagic, decoded, error) == DecodeResult::Invalid);

    // Unknown opcode.
    auto unknown = makeFrame(Opcode::Hello, 0, {});
    unknown[8] = 0xFF;
    unknown[9] = 0xFF;
    CHECK(decodeFrame(unknown, decoded, error) == DecodeResult::Invalid);

    // Non-zero flags (V1 requires 0).
    auto flags = makeFrame(Opcode::Hello, 0, {});
    flags[10] = 1;
    CHECK(decodeFrame(flags, decoded, error) == DecodeResult::Invalid);

    // Payload length mismatch / trailing bytes.
    auto trailing = makeFrame(Opcode::Hello, 0, bytes("abc"));
    trailing.push_back(0);
    CHECK(decodeFrame(trailing, decoded, error) == DecodeResult::Invalid);

    // Declared length exceeds the frame's actual bytes.
    auto shortPayload = makeFrame(Opcode::Hello, 0, bytes("abc"));
    shortPayload.resize(shortPayload.size() - 1);
    CHECK(decodeFrame(shortPayload, decoded, error) == DecodeResult::Invalid);

    // Incompatible major version is distinguished from invalid input.
    auto major = makeFrame(Opcode::Hello, 0, {});
    major[4] = 2;
    CHECK(decodeFrame(major, decoded, error) == DecodeResult::IncompatibleVersion);

    // Frame bigger than the protocol ceiling is rejected before decoding.
    std::vector<std::uint8_t> huge(kMaxFrameBytes + 1, 0);
    huge[0] = 'L'; huge[1] = 'H'; huge[2] = 'S'; huge[3] = '1';
    CHECK(decodeFrame(huge, decoded, error) == DecodeResult::Invalid);

    // Oversized payload cannot be encoded.
    Frame tooBig;
    tooBig.opcode = static_cast<std::uint16_t>(Opcode::Hello);
    tooBig.payload.resize(kMaxPayloadBytes + 1);
    std::vector<std::uint8_t> out;
    CHECK(!encodeFrame(tooBig, out));
}

// SHA-256 known-answer tests.
void testSha256() {
    CHECK(sha256::hashString("").toHex()
          == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256::hashString("abc").toHex()
          == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256::hashString("The quick brown fox jumps over the lazy dog").toHex()
          == "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");

    // Streaming across block boundaries must match the one-shot result.
    std::string longText(1000, 'x');
    auto oneShot = sha256::hash(longText.data(), longText.size());
    sha256::Hasher hasher;
    for (std::size_t i = 0; i < longText.size(); i += 7) {
        std::size_t const span = (i + 7 <= longText.size()) ? 7 : (longText.size() - i);
        hasher.update(longText.data() + i, span);
    }
    CHECK(hasher.finish() == oneShot);
}

void testUuidAndHashParsing() {
    Uuid id;
    CHECK(Uuid::parse("c4391dbd-0cd1-4b7b-8ee7-61ff2f7f4b6b", id));
    CHECK(id.toString() == "c4391dbd-0cd1-4b7b-8ee7-61ff2f7f4b6b");
    CHECK(!id.isNil());
    CHECK(!Uuid::parse("c4391dbd0cd14b7b8ee761ff2f7f4b6b", id)); // no hyphens -> reject
    CHECK(Uuid::parse("c4391dbd-0cd1-4b7b-8ee7-61ff2f7f4b6g", id) == false); // bad hex
    CHECK(Uuid::parse("", id) == false);
    CHECK(Uuid{}.isNil());

    Uuid generated = Uuid::random();
    CHECK(!generated.isNil());
    CHECK(generated.bytes[6] >> 4 == 4); // version 4

    Sha256Hash hash;
    CHECK(Sha256Hash::parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", hash));
    CHECK(hash.toHex() == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    CHECK(Sha256Hash::parse("0123", hash) == false);
    CHECK(Sha256Hash::parse("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg", hash) == false);
    CHECK(Sha256Hash{}.isZero());
}

void testFileChunkCodec() {
    std::array<std::uint8_t, kUuidBytes> transfer{};
    for (std::size_t i = 0; i < transfer.size(); ++i) {
        transfer[i] = static_cast<std::uint8_t>(i);
    }
    std::vector<std::uint8_t> data(kMaxFileChunkBytes, 0xAB);

    std::vector<std::uint8_t> payload;
    CHECK(encodeFileChunk(transfer, 1024, data, payload));
    CHECK(payload.size() == kFileChunkPrefixBytes + kMaxFileChunkBytes);

    FileChunkPayload chunk;
    std::string error;
    CHECK(decodeFileChunk(payload, chunk, error));
    CHECK(chunk.transferId == transfer);
    CHECK(chunk.offset == 1024);
    CHECK(chunk.length == kMaxFileChunkBytes);
    CHECK(chunk.data.size() == kMaxFileChunkBytes);

    // Zero-length and oversized chunks are rejected.
    CHECK(!encodeFileChunk(transfer, 0, {}, payload));
    std::vector<std::uint8_t> tooBig(kMaxFileChunkBytes + 1, 0);
    CHECK(!encodeFileChunk(transfer, 0, tooBig, payload));

    // Truncated payload is rejected.
    std::vector<std::uint8_t> truncated(payload.begin(), payload.end() - 1);
    CHECK(!decodeFileChunk(truncated, chunk, error));
}

void testValidationPrimitives() {
    using namespace lholo::sync::validation;

    CHECK(isValidUtf8("hello"));
    CHECK(isValidUtf8("自動倉庫"));
    CHECK(!isValidUtf8(std::string("\xC3\x28", 2)));              // bad continuation
    CHECK(!isValidUtf8(std::string("\xE2\x80", 2)));             // truncated 3-byte
    CHECK(!isValidUtf8(std::string("\xED\xA0\x80", 3)));         // surrogate

    CHECK(isValidDisplayName("自動倉庫"));
    CHECK(!isValidDisplayName(""));
    CHECK(!isValidDisplayName("a/b"));
    CHECK(!isValidDisplayName("name."));
    CHECK(!isValidDisplayName(std::string("bell\x07", 5)));
    CHECK(!isValidDisplayName(std::string("\xE2\x80\xAE" "evil", 6))); // bidi RLO
    CHECK(!isValidDisplayName(std::string(200, 'a')));           // > 128 bytes

    std::int64_t signed64 = 0;
    CHECK(parseInt64String("15", signed64) && signed64 == 15);
    CHECK(parseInt64String("-9223372036854775808", signed64)
          && signed64 == std::numeric_limits<std::int64_t>::min());
    CHECK(!parseInt64String("9223372036854775808", signed64)); // overflow
    CHECK(!parseInt64String("+1", signed64));
    CHECK(!parseInt64String("1x", signed64));
    CHECK(!parseInt64String("", signed64));

    std::uint64_t unsigned64 = 0;
    CHECK(parseUint64String("18446744073709551615", unsigned64)
          && unsigned64 == std::numeric_limits<std::uint64_t>::max());
    CHECK(!parseUint64String("18446744073709551616", unsigned64));
    CHECK(!parseUint64String("-1", unsigned64));
}

void testControlRoundTrips() {
    // HELLO / HELLO_ACK
    control::Hello hello;
    hello.clientVersion = "LHolo/test";
    hello.protocolMajor = kProtocolMajor;
    hello.protocolMinor = kProtocolMinor;
    hello.features = {"files", "placements", "edit_lock"};
    control::Hello helloOut;
    CHECK(control::decode(control::encode(hello), helloOut).ok);
    CHECK(helloOut.clientVersion == hello.clientVersion);
    CHECK(helloOut.protocolMajor == kProtocolMajor);
    CHECK(helloOut.features == hello.features);

    control::HelloAck ack;
    ack.serverVersion = "LHoloSyncServer/test";
    ack.serverId = Uuid::random();
    ack.worldId = Uuid::random();
    ack.sessionId = Uuid::random();
    ack.features = {"files"};
    ack.role = Role::Member;
    ack.maxFileBytes = 128ull * 1024 * 1024;
    ack.maxActiveTransfers = 2;
    ack.catalogSequence = 143;
    control::HelloAck ackOut;
    CHECK(control::decode(control::encode(ack), ackOut).ok);
    CHECK(ackOut.serverId == ack.serverId);
    CHECK(ackOut.worldId == ack.worldId);
    CHECK(ackOut.role == Role::Member);
    CHECK(ackOut.maxFileBytes == ack.maxFileBytes);
    CHECK(ackOut.catalogSequence == 143);

    // Placement object round trip (LIST_PAGE item shape).
    SharedPlacementDto dto;
    dto.placementId = Uuid::random();
    dto.fileHash = sha256::hashString("payload");
    dto.format = SchematicFormat::Mcstructure;
    dto.displayName = "自動倉庫";
    dto.dimension = 1;
    dto.originX = -100;
    dto.originY = 64;
    dto.originZ = 200;
    dto.rotation = 3;
    dto.mirror = 2;
    dto.revision = 15;
    dto.ownerId = "actor";
    dto.updatedAtUnixMs = 1793682000000ll;
    dto.deleted = false;
    SharedPlacementDto dtoOut;
    CHECK(control::decodePlacement(control::encodePlacement(dto), dtoOut).ok);
    CHECK(dtoOut.placementId == dto.placementId);
    CHECK(dtoOut.fileHash == dto.fileHash);
    CHECK(dtoOut.format == dto.format);
    CHECK(dtoOut.displayName == dto.displayName);
    CHECK(dtoOut.dimension == dto.dimension);
    CHECK(dtoOut.originX == dto.originX && dtoOut.originZ == dto.originZ);
    CHECK(dtoOut.rotation == dto.rotation && dtoOut.mirror == dto.mirror);
    CHECK(dtoOut.revision == dto.revision);
    CHECK(dtoOut.updatedAtUnixMs == dto.updatedAtUnixMs);

    // Error message.
    control::ErrorMessage err;
    err.code = ErrorCode::RevisionConflict;
    err.message = "stale revision";
    err.retryable = true;
    err.retryAfterMs = 500;
    control::ErrorMessage errOut;
    CHECK(control::decode(control::encode(err), errOut).ok);
    CHECK(errOut.code == ErrorCode::RevisionConflict);
    CHECK(errOut.retryable && errOut.retryAfterMs == 500);
}

void testControlRejection() {
    // Duplicate keys are rejected.
    std::string duplicate = R"({"cursor":null,"cursor":null,"page_size":1,"known_sequence":"0"})";
    control::ListRequest request;
    CHECK(!control::decode(bytes(duplicate), request).ok);

    // Missing required field.
    std::string missing = R"({"page_size":1})";
    CHECK(!control::decode(bytes(missing), request).ok);

    // Out-of-range values.
    std::string badPage = R"({"cursor":null,"page_size":9999,"known_sequence":"0"})";
    CHECK(!control::decode(bytes(badPage), request).ok);

    // Control JSON over the 32 KiB budget.
    std::string huge = R"({"cursor":")" + std::string(kMaxControlJsonBytes, 'a') + R"(","page_size":1,"known_sequence":"0"})";
    CHECK(!control::decode(bytes(huge), request).ok);

    // Non-object root.
    CHECK(!control::decode(bytes("[1,2,3]"), request).ok);

    // Unknown format enum.
    control::FileOffer offer;
    std::string badFormat =
        R"({"client_operation_id":"c4391dbd-0cd1-4b7b-8ee7-61ff2f7f4b6b","sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","format":"lholo","size_bytes":"10","display_filename":"x"})";
    CHECK(!control::decode(bytes(badFormat), offer).ok);

    // Origin out of the LHolo coordinate range.
    control::PlacementCreate create;
    std::string badOrigin =
        R"({"client_operation_id":"c4391dbd-0cd1-4b7b-8ee7-61ff2f7f4b6b","display_name":"x","file_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","file_format":"litematic","dimension":0,"origin":[99999999,64,0],"rotation":0,"mirror":0})";
    CHECK(!control::decode(bytes(badOrigin), create).ok);
}

} // namespace

int main() {
    testFrameRoundTrip();
    testFrameRejection();
    testSha256();
    testUuidAndHashParsing();
    testFileChunkCodec();
    testValidationPrimitives();
    testControlRoundTrips();
    testControlRejection();

    std::printf("LHoloSyncTests: %d checks, %d failures\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
