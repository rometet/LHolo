// LHolo - Client-side projection renderer for Minecraft Bedrock Windows
// Copyright (C) 2026  MarmieQi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "structure/formats/StructureFormatLoaders.h"
#include "structure/formats/RegionMergePlan.h"
#include "structure/formats/JavaNbtReader.h"
#include "structure/formats/LitematicFormatRules.h"
#include "structure/formats/McstructureIndices.h"
#include "structure/formats/McstructureInput.h"
#include "structure/StructureLoader.h"
#include "structure/PlacementSession.h"
#include "block/BlockPlacementRules.h"
#include "block/MaterialKeyCache.h"
#include "structure/java_to_bedrock/JavaBlockEntityToBedrock.h"
#include "structure/java_to_bedrock/JavaToBedrock.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <map>
#include <optional>
#include <set>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <variant>
#include <vector>

#include <zlib.h>

#include "ll/api/service/Bedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/ByteArrayTag.h"
#include "mc/deps/nbt/IntArrayTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/block/actor/BlockActorType.h"
#include "mc/world/level/block/registry/BlockTypeRegistry.h"
#include "mc/world/level/material/Material.h"
#include "mc/world/level/levelgen/structure/StructureBlockPalette.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include "mc/world/level/levelgen/structure/StructureCompression.h"
#include "mc/world/level/Level.h"

namespace lholo::structure::detail {

struct PreparedStructureLoad {
    std::filesystem::path       path;
    std::shared_ptr<CompoundTag> root;
    int                         sizeX{};
    int                         sizeY{};
    int                         sizeZ{};
    std::uint64_t               volume{};
    std::uint64_t               primaryBlocks{};
    std::uint64_t               secondaryBlocks{};
    std::uint64_t               paletteEntries{};
    std::vector<std::uint64_t>  occupiedIndices;
    // Large block-index layers are kept as primitive arrays and deliberately
    // omitted from the generic CompoundTag tree.
    std::vector<std::vector<std::int32_t>> blockIndexLayers;
    // Original bytes are retained only for the compatibility fallback.
    std::shared_ptr<std::string> fallbackOriginalBytes;
    // Composite .lholo manifest: NBT parsing of every referenced mcstructure
    // happens on the background preparation thread, never on Present.
    struct Component {
        std::string name;
        std::shared_ptr<PreparedStructureLoad> prepared;
    };
    std::vector<Component> components;
    bool composite{};
};

namespace {

constexpr std::uintmax_t kMaximumStructureFileSize = 512ull * 1024ull * 1024ull;
constexpr std::size_t    kMaximumInflatedFileSize  = 1024ull * 1024ull * 1024ull;
std::atomic_uint64_t     gGeneration{0};

void assignMaterialIndices(LoadedStructure& loaded) {
    std::map<std::string, std::uint64_t> bodyCounts;
    std::map<std::string, std::uint64_t> liquidCounts;
    std::unordered_map<Block const*, std::string> cachedKeys;
    std::string const emptyKey;
    auto const keyFor = [&](Block const* value) -> std::string const& {
        if (!value) return emptyKey;
        return block::detail::cachedMaterialKey(cachedKeys, value, [&] {
            return block::materialKey(value->getTypeName());
        });
    };
    for (auto const& entry : loaded.renderBlocks) {
        auto const& body = keyFor(entry.block);
        auto const& liquid = keyFor(entry.liquid);
        if (!body.empty()) ++bodyCounts[body];
        if (!liquid.empty()) ++liquidCounts[liquid];
    }
    auto sortedKeys = [](auto const& counts) {
        std::vector<std::string> keys;
        for (auto const& [key, count] : counts) {
            (void)count;
            keys.push_back(key);
        }
        std::sort(keys.begin(), keys.end(), [&](auto const& left, auto const& right) {
            if (counts.at(left) != counts.at(right)) return counts.at(left) > counts.at(right);
            return left < right;
        });
        return keys;
    };
    auto const bodyKeys = sortedKeys(bodyCounts);
    auto const liquidKeys = sortedKeys(liquidCounts);
    std::map<std::string, int> bodyIndices;
    std::map<std::string, int> liquidIndices;
    for (std::size_t index = 0; index < bodyKeys.size(); ++index) {
        bodyIndices.emplace(bodyKeys[index], static_cast<int>(index));
    }
    for (std::size_t index = 0; index < liquidKeys.size(); ++index) {
        liquidIndices.emplace(
            liquidKeys[index], static_cast<int>(bodyKeys.size() + index)
        );
    }
    for (auto& entry : loaded.renderBlocks.mutableValues()) {
        if (auto const found = bodyIndices.find(keyFor(entry.block)); found != bodyIndices.end()) {
            entry.materialIndex = found->second;
        }
        if (auto const found = liquidIndices.find(keyFor(entry.liquid)); found != liquidIndices.end()) {
            entry.liquidMaterialIndex = found->second;
        }
    }
    loaded.materialCount = bodyKeys.size() + liquidKeys.size();
}

std::optional<std::string> readFile(std::filesystem::path const& path, std::string& error) {
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError)) {
        error = filesystemError ? filesystemError.message() : "路径不是普通文件";
        return std::nullopt;
    }
    auto const size = std::filesystem::file_size(path, filesystemError);
    if (filesystemError) {
        error = filesystemError.message();
        return std::nullopt;
    }
    if (size == 0 || size > kMaximumStructureFileSize) {
        error = "文件为空或超过 512 MiB 限制";
        return std::nullopt;
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        error = "文件过大";
        return std::nullopt;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "无法打开文件";
        return std::nullopt;
    }
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        error = "读取文件失败";
        return std::nullopt;
    }
    return bytes;
}

CompoundTagVariant const* findTag(CompoundTag const& parent, std::string_view name) {
    auto const found = parent.mTags.find(name);
    return found == parent.mTags.end() ? nullptr : &found->second;
}

CompoundTag const* findCompound(CompoundTag const& parent, std::string_view name) {
    auto const* value = findTag(parent, name);
    return value && value->hold<CompoundTag>() ? &value->get<CompoundTag>() : nullptr;
}

ListTag const* findList(CompoundTag const& parent, std::string_view name) {
    auto const* value = findTag(parent, name);
    return value && value->hold<ListTag>() ? &value->get<ListTag>() : nullptr;
}

bool readThreeInts(ListTag const& list, int& x, int& y, int& z) {
    if (list.size() != 3) return false;
    auto const read = [&list](std::size_t index, int& output) {
        auto const& value = list[index];
        if (!value.hold<IntTag>()) return false;
        output = static_cast<int>(value.get<IntTag>());
        return true;
    };
    return read(0, x) && read(1, y) && read(2, z);
}

std::optional<std::array<int,3>> readMcstructureWorldOrigin(CompoundTag const& root) {
    auto const* origin=findList(root,"structure_world_origin");
    std::array<int,3> coords{};
    if(!origin || !readThreeInts(*origin,coords[0],coords[1],coords[2]))
        return std::nullopt;
    return coords;
}

bool checkedVolume3(
    std::uint64_t x,
    std::uint64_t y,
    std::uint64_t z,
    std::uint64_t& result
) {
    if (x == 0 || y == 0 || z == 0) return false;
    auto const max = std::numeric_limits<std::uint64_t>::max();
    if (x > max / y) return false;
    auto const xy = x * y;
    if (xy > max / z) return false;
    result = xy * z;
    return result <= static_cast<std::uint64_t>(
        std::numeric_limits<std::size_t>::max()
    );
}

bool inspectBlockLayer(
    ListTag const&             layer,
    std::uint64_t              volume,
    std::size_t                paletteSize,
    std::uint64_t&             occupied,
    std::vector<std::uint64_t>* occupiedIndices = nullptr
) {
    if (static_cast<std::uint64_t>(layer.size()) != volume) return false;
    occupied = 0;
    for (std::size_t index = 0; index < layer.size(); ++index) {
        auto const& value = layer[index];
        if (!value.hold<IntTag>()) return false;
        if (!validPaletteIndex(static_cast<int>(value.get<IntTag>()), paletteSize)) return false;
        if (static_cast<int>(value.get<IntTag>()) >= 0) {
            ++occupied;
            if (occupiedIndices) occupiedIndices->push_back(index);
        }
    }
    return true;
}

// format 2 (1.26.5x+) stores each index layer as a TAG_Int_Array instead of
// a nested TAG_List of ints.
bool inspectBlockLayer(
    IntArrayTag const&         layer,
    std::uint64_t              volume,
    std::size_t                paletteSize,
    std::uint64_t&             occupied,
    std::vector<std::uint64_t>* occupiedIndices = nullptr
) {
    if (static_cast<std::uint64_t>(layer.size()) != volume) return false;
    occupied = 0;
    for (std::size_t index = 0; index < layer.size(); ++index) {
        if (!validPaletteIndex(static_cast<int>(layer[index]), paletteSize)) return false;
        if (static_cast<int>(layer[index]) >= 0) {
            ++occupied;
            if (occupiedIndices) occupiedIndices->push_back(index);
        }
    }
    return true;
}

bool collectRenderableCandidates(
    ListTag const&                   layer,
    std::uint64_t                    volume,
    std::uint64_t&                   occupied,
    std::vector<std::uint8_t> const& airPalette,
    std::vector<std::uint8_t>&       candidateMask
) {
    if (static_cast<std::uint64_t>(layer.size()) != volume
        || candidateMask.size() != static_cast<std::size_t>(volume)) {
        return false;
    }
    occupied = 0;
    for (std::size_t index = 0; index < layer.size(); ++index) {
        auto const& value = layer[index];
        if (!value.hold<IntTag>()) return false;
        auto const paletteIndex = static_cast<int>(value.get<IntTag>());
        if (!validPaletteIndex(paletteIndex, airPalette.size())) return false;
        if (paletteIndex < 0) continue;
        ++occupied;
        auto const knownAir = static_cast<std::size_t>(paletteIndex) < airPalette.size()
            && airPalette[static_cast<std::size_t>(paletteIndex)] != 0;
        if (!knownAir) candidateMask[index] = 1;
    }
    return true;
}

bool collectRenderableCandidates(
    IntArrayTag const&               layer,
    std::uint64_t                    volume,
    std::uint64_t&                   occupied,
    std::vector<std::uint8_t> const& airPalette,
    std::vector<std::uint8_t>&       candidateMask
) {
    if (static_cast<std::uint64_t>(layer.size()) != volume
        || candidateMask.size() != static_cast<std::size_t>(volume)) {
        return false;
    }
    occupied = 0;
    for (std::size_t index = 0; index < layer.size(); ++index) {
        auto const paletteIndex = static_cast<int>(layer[index]);
        if (!validPaletteIndex(paletteIndex, airPalette.size())) return false;
        if (paletteIndex < 0) continue;
        ++occupied;
        auto const knownAir = static_cast<std::size_t>(paletteIndex) < airPalette.size()
            && airPalette[static_cast<std::size_t>(paletteIndex)] != 0;
        if (!knownAir) candidateMask[index] = 1;
    }
    return true;
}

bool readJavaVec3(JavaNbtTag::Compound const& parent, std::string_view name, int& x, int& y, int& z) {
    auto const* compound = javaValue<JavaNbtTag::Compound>(parent, name);
    if (!compound) return false;
    auto const* xValue = javaValue<std::int32_t>(*compound, "x");
    auto const* yValue = javaValue<std::int32_t>(*compound, "y");
    auto const* zValue = javaValue<std::int32_t>(*compound, "z");
    if (!xValue || !yValue || !zValue) return false;
    x = *xValue;
    y = *yValue;
    z = *zValue;
    return true;
}

std::optional<bool> readJavaBoolean(JavaNbtTag::Compound const& parent, std::string_view name) {
    if (auto const* value = javaValue<std::int8_t>(parent, name)) return *value != 0;
    return std::nullopt;
}

bool readJavaSignSide(JavaNbtTag::Compound const& parent, std::string_view name, JavaSignSideData& output) {
    auto const* side = javaValue<JavaNbtTag::Compound>(parent, name);
    if (!side) return false;
    if (auto const* messages = javaValue<JavaNbtTag::List>(*side, "messages")) {
        auto const count = std::min(messages->size(), output.messages.size());
        for (std::size_t line = 0; line < count; ++line) {
            if (auto const* message = std::get_if<std::string>(&(*messages)[line].value)) {
                output.messages[line] = *message;
            }
        }
    }
    if (auto const glowing = readJavaBoolean(*side, "has_glowing_text")) output.glowing = *glowing;
    return true;
}

std::optional<JavaSignBlockEntityData> readJavaSignBlockEntity(JavaNbtTag::Compound const& entity) {
    JavaSignBlockEntityData result;
    bool const hasModernFront = readJavaSignSide(entity, "front_text", result.front);
    bool const hasModernBack = readJavaSignSide(entity, "back_text", result.back);
    bool       hasLegacyText{};
    if (!hasModernFront) {
        static constexpr std::array<std::string_view, 4> legacyLines{"Text1", "Text2", "Text3", "Text4"};
        for (std::size_t line = 0; line < legacyLines.size(); ++line) {
            if (auto const* message = javaValue<std::string>(entity, legacyLines[line])) {
                result.front.messages[line] = *message;
                hasLegacyText = true;
            }
        }
        if (auto const glowing = readJavaBoolean(entity, "GlowingText")) result.front.glowing = *glowing;
    }
    if (auto const waxed = readJavaBoolean(entity, "is_waxed")) result.waxed = *waxed;
    return hasModernFront || hasModernBack || hasLegacyText
        ? std::optional<JavaSignBlockEntityData>{std::move(result)} : std::nullopt;
}

std::uint64_t localCellIndex(int x, int y, int z, int sizeY, int sizeZ) {
    return (static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(sizeY)
            + static_cast<std::uint64_t>(y))
        * static_cast<std::uint64_t>(sizeZ) + static_cast<std::uint64_t>(z);
}

std::optional<std::string> inflateGzip(std::string_view compressed, std::string& error) {
    if (compressed.size() < 2 || static_cast<std::uint8_t>(compressed[0]) != 0x1f
        || static_cast<std::uint8_t>(compressed[1]) != 0x8b) {
        return std::string{compressed};
    }
    z_stream stream{};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
    stream.avail_in = static_cast<uInt>(std::min<std::size_t>(compressed.size(), std::numeric_limits<uInt>::max()));
    if (inflateInit2(&stream, 15 + 16) != Z_OK) {
        error = "无法初始化 gzip 解压器";
        return std::nullopt;
    }
    struct EndInflate { z_stream* stream; ~EndInflate() { inflateEnd(stream); } } end{&stream};
    std::string output;
    std::array<char, 256 * 1024> chunk{};
    int result = Z_OK;
    while (result == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        result = inflate(&stream, Z_NO_FLUSH);
        auto const produced = chunk.size() - stream.avail_out;
        if (output.size() + produced > kMaximumInflatedFileSize) {
            error = "解压后的 Litematic 超过 1 GiB 限制";
            return std::nullopt;
        }
        output.append(chunk.data(), produced);
    }
    if (result != Z_STREAM_END) {
        error = std::string{"Litematic gzip 解压失败: "} + (stream.msg ? stream.msg : "未知错误");
        return std::nullopt;
    }
    return output;
}

ResolvedJavaBlock resolveJavaBlock(JavaNbtTag const& paletteEntry, int javaDataVersion) {
    auto const* compound = std::get_if<JavaNbtTag::Compound>(&paletteEntry.value);
    if (!compound) return {};
    auto const* name = javaValue<std::string>(*compound, "Name");
    if (!name || name->empty()) return {};
    // Carry the Java `Properties` (all string values) into the mapper so that
    // orientation and other stored states survive the Java -> Bedrock crossing.
    std::vector<std::pair<std::string, std::string>> properties;
    if (auto const* props = javaValue<JavaNbtTag::Compound>(*compound, "Properties")) {
        properties.reserve(props->size());
        for (auto const& [key, value] : *props) {
            if (auto const* text = std::get_if<std::string>(&value.value)) {
                properties.emplace_back(key, *text);
            }
        }
    }
    return resolveJavaBlockState(*name, properties, javaDataVersion);
}

std::shared_ptr<PreparedStructureLoad> prepareMcstructure(
    std::filesystem::path const& path,
    std::string&                 error
) {
    auto bytes = readFile(path, error);
    if (!bytes) return nullptr;

    auto stripped = stripMcstructureBlockIndices(*bytes, sizeof(CompoundTagVariant));
    auto const& parseBytes = stripped ? stripped->parseBytes : *bytes;
    auto root = CompoundTag::fromBinaryNbt(parseBytes, true);
    if (!root) {
        error = "不是有效的 Bedrock little-endian NBT: " + root.error().message();
        return nullptr;
    }

    auto prepared = std::make_shared<PreparedStructureLoad>();
    prepared->path = path;
    if (stripped) {
        prepared->blockIndexLayers = std::move(stripped->layers);
        prepared->fallbackOriginalBytes =
            std::make_shared<std::string>(std::move(*bytes));
    }
    // The parsed NBT tree is already owned by the expected value. Move it into
    // the staged result instead of deep-copying every block-index tag.
    prepared->root = std::make_shared<CompoundTag>(std::move(*root));

    // Compressed 26.60 native format: structure is a zlib ByteArray.
    auto const* compressionTag = findTag(*prepared->root, "compression");
    auto compression = StructureCompression::None;
    if (compressionTag) {
        if (!compressionTag->hold<IntTag>()) {
            error = "mcstructure compression must be an integer";
            return nullptr;
        }
        compression = static_cast<StructureCompression>(static_cast<int>(compressionTag->get<IntTag>()));
    }
    if (compression == StructureCompression::Zlib) {
        auto const* tag = findTag(*prepared->root, "structure");
        if (!tag || !tag->hold<ByteArrayTag>()) {
            error = "compressed mcstructure must contain a structure ByteArray";
            return nullptr;
        }
        auto const& payload = tag->get<ByteArrayTag>();
        auto decodedBytes = inflateMcstructureTag(
            {reinterpret_cast<char const*>(payload.data()), payload.size()},
            kMaximumInflatedFileSize,error);
        if (!decodedBytes) return nullptr;
        auto decoded = CompoundTag::fromBinaryNbt(*decodedBytes,true);
        if (!decoded) {
            error = "decompressed mcstructure invalid NBT: " + decoded.error().message();
            return nullptr;
        }
        prepared->root->mTags["structure"] = std::move(*decoded);
        // Retain the intact compressed envelope for vanilla fallback.
        if (!prepared->fallbackOriginalBytes)
            prepared->fallbackOriginalBytes=std::make_shared<std::string>(std::move(*bytes));
    } else if (compression != StructureCompression::None) {
        error="unsupported mcstructure compression value";
        return nullptr;
    }
    auto const* size = findList(*prepared->root, "size");
    auto const* structure = findCompound(*prepared->root, "structure");
    if (!size || !structure) {
        error = "缺少 size 或 structure 标签";
        return nullptr;
    }
    if (!readThreeInts(*size, prepared->sizeX, prepared->sizeY, prepared->sizeZ)
        || prepared->sizeX <= 0 || prepared->sizeY <= 0 || prepared->sizeZ <= 0
        || !checkedVolume3(
            static_cast<std::uint64_t>(prepared->sizeX),
            static_cast<std::uint64_t>(prepared->sizeY),
            static_cast<std::uint64_t>(prepared->sizeZ),
            prepared->volume
        )) {
        error = "结构尺寸无效或体积溢出";
        return nullptr;
    }

    auto const* palette = findCompound(*structure, "palette");
    auto const* defaultPalette = palette ? findCompound(*palette, "default") : nullptr;
    auto const* blockPalette = defaultPalette ? findList(*defaultPalette, "block_palette") : nullptr;
    if (!blockPalette) {
        error = "缺少 palette.default.block_palette";
        return nullptr;
    }
    prepared->paletteEntries = static_cast<std::uint64_t>(blockPalette->size());

    // Resolve literal air entries from the file palette itself. Unknown or
    // malformed entries remain candidates so this optimization can never hide
    // an unrecognized real block.
    std::vector<std::uint8_t> airPalette(blockPalette->size(), 0);
    for (std::size_t paletteIndex = 0; paletteIndex < blockPalette->size(); ++paletteIndex) {
        auto const& entry = (*blockPalette)[paletteIndex];
        if (!entry.hold<CompoundTag>()) continue;
        auto const* nameTag = findTag(entry.get<CompoundTag>(), "name");
        if (!nameTag || !nameTag->hold<StringTag>()) continue;
        auto const& name = static_cast<std::string const&>(nameTag->get<StringTag>());
        if (name == "minecraft:air") airPalette[paletteIndex] = 1;
    }

    auto const* blockIndices = findList(*structure, "block_indices");
    if (prepared->blockIndexLayers.empty()) {
        if (!blockIndices || blockIndices->empty()
            || !((*blockIndices)[0].hold<ListTag>() || (*blockIndices)[0].hold<IntArrayTag>())) {
            error = "block_indices 不是有效的双层索引";
            return nullptr;
        }
        if (blockIndices->size() > 2) {
            error = "block_indices 包含过多索引层";
            return nullptr;
        }
        // Validate generic fallback shapes before allocating a volume-sized
        // candidate mask. A tiny unsupported index list must not make a huge
        // declared size allocate gigabytes before the mismatch is discovered.
        for (auto const& layer : *blockIndices) {
            std::uint64_t occupied{};
            bool const valid = layer.hold<ListTag>()
                ? inspectBlockLayer(layer.get<ListTag>(), prepared->volume, blockPalette->size(), occupied)
                : layer.hold<IntArrayTag>()
                    && inspectBlockLayer(layer.get<IntArrayTag>(), prepared->volume, blockPalette->size(), occupied);
            if (!valid) {
                error = "方块索引数量、类型或调色板索引与结构不匹配";
                return nullptr;
            }
        }
    } else {
        if (prepared->blockIndexLayers.size() > 2) {
            error = "block_indices 包含过多索引层";
            return nullptr;
        }
        for (auto const& layer : prepared->blockIndexLayers) {
            if (layer.size() != static_cast<std::size_t>(prepared->volume)) {
                error = "方块索引数量与结构尺寸不匹配";
                return nullptr;
            }
        }
    }

    // Union the two layers into a dense byte mask. This avoids pushing more
    // than one million indices and then sorting/uniquing them on legacy files.
    std::vector<std::uint8_t> candidateMask(
        static_cast<std::size_t>(prepared->volume), 0
    );
    if (!prepared->blockIndexLayers.empty()) {
        if (!collectRenderableCandidates(
                prepared->blockIndexLayers[0],
                prepared->volume,
                prepared->primaryBlocks,
                airPalette,
                candidateMask
            )) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
        if (prepared->blockIndexLayers.size() >= 2
            && !collectRenderableCandidates(
                prepared->blockIndexLayers[1],
                prepared->volume,
                prepared->secondaryBlocks,
                airPalette,
                candidateMask
            )) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
    } else if ((*blockIndices)[0].hold<ListTag>()) {
        if (!collectRenderableCandidates(
                (*blockIndices)[0].get<ListTag>(),
                prepared->volume,
                prepared->primaryBlocks,
                airPalette,
                candidateMask
            )) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
        if (blockIndices->size() >= 2
            && (!(*blockIndices)[1].hold<ListTag>()
                || !collectRenderableCandidates(
                    (*blockIndices)[1].get<ListTag>(),
                    prepared->volume,
                    prepared->secondaryBlocks,
                    airPalette,
                    candidateMask
                ))) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
    } else {
        if (!collectRenderableCandidates(
                (*blockIndices)[0].get<IntArrayTag>(),
                prepared->volume,
                prepared->primaryBlocks,
                airPalette,
                candidateMask
            )) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
        if (blockIndices->size() >= 2
            && (!(*blockIndices)[1].hold<IntArrayTag>()
                || !collectRenderableCandidates(
                    (*blockIndices)[1].get<IntArrayTag>(),
                    prepared->volume,
                    prepared->secondaryBlocks,
                    airPalette,
                    candidateMask
                ))) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
    }

    auto const candidateCount = static_cast<std::size_t>(
        std::count(candidateMask.begin(), candidateMask.end(), std::uint8_t{1})
    );
    prepared->occupiedIndices.reserve(candidateCount);
    for (std::size_t index = 0; index < candidateMask.size(); ++index) {
        if (candidateMask[index]) prepared->occupiedIndices.push_back(index);
    }
    return prepared;
}

std::shared_ptr<LoadedStructure> finalizeMcstructure(
    std::shared_ptr<PreparedStructureLoad> const& prepared,
    std::string&                                  error
) {
    if (!prepared || !prepared->root) {
        error = "结构预解析数据无效";
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sourceWorldOrigin=readMcstructureWorldOrigin(*prepared->root);
    loaded->sizeX = prepared->sizeX;
    loaded->sizeY = prepared->sizeY;
    loaded->sizeZ = prepared->sizeZ;
    loaded->volume = prepared->volume;
    loaded->primaryBlocks = prepared->primaryBlocks;
    loaded->secondaryBlocks = prepared->secondaryBlocks;
    loaded->paletteEntries = prepared->paletteEntries;
    loaded->regions.push_back({0, 0, 0, loaded->sizeX, loaded->sizeY, loaded->sizeZ, "main"});

    auto const* structure = findCompound(*prepared->root, "structure");
    auto const* palette = structure ? findCompound(*structure, "palette") : nullptr;
    auto const* defaultPalette = palette ? findCompound(*palette, "default") : nullptr;
    auto const* blockPositionData = defaultPalette
        ? findCompound(*defaultPalette, "block_position_data") : nullptr;
    if (!structure || !defaultPalette) {
        error = "结构预解析数据缺少 palette/default";
        return nullptr;
    }

    auto const clientLevel = ll::service::getMultiPlayerLevel();
    if (!clientLevel) {
        error = "尚未进入世界，无法解析结构";
        return nullptr;
    }

    // Fast path: parse only the block palette with Bedrock's typed API, then
    // consume the already-parsed index arrays directly. This avoids
    // StructureTemplate::load walking the full structure volume again.
    auto const* stagedBlockIndices = findList(*structure, "block_indices");
    auto const stagedLayerCount = !prepared->blockIndexLayers.empty()
        ? prepared->blockIndexLayers.size()
        : (stagedBlockIndices ? stagedBlockIndices->size() : 0);
    if (stagedLayerCount != 0) {
        // Native block-palette parse/destructors are not exported on 26.60.
        // Construct a 1-cell test structure that keeps the exact palette, then
        // use MCAPI StructureTemplate::load. The real index streams remain in
        // prepared->blockIndexLayers and never enter the native parser.
        std::unique_ptr<StructureTemplate> paletteTemplate;
        try {
            CompoundTag paletteProbe{*prepared->root};
            auto& probeSize = paletteProbe.at("size").get<ListTag>();
            auto& probeStructure = paletteProbe.at("structure").get<CompoundTag>();
            auto& probeLayers = probeStructure.at("block_indices").get<ListTag>();
            if (probeSize.size() == 3
                && probeSize[0]->getId() == Tag::Int
                && probeSize[1]->getId() == Tag::Int
                && probeSize[2]->getId() == Tag::Int) {
                for (auto& component : probeSize) component->as<IntTag>().data = 1;
                probeLayers.clear();
                probeLayers.mType = Tag::IntArray;
                probeLayers.emplace_back(std::make_unique<IntArrayTag>(std::vector<int>{0}));
                probeLayers.emplace_back(std::make_unique<IntArrayTag>(std::vector<int>{-1}));
                auto& probeDefault = probeStructure.at("palette").get<CompoundTag>()
                    .at("default").get<CompoundTag>();
                // Keep the native tag schema, but omit all unrelated data.
                // A 1-cell palette probe must not deserialize the full list
                // of block entities or world actors from a giant schematic.
                probeDefault.mTags["block_position_data"] = CompoundTag{};
                if (auto entity = probeStructure.mTags.find("entities");
                    entity != probeStructure.mTags.end()
                    && entity->second.hold<ListTag>()) {
                    entity->second.get<ListTag>().clear();
                }
                paletteTemplate = std::make_unique<StructureTemplate>(
                    "lholo:palette_only", clientLevel->getUnknownBlockTypeRegistry()
                );
                if (!paletteTemplate->load(paletteProbe)) paletteTemplate.reset();
            }
        } catch (...) {
            paletteTemplate.reset();
        }
        auto const* stagedPalette = paletteTemplate
            ? paletteTemplate->mStructureTemplateData.get().getPalette(
                StructureTemplateData::DEFAULT_PALETTE_NAME())
            : nullptr;
        if (stagedPalette) {
            auto const unknownRegistry = clientLevel->getUnknownBlockTypeRegistry();
            std::vector<Block const*> resolvedPalette(
                static_cast<std::size_t>(prepared->paletteEntries), nullptr
            );
            for (std::size_t paletteIndex = 0;
                 paletteIndex < resolvedPalette.size();
                 ++paletteIndex) {
                auto const* resolved = stagedPalette->tryGetBlock(
                    static_cast<std::uint64_t>(paletteIndex), unknownRegistry
                );
                if (resolved && !resolved->isAir()) {
                    resolvedPalette[paletteIndex] = resolved;
                }
            }

            auto const paletteIndexAt = [&](std::size_t layerIndex, std::size_t index) -> int {
                if (!prepared->blockIndexLayers.empty()) {
                    if (layerIndex >= prepared->blockIndexLayers.size()) return -1;
                    auto const& values = prepared->blockIndexLayers[layerIndex];
                    return index < values.size() ? static_cast<int>(values[index]) : -1;
                }
                if (!stagedBlockIndices || layerIndex >= stagedBlockIndices->size()) return -1;
                auto const& layer = (*stagedBlockIndices)[layerIndex];
                if (layer.hold<ListTag>()) {
                    auto const& values = layer.get<ListTag>();
                    if (index >= values.size() || !values[index].hold<IntTag>()) return -1;
                    return static_cast<int>(values[index].get<IntTag>());
                }
                if (layer.hold<IntArrayTag>()) {
                    auto const& values = layer.get<IntArrayTag>();
                    if (index >= values.size()) return -1;
                    return static_cast<int>(values[index]);
                }
                return -1;
            };
            auto const resolveNative = [&](int paletteIndex) -> Block const* {
                if (paletteIndex < 0
                    || static_cast<std::size_t>(paletteIndex) >= resolvedPalette.size()) {
                    return nullptr;
                }
                return resolvedPalette[static_cast<std::size_t>(paletteIndex)];
            };

            loaded->renderBlocks.reserve(prepared->occupiedIndices.size());
            auto const yz = static_cast<std::uint64_t>(loaded->sizeY)
                * static_cast<std::uint64_t>(loaded->sizeZ);
            for (auto const index64 : prepared->occupiedIndices) {
                auto const index = static_cast<std::size_t>(index64);
                auto const* primary = resolveNative(
                    paletteIndexAt(0, index)
                );
                auto const* secondary = stagedLayerCount >= 2
                    ? resolveNative(paletteIndexAt(1, index))
                    : nullptr;

                Block const* block{};
                Block const* liquid{};
                auto const assign = [&](Block const* value) {
                    if (!value) return;
                    if (value->getBlockType().mMaterial.get().mLiquid) liquid = value;
                    else if (!block) block = value;
                };
                assign(primary);
                assign(secondary);
                if (primary && primary->getTypeName() == "minecraft:bubble_column") {
                    block = primary;
                    liquid = (secondary && secondary->getBlockType().mMaterial.get().mLiquid)
                        ? secondary : nullptr;
                } else if (secondary && secondary->getTypeName() == "minecraft:bubble_column") {
                    block = secondary;
                    liquid = (primary && primary->getBlockType().mMaterial.get().mLiquid)
                        ? primary : nullptr;
                }
                if (!block && !liquid) continue;

                auto const x = index64 / yz;
                auto const remainder = index64 % yz;
                auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
                auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);

                std::shared_ptr<CompoundTag const> blockEntityNbt;
                if (blockPositionData && block
                    && block->getBlockType().getBlockEntityType() != BlockActorType::Undefined) {
                    auto const* positionData = findCompound(
                        *blockPositionData, std::to_string(index64)
                    );
                    auto const* entityData = positionData
                        ? findCompound(*positionData, "block_entity_data") : nullptr;
                    if (entityData) {
                        blockEntityNbt = std::make_shared<CompoundTag const>(*entityData);
                    }
                }
                loaded->renderBlocks.push_back({
                    static_cast<int>(x),
                    static_cast<int>(y),
                    static_cast<int>(z),
                    block,
                    liquid,
                    std::move(blockEntityNbt)
                });
            }

            if (!loaded->renderBlocks.empty()) {
                assignMaterialIndices(*loaded);
                loaded->generation =
                    gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
                loaded->sourcePath = prepared->path;
                return loaded;
            }
            loaded->renderBlocks.clear();
        }
    }

    // Compatibility fallback for unusual/older palettes. If the
    // background parser used an IntArray-normalized copy, reparse the original
    // bytes here so vanilla sees the exact file representation it expects.
    std::shared_ptr<CompoundTag> fallbackRoot = prepared->root;
    if (prepared->fallbackOriginalBytes) {
        // The optimized parse copy omits the huge list. A compatibility
        // fallback constructs the original generic tree, so check its larger
        // native per-element storage budget before asking the engine to parse.
        (void)BedrockNbtScanner{*prepared->fallbackOriginalBytes, sizeof(CompoundTagVariant),
                                kMaximumInflatedFileSize, false}.scan();
        auto originalRoot = CompoundTag::fromBinaryNbt(
            *prepared->fallbackOriginalBytes, true
        );
        if (!originalRoot) {
            error = "原始 mcstructure NBT 回退解析失败: "
                + originalRoot.error().message();
            return nullptr;
        }
        fallbackRoot = std::make_shared<CompoundTag>(std::move(*originalRoot));
    }

    auto nativeStructure = std::make_unique<StructureTemplate>(
        "lholo:projection",
        clientLevel->getUnknownBlockTypeRegistry()
    );
    if (!nativeStructure->load(*fallbackRoot)) {
        error = "原版 StructureTemplate 无法加载该结构";
        return nullptr;
    }

    auto const& nativeData = nativeStructure->mStructureTemplateData.get();
    auto const& nativeSize = nativeData.mSize.get();
    if (nativeSize.x != loaded->sizeX
        || nativeSize.y != loaded->sizeY
        || nativeSize.z != loaded->sizeZ) {
        error = "原版 StructureTemplate 返回的尺寸与文件不一致";
        return nullptr;
    }
    auto const* nativePalette = nativeData.getPalette(
        StructureTemplateData::DEFAULT_PALETTE_NAME()
    );
    if (!nativePalette) {
        error = "原版 StructureTemplate 缺少 default palette";
        return nullptr;
    }

    auto const& nativePrimary = nativeData.mBlockIndices.get();
    auto const& nativeSecondaryStorage = nativeData.mExtraBlockIndices.get();
    if (nativePrimary.size() != loaded->volume
        || (!nativeSecondaryStorage && loaded->secondaryBlocks > 0)
        || (nativeSecondaryStorage && nativeSecondaryStorage->size() != loaded->volume)) {
        error = "原版 StructureTemplate 的方块索引数量与结构体积不一致";
        return nullptr;
    }

    auto const unknownRegistry = nativeStructure->mUnknownBlockRegistry.get();

    // Palette cardinality is tiny compared with the structure volume. Resolve
    // each palette entry once instead of calling tryGetBlock() for every cell.
    std::vector<Block const*> resolvedPalette(
        static_cast<std::size_t>(prepared->paletteEntries), nullptr
    );
    for (std::size_t paletteIndex = 0; paletteIndex < resolvedPalette.size(); ++paletteIndex) {
        auto const* resolved = nativePalette->tryGetBlock(
            static_cast<std::uint64_t>(paletteIndex), unknownRegistry
        );
        if (resolved && !resolved->isAir()) resolvedPalette[paletteIndex] = resolved;
    }
    auto const resolveNative = [&](int paletteIndex) -> Block const* {
        if (paletteIndex < 0
            || static_cast<std::size_t>(paletteIndex) >= resolvedPalette.size()) {
            return nullptr;
        }
        return resolvedPalette[static_cast<std::size_t>(paletteIndex)];
    };

    loaded->renderBlocks.reserve(prepared->occupiedIndices.size());
    auto const yz = static_cast<std::uint64_t>(loaded->sizeY)
        * static_cast<std::uint64_t>(loaded->sizeZ);
    for (auto const index : prepared->occupiedIndices) {
        if (index >= nativePrimary.size()) continue;
        auto const* primary = resolveNative(
            nativePrimary[static_cast<std::size_t>(index)]
        );
        auto const* secondary = nativeSecondaryStorage
            ? resolveNative((*nativeSecondaryStorage)[static_cast<std::size_t>(index)])
            : nullptr;

        Block const* block{};
        Block const* liquid{};
        auto const assign = [&](Block const* value) {
            if (!value) return;
            if (value->getBlockType().mMaterial.get().mLiquid) liquid = value;
            else if (!block) block = value;
        };
        assign(primary);
        assign(secondary);
        if (primary && primary->getTypeName() == "minecraft:bubble_column") {
            block = primary;
            liquid = (secondary && secondary->getBlockType().mMaterial.get().mLiquid)
                ? secondary : nullptr;
        } else if (secondary && secondary->getTypeName() == "minecraft:bubble_column") {
            block = secondary;
            liquid = (primary && primary->getBlockType().mMaterial.get().mLiquid)
                ? primary : nullptr;
        }
        if (!block && !liquid) continue;

        auto const x = index / yz;
        auto const remainder = index % yz;
        auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);

        std::shared_ptr<CompoundTag const> blockEntityNbt;
        if (blockPositionData && block
            && block->getBlockType().getBlockEntityType() != BlockActorType::Undefined) {
            auto const* positionData = findCompound(
                *blockPositionData, std::to_string(index)
            );
            auto const* entityData = positionData
                ? findCompound(*positionData, "block_entity_data") : nullptr;
            if (entityData) {
                blockEntityNbt = std::make_shared<CompoundTag const>(*entityData);
            }
        }
        loaded->renderBlocks.push_back({
            static_cast<int>(x),
            static_cast<int>(y),
            static_cast<int>(z),
            block,
            liquid,
            std::move(blockEntityNbt)
        });
    }

    assignMaterialIndices(*loaded);
    loaded->renderBlocks.packFromValues(loaded->sizeX,loaded->sizeY,loaded->sizeZ);
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = prepared->path;
    return loaded;
}

std::shared_ptr<LoadedStructure> loadMcstructure(std::filesystem::path const& path, std::string& error) {
    auto bytes = readFile(path, error);
    if (!bytes) return nullptr;

    (void)BedrockNbtScanner{*bytes, sizeof(CompoundTagVariant), kMaximumInflatedFileSize, false}.scan();

    auto root = CompoundTag::fromBinaryNbt(*bytes, true);
    if (!root) {
        error = "不是有效的 Bedrock little-endian NBT: " + root.error().message();
        return nullptr;
    }

    auto const* size = findList(*root, "size");
    auto const* structure = findCompound(*root, "structure");
    if (!size || !structure) {
        error = "缺少 size 或 structure 标签";
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sourceWorldOrigin=readMcstructureWorldOrigin(*root);
    if (!readThreeInts(*size, loaded->sizeX, loaded->sizeY, loaded->sizeZ)
        || loaded->sizeX <= 0 || loaded->sizeY <= 0 || loaded->sizeZ <= 0) {
        error = "结构尺寸无效";
        return nullptr;
    }
    if (!checkedVolume3(
            static_cast<std::uint64_t>(loaded->sizeX),
            static_cast<std::uint64_t>(loaded->sizeY),
            static_cast<std::uint64_t>(loaded->sizeZ),
            loaded->volume
        )) {
        error = "Structure volume overflow";
        return nullptr;
    }
    loaded->regions.push_back({0, 0, 0, loaded->sizeX, loaded->sizeY, loaded->sizeZ, "main"});

    auto const* palette = findCompound(*structure, "palette");
    auto const* defaultPalette = palette ? findCompound(*palette, "default") : nullptr;
    auto const* blockPalette = defaultPalette ? findList(*defaultPalette, "block_palette") : nullptr;
    auto const* blockPositionData = defaultPalette
        ? findCompound(*defaultPalette, "block_position_data") : nullptr;
    if (!blockPalette) {
        error = "缺少 palette.default.block_palette";
        return nullptr;
    }
    loaded->paletteEntries = static_cast<std::uint64_t>(blockPalette->size());

    auto const* blockIndices = findList(*structure, "block_indices");
    if (!blockIndices || blockIndices->empty()
        || !((*blockIndices)[0].hold<ListTag>() || (*blockIndices)[0].hold<IntArrayTag>())) {
        error = "block_indices 不是有效的双层索引";
        return nullptr;
    }
    if (blockIndices->size() > 2) {
        error = "block_indices 包含过多索引层";
        return nullptr;
    }
    // format 1: List<List<Int>>（主层 + 可选副层）。
    // format 2 (1.26.5x 起): List<IntArray>——每层一个 IntArray，副层取消
    // （含水内联为方块状态，通常只有一个 IntArray）。两种形状都接受，缺失的
    // 副层视为全空；真正的解析交给原版 StructureTemplate::load，它会把两种
    // 形状都规范化为 mBlockIndices + optional<mExtraBlockIndices>。
    if ((*blockIndices)[0].hold<ListTag>()) {
        if (!inspectBlockLayer((*blockIndices)[0].get<ListTag>(), loaded->volume, blockPalette->size(), loaded->primaryBlocks)) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
        if (blockIndices->size() >= 2) {
            if (!(*blockIndices)[1].hold<ListTag>()
                || !inspectBlockLayer((*blockIndices)[1].get<ListTag>(), loaded->volume, blockPalette->size(), loaded->secondaryBlocks)) {
                error = "方块索引数量或类型与结构尺寸不匹配";
                return nullptr;
            }
        }
    } else {
        if (!inspectBlockLayer((*blockIndices)[0].get<IntArrayTag>(), loaded->volume, blockPalette->size(), loaded->primaryBlocks)) {
            error = "方块索引数量或类型与结构尺寸不匹配";
            return nullptr;
        }
        if (blockIndices->size() >= 2) {
            if (!(*blockIndices)[1].hold<IntArrayTag>()
                || !inspectBlockLayer((*blockIndices)[1].get<IntArrayTag>(), loaded->volume, blockPalette->size(), loaded->secondaryBlocks)) {
                error = "方块索引数量或类型与结构尺寸不匹配";
                return nullptr;
            }
        }
    }

    // Use the game's own StructureTemplate loader. Besides reading both index
    // layers, it performs the format-version block-state upgrade and resolves
    // blocks through the active world's palette/unknown-block registry. Calling
    // Block::tryGetFromRegistry() directly skips that official load pipeline and
    // can turn valid legacy states (notably water and doors) into unknown blocks.
    // StructureTemplate::create() internally uses ll::service::getLevel(),
    // which only exists for the integrated (local) server and returns null on
    // remote server connections. Build the template against the client
    // multiplayer Level instead; it exists in local worlds and on servers.
    auto const clientLevel = ll::service::getMultiPlayerLevel();
    if (!clientLevel) {
        error = "尚未进入世界，无法解析结构";
        return nullptr;
    }
    auto nativeStructure = std::make_unique<StructureTemplate>(
        "lholo:projection",
        clientLevel->getUnknownBlockTypeRegistry()
    );
    if (!nativeStructure->load(*root)) {
        error = "原版 StructureTemplate 无法加载该结构";
        return nullptr;
    }
    auto const& nativeData = nativeStructure->mStructureTemplateData.get();
    auto const& nativeSize = nativeData.mSize.get();
    if (nativeSize.x != loaded->sizeX || nativeSize.y != loaded->sizeY || nativeSize.z != loaded->sizeZ) {
        error = "原版 StructureTemplate 返回的尺寸与文件不一致";
        return nullptr;
    }
    auto const* nativePalette = nativeData.getPalette(StructureTemplateData::DEFAULT_PALETTE_NAME());
    if (!nativePalette) {
        error = "原版 StructureTemplate 缺少 default palette";
        return nullptr;
    }
    auto const& nativePrimary = nativeData.mBlockIndices.get();
    auto const& nativeSecondaryStorage = nativeData.mExtraBlockIndices.get();
    // Since 26.51 mExtraBlockIndices is std::optional: the vanilla loader
    // collapses the second layer into an empty value whenever it holds nothing
    // but NO_BLOCK_INDEX_VALUE entries. That reports "no water or waterlogged
    // blocks in this structure", not corruption, so the layer must be treated
    // as empty rather than as a load failure. The file's own second layer was
    // already validated above, so an absent layer can only be legitimate when
    // that raw pass counted no occupied cells; anything else stays inconsistent
    // and is rejected by the size check below.
    if (nativePrimary.size() != loaded->volume
        || (!nativeSecondaryStorage && loaded->secondaryBlocks > 0)
        || (nativeSecondaryStorage && nativeSecondaryStorage->size() != loaded->volume)) {
        error = "原版 StructureTemplate 的方块索引数量与结构体积不一致";
        return nullptr;
    }
    auto const unknownRegistry = nativeStructure->mUnknownBlockRegistry.get();
    auto const resolveNative = [&](int paletteIndex) -> Block const* {
        if (paletteIndex < 0) return nullptr;
        auto const* resolved = nativePalette->tryGetBlock(
            static_cast<std::uint64_t>(paletteIndex), unknownRegistry
        );
        return resolved && !resolved->isAir() ? resolved : nullptr;
    };
    loaded->renderBlocks.reserve(static_cast<std::size_t>(loaded->primaryBlocks + loaded->secondaryBlocks));
    auto const yz = static_cast<std::uint64_t>(loaded->sizeY) * static_cast<std::uint64_t>(loaded->sizeZ);
    for (std::uint64_t index = 0; index < loaded->volume; ++index) {
        auto const* primary = resolveNative(nativePrimary[static_cast<std::size_t>(index)]);
        auto const* secondary = nativeSecondaryStorage
            ? resolveNative((*nativeSecondaryStorage)[static_cast<std::size_t>(index)])
            : nullptr;
        Block const* block{};
        Block const* liquid{};
        auto const assign = [&](Block const* value) {
            if (!value) return;
            if (value->getBlockType().mMaterial.get().mLiquid) liquid = value;
            else if (!block) block = value;
        };
        assign(primary);
        assign(secondary);
        if (primary && primary->getTypeName() == "minecraft:bubble_column") {
            block = primary;
            liquid = (secondary && secondary->getBlockType().mMaterial.get().mLiquid)
                ? secondary : nullptr;
        } else if (secondary && secondary->getTypeName() == "minecraft:bubble_column") {
            block = secondary;
            liquid = (primary && primary->getBlockType().mMaterial.get().mLiquid)
                ? primary : nullptr;
        }
        if (!block && !liquid) continue;
        auto const x = index / yz;
        auto const remainder = index % yz;
        auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);
        std::shared_ptr<CompoundTag const> blockEntityNbt;
        if (blockPositionData) {
            auto const* positionData = findCompound(*blockPositionData, std::to_string(index));
            auto const* entityData = positionData
                ? findCompound(*positionData, "block_entity_data") : nullptr;
            if (entityData) blockEntityNbt = std::make_shared<CompoundTag const>(*entityData);
        }
        loaded->renderBlocks.push_back({
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z), block, liquid,
            std::move(blockEntityNbt)
        });
    }
    assignMaterialIndices(*loaded);
    loaded->renderBlocks.packFromValues(loaded->sizeX,loaded->sizeY,loaded->sizeZ);
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = path;
    return loaded;
}

std::shared_ptr<LoadedStructure> loadLitematic(std::filesystem::path const& path, std::string& error) try {
    if (!ll::service::getMultiPlayerLevel()) {
        error = "尚未进入世界，无法解析结构";
        return nullptr;
    }

    // The Java-to-Bedrock mapper caches non-owning Block pointers while resolving one
    // palette. Minecraft rebuilds those registry-owned permutations across a
    // world teardown, so a cache from the previous world must never be reused.
    resetJavaBlockMappingCache();

    auto compressed = readFile(path, error);
    if (!compressed) return nullptr;
    auto bytes = inflateGzip(*compressed, error);
    if (!bytes) return nullptr;

    auto root = JavaNbtReader{*bytes}.readRoot();
    auto const* storedFormatVersion = javaValue<std::int32_t>(root, "Version");
    int const litematicVersion = storedFormatVersion ? *storedFormatVersion : 0;
    auto const* storedDataVersion = javaValue<std::int32_t>(root, "MinecraftDataVersion");
    int const javaDataVersion = storedDataVersion ? *storedDataVersion : 0;
    auto const* regions = javaValue<JavaNbtTag::Compound>(root, "Regions");
    if (!regions || regions->empty()) {
        error = "Litematic 缺少 Regions 或没有区域";
        return nullptr;
    }

    struct Region {
        int posX{}, posY{}, posZ{};
        int signedX{}, signedY{}, signedZ{};
        int sizeX{}, sizeY{}, sizeZ{};
        std::string name;
        std::vector<ResolvedJavaBlock> palette;
        JavaNbtTag::LongArray const* states{};
        std::unordered_map<std::uint64_t, JavaNbtTag::Compound const*> blockEntities;
    };
    std::vector<Region> parsedRegions;
    parsedRegions.reserve(regions->size());
    std::int64_t minX = std::numeric_limits<std::int64_t>::max();
    std::int64_t minY = minX;
    std::int64_t minZ = minX;
    std::int64_t maxX = std::numeric_limits<std::int64_t>::min();
    std::int64_t maxY = maxX;
    std::int64_t maxZ = maxX;
    std::uint64_t paletteEntries{};

    for (auto const* entry : namedEntriesInOrder(*regions)) {
        auto const& [regionName, regionTag] = *entry;
        auto const* compound = std::get_if<JavaNbtTag::Compound>(&regionTag.value);
        if (!compound) continue;
        Region region;
        region.name=regionName;
        if (!readJavaVec3(*compound, "Position", region.posX, region.posY, region.posZ)
            || !readJavaVec3(*compound, "Size", region.signedX, region.signedY, region.signedZ)
            || region.signedX == 0 || region.signedY == 0 || region.signedZ == 0
            || region.signedX == std::numeric_limits<int>::min()
            || region.signedY == std::numeric_limits<int>::min()
            || region.signedZ == std::numeric_limits<int>::min()) {
            error = "Litematic 区域 " + regionName + " 的 Position/Size 无效";
            return nullptr;
        }
        region.sizeX = std::abs(region.signedX);
        region.sizeY = std::abs(region.signedY);
        region.sizeZ = std::abs(region.signedZ);
        auto const* storedRegionDataVersion = javaValue<std::int32_t>(*compound, "DataVersion");
        int const regionDataVersion = storedRegionDataVersion ? *storedRegionDataVersion : javaDataVersion;
        auto const* palette = javaValue<JavaNbtTag::List>(*compound, "BlockStatePalette");
        region.states = javaValue<JavaNbtTag::LongArray>(*compound, "BlockStates");
        if (!palette || palette->empty() || !region.states || region.states->empty()) {
            error = "Litematic 区域 " + regionName + " 缺少方块调色板或 BlockStates";
            return nullptr;
        }
        region.palette.reserve(palette->size());
        for (auto const& entry : *palette) {
            auto const resolved = resolveJavaBlock(entry, regionDataVersion);
            if (resolved.stateResolutionFailed) {
                error = "Unresolved Java block state in Litematic region " + regionName
                    + " at palette index " + std::to_string(region.palette.size());
                return nullptr;
            }
            region.palette.push_back(resolved);
        }
        paletteEntries += palette->size();

        if (auto const* blockEntities = javaValue<JavaNbtTag::List>(*compound, "TileEntities")) {
            region.blockEntities.reserve(blockEntities->size());
            for (auto const& entry : *blockEntities) {
                auto const* wrapper = std::get_if<JavaNbtTag::Compound>(&entry.value);
                if (!wrapper) continue;
                auto const* x = javaValue<std::int32_t>(*wrapper, "x");
                auto const* y = javaValue<std::int32_t>(*wrapper, "y");
                auto const* z = javaValue<std::int32_t>(*wrapper, "z");
                if (!x || !y || !z || *x < 0 || *y < 0 || *z < 0
                    || *x >= region.sizeX || *y >= region.sizeY || *z >= region.sizeZ) {
                    continue;
                }
                auto const* entity = litematicVersion == 1
                    ? javaValue<JavaNbtTag::Compound>(*wrapper, "TileNBT") : wrapper;
                if (!entity || entity->empty()) continue;
                region.blockEntities.insert_or_assign(
                    localCellIndex(*x, *y, *z, region.sizeY, region.sizeZ), entity
                );
            }
        }

        auto const endX = static_cast<std::int64_t>(region.posX)
            + (region.signedX < 0 ? -(static_cast<std::int64_t>(region.sizeX) - 1) : region.sizeX - 1);
        auto const endY = static_cast<std::int64_t>(region.posY)
            + (region.signedY < 0 ? -(static_cast<std::int64_t>(region.sizeY) - 1) : region.sizeY - 1);
        auto const endZ = static_cast<std::int64_t>(region.posZ)
            + (region.signedZ < 0 ? -(static_cast<std::int64_t>(region.sizeZ) - 1) : region.sizeZ - 1);
        minX = std::min({minX, static_cast<std::int64_t>(region.posX), endX});
        minY = std::min({minY, static_cast<std::int64_t>(region.posY), endY});
        minZ = std::min({minZ, static_cast<std::int64_t>(region.posZ), endZ});
        maxX = std::max({maxX, static_cast<std::int64_t>(region.posX), endX});
        maxY = std::max({maxY, static_cast<std::int64_t>(region.posY), endY});
        maxZ = std::max({maxZ, static_cast<std::int64_t>(region.posZ), endZ});
        parsedRegions.push_back(std::move(region));
    }
    if (parsedRegions.empty()) {
        error = "Litematic 没有可加载的区域";
        return nullptr;
    }

    auto const extentX = maxX - minX + 1;
    auto const extentY = maxY - minY + 1;
    auto const extentZ = maxZ - minZ + 1;
    if (extentX <= 0 || extentY <= 0 || extentZ <= 0
        || extentX > std::numeric_limits<int>::max()
        || extentY > std::numeric_limits<int>::max()
        || extentZ > std::numeric_limits<int>::max()) {
        error = "Litematic 合并后的结构尺寸无效或过大";
        return nullptr;
    }

    auto loaded = std::make_shared<LoadedStructure>();
    loaded->sizeX = static_cast<int>(extentX);
    loaded->sizeY = static_cast<int>(extentY);
    loaded->sizeZ = static_cast<int>(extentZ);
    if (!checkedVolume3(
            static_cast<std::uint64_t>(extentX),
            static_cast<std::uint64_t>(extentY),
            static_cast<std::uint64_t>(extentZ),
            loaded->volume
        )) {
        error = "Litematic merged volume overflow";
        return nullptr;
    }
    loaded->paletteEntries = paletteEntries;
    loaded->regions.reserve(parsedRegions.size());
    loaded->regionRenderBlocks.reserve(parsedRegions.size());
    struct MergedJavaCell {
        ResolvedJavaBlock                 block;
        std::shared_ptr<CompoundTag const> blockEntityNbt;
    };
    std::unordered_map<std::uint64_t, MergedJavaCell> mergedBlocks;

    for (auto const& region : parsedRegions) {
        std::uint64_t regionVolume{};
        if (!checkedVolume3(
                static_cast<std::uint64_t>(region.sizeX),
                static_cast<std::uint64_t>(region.sizeY),
                static_cast<std::uint64_t>(region.sizeZ),
                regionVolume
            )) {
            error = "Litematic region volume overflow";
            return nullptr;
        }
        // Litematica stores BlockStates from the region's minimum corner even
        // when Size is negative. The sign only records which selection corner
        // is Position; it must not mirror the block data.
        auto const regionMinX = static_cast<std::int64_t>(region.posX)
            - (region.signedX < 0 ? static_cast<std::int64_t>(region.sizeX) - 1 : 0);
        auto const regionMinY = static_cast<std::int64_t>(region.posY)
            - (region.signedY < 0 ? static_cast<std::int64_t>(region.sizeY) - 1 : 0);
        auto const regionMinZ = static_cast<std::int64_t>(region.posZ)
            - (region.signedZ < 0 ? static_cast<std::int64_t>(region.sizeZ) - 1 : 0);
        loaded->regions.push_back({
            static_cast<int>(regionMinX - minX),
            static_cast<int>(regionMinY - minY),
            static_cast<int>(regionMinZ - minZ),
            region.sizeX,
            region.sizeY,
            region.sizeZ,
            region.name
        });
        loaded->regionRenderBlocks.emplace_back();
        auto const bits = std::max<unsigned>(
            2u,
            static_cast<unsigned>(std::bit_width(static_cast<unsigned>(region.palette.size() - 1)))
        );
        if (bits == 0
            || regionVolume > (std::numeric_limits<std::uint64_t>::max() - 63ULL) / bits) {
            error = "Litematic BlockStates bit count overflow";
            return nullptr;
        }
        auto const requiredLongs = (regionVolume * bits + 63ULL) / 64ULL;
        if (requiredLongs > region.states->size()) {
            error = "Litematic 的 BlockStates 数量与区域尺寸不匹配";
            return nullptr;
        }
        for (std::uint64_t index = 0; index < regionVolume; ++index) {
            auto const paletteIndex = packedPaletteIndex(*region.states, index, bits);
            if (paletteIndex >= region.palette.size()) {
                error = "Litematic BlockStates palette index is out of range";
                return nullptr;
            }
            auto const& resolved = region.palette[paletteIndex];
            if (!resolved.block && !resolved.liquid) continue;
            auto const layer = static_cast<std::uint64_t>(region.sizeX) * region.sizeZ;
            auto const localY = index / layer;
            auto const remainder = index % layer;
            auto const localZ = remainder / static_cast<std::uint64_t>(region.sizeX);
            auto const localX = remainder % static_cast<std::uint64_t>(region.sizeX);
            auto const worldX = regionMinX + static_cast<std::int64_t>(localX);
            auto const worldY = regionMinY + static_cast<std::int64_t>(localY);
            auto const worldZ = regionMinZ + static_cast<std::int64_t>(localZ);
            auto const x = static_cast<std::uint64_t>(worldX - minX);
            auto const y = static_cast<std::uint64_t>(worldY - minY);
            auto const z = static_cast<std::uint64_t>(worldZ - minZ);
            auto const mergedIndex = (x * static_cast<std::uint64_t>(loaded->sizeY) + y)
                * static_cast<std::uint64_t>(loaded->sizeZ) + z;
            std::shared_ptr<CompoundTag const> blockEntityNbt;
            auto const blockEntity = region.blockEntities.find(
                localCellIndex(
                    static_cast<int>(localX), static_cast<int>(localY), static_cast<int>(localZ),
                    region.sizeY, region.sizeZ
                )
            );
            if (resolved.block && blockEntity != region.blockEntities.end()) {
                auto const actorType = resolved.block->getBlockType().getBlockEntityType();
                if (actorType == BlockActorType::Sign || actorType == BlockActorType::HangingSign) {
                    if (auto const signData = readJavaSignBlockEntity(*blockEntity->second)) {
                        blockEntityNbt = convertJavaSignBlockEntity(
                            *signData,
                            actorType == BlockActorType::HangingSign
                                ? JavaSignBlockEntityKind::HangingSign : JavaSignBlockEntityKind::Sign,
                            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)
                        );
                    }
                }
            }
            loaded->regionRenderBlocks.back().push_back({
                static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
                resolved.block, resolved.liquid, blockEntityNbt
            });
            mergedBlocks.insert_or_assign(
                mergedIndex, MergedJavaCell{resolved, std::move(blockEntityNbt)}
            );
        }
    }
    loaded->renderBlocks.reserve(mergedBlocks.size());
    auto const yz = static_cast<std::uint64_t>(loaded->sizeY) * loaded->sizeZ;
    for (auto& [index, cell] : mergedBlocks) {
        auto const x = index / yz;
        auto const remainder = index % yz;
        auto const y = remainder / static_cast<std::uint64_t>(loaded->sizeZ);
        auto const z = remainder % static_cast<std::uint64_t>(loaded->sizeZ);
        // The Java -> Bedrock mapper already split each cell into its body and
        // liquid layers (pure liquids, and the water that waterlogged blocks
        // carry), matching the two-layer semantics of the .mcstructure path.
        loaded->renderBlocks.push_back({
            static_cast<int>(x), static_cast<int>(y), static_cast<int>(z),
            cell.block.block, cell.block.liquid, std::move(cell.blockEntityNbt)
        });
    }
    std::sort(loaded->renderBlocks.mutableValues().begin(), loaded->renderBlocks.mutableValues().end(), [](auto const& left, auto const& right) {
        return std::tie(left.x, left.y, left.z) < std::tie(right.x, right.y, right.z);
    });
    loaded->primaryBlocks = loaded->renderBlocks.size();
    assignMaterialIndices(*loaded);
    loaded->renderBlocks.packFromValues(loaded->sizeX,loaded->sizeY,loaded->sizeZ);
    loaded->generation = gGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    loaded->sourcePath = path;
    return loaded;
} catch (std::exception const& exception) {
    error = exception.what();
    return nullptr;
}

} // namespace

std::shared_ptr<LoadedStructure> makeCachedRegionView(
    std::shared_ptr<LoadedStructure const> source) {
    if(!source) return {};
    auto view=std::make_shared<LoadedStructure>();
    view->sourcePath=source->sourcePath;
    view->sourceWorldOrigin=source->sourceWorldOrigin;
    view->sizeX=source->sizeX;
    view->sizeY=source->sizeY;
    view->sizeZ=source->sizeZ;
    view->volume=source->volume;
    view->primaryBlocks=source->primaryBlocks;
    view->secondaryBlocks=source->secondaryBlocks;
    view->paletteEntries=source->paletteEntries;
    view->materialCount=source->materialCount;
    view->sourceRegions=source->sourceRegions.empty()?source->regions:source->sourceRegions;
    view->regions=source->regions;
    view->sourceBacking=std::move(source);
    view->generation=gGeneration.fetch_add(1,std::memory_order_acq_rel)+1;
    return view;
}
void filterLoadedRegions(LoadedStructure& loaded,std::vector<RegionSetting> const& settings) {
    auto const& source=loaded.sourceBacking?*loaded.sourceBacking:loaded;
    if(loaded.sourceRegions.empty())loaded.sourceRegions=source.regions;
    if(settings.empty()){
        if(loaded.sourceBacking)loaded.renderBlocks=source.renderBlocks;
        return;
    }
    auto enabled=[&](std::string const& key){
        auto it=std::find_if(settings.begin(),settings.end(),
            [&](RegionSetting const& r){return r.name==key;});
        return it==settings.end() || it->enabled;
    };
    // User-defined AABBs partition a single mcstructure into disjoint source
    // tiles. When a custom region overlaps another, the newest wins. The
    // remaining cells belong to the implicit "main" region.
    bool const canSplit=loaded.sourceRegions.size()==1 &&
        loaded.sourceRegions.front().name=="main" &&
        loaded.regionRenderBlocks.empty();
    std::vector<RegionSetting const*> custom;
    if(canSplit)for(auto const& setting:settings){
        if(!setting.area)continue;
        if(custom.size()>=4)break;
        auto const& b=*setting.area;
        if(b.x<0 || b.y<0 || b.z<0 ||
            b.sizeX<1 || b.sizeY<1 || b.sizeZ<1 ||
            static_cast<std::int64_t>(b.x)+b.sizeX>loaded.sizeX ||
            static_cast<std::int64_t>(b.y)+b.sizeY>loaded.sizeY ||
            static_cast<std::int64_t>(b.z)+b.sizeZ>loaded.sizeZ)continue;
        custom.push_back(&setting);
    }
    if(!custom.empty()) {
        auto ownerAt=[&](int x,int y,int z)->std::string const& {
            for(auto it=custom.rbegin();it!=custom.rend();++it){
                auto const& r=**it;auto const& b=*r.area;
                if(x>=b.x && x<b.x+b.sizeX && y>=b.y && y<b.y+b.sizeY
                    && z>=b.z && z<b.z+b.sizeZ)return r.name;
            }
            return loaded.sourceRegions.front().name;
        };
        // Partition the complete source cuboid at all custom boundaries;
        // unlike a bounding-box mask, each resulting box accurately models
        // area coverage and 'extra block' verification excludes disabled parts.
        std::vector<int> xCuts{0,loaded.sizeX},yCuts{0,loaded.sizeY},zCuts{0,loaded.sizeZ};
        for(auto const* r:custom){
            auto const& b=*r->area;
            xCuts.push_back(b.x);xCuts.push_back(b.x+b.sizeX);
            yCuts.push_back(b.y);yCuts.push_back(b.y+b.sizeY);
            zCuts.push_back(b.z);zCuts.push_back(b.z+b.sizeZ);
        }
        auto order=[](std::vector<int>& v){
            std::sort(v.begin(),v.end());
            v.erase(std::unique(v.begin(),v.end()),v.end());
        };
        order(xCuts);order(yCuts);order(zCuts);
        std::vector<LoadedStructure::RegionBox> tiles;
        for(std::size_t yi=1;yi<yCuts.size();++yi)
            for(std::size_t zi=1;zi<zCuts.size();++zi) {
                // Join adjacent same-owner tiles along X to keep event
                // interest/extra scans compact even for several regions.
                std::size_t xi=1;
                while(xi<xCuts.size()){
                    int const x=xCuts[xi-1], y=yCuts[yi-1],z=zCuts[zi-1];
                    auto const& name=ownerAt(x,y,z);
                    std::size_t end=xi;
                    while(end+1<xCuts.size() && ownerAt(xCuts[end],y,z)==name)++end;
                    if(enabled(name))tiles.push_back({
                        x,y,z,xCuts[end]-x,yCuts[yi]-y,zCuts[zi]-z,name
                    });
                    xi=end+1;
                }
            }
        loaded.regions=std::move(tiles);
        if(loaded.sourceBacking)loaded.renderBlocks=source.renderBlocks;
        std::erase_if(loaded.renderBlocks.mutableValues(),[&](LoadedStructure::RenderBlock const& b){
            return !enabled(ownerAt(b.x,b.y,b.z));
        });
        loaded.primaryBlocks=loaded.renderBlocks.size();
        assignMaterialIndices(loaded);
        loaded.renderBlocks.packFromValues(loaded.sizeX,loaded.sizeY,loaded.sizeZ);
        return;
    }
    // File-native subregions: preserve the original source order and original
    // cell values. A disabled later region reveals earlier overlapping cells.
    bool anyDisabled=false;
    std::vector<LoadedStructure::RegionBox> active;
    active.reserve(loaded.sourceRegions.size());
    for(auto const& region:loaded.sourceRegions){
        if(enabled(region.name))active.push_back(region);
        else anyDisabled=true;
    }
    if(!anyDisabled){
        if(loaded.sourceBacking)loaded.renderBlocks=source.renderBlocks;
        return;
    }
    // Compact disjoint-source path: keep only a byte of owner metadata per
    // original block, instead of a duplicate vector of native render blocks.
    // The source is already globally sorted. Filtering preserves that order,
    // removing both hash-map rebuilds and repeated full-vector sorts.
    if(!source.disjointSourceOwners.empty() &&
       source.disjointSourceOwners.size()==source.renderBlocks.size()) {
        std::vector<bool> activeMask;
        activeMask.reserve(loaded.sourceRegions.size());
        for(auto const& r:loaded.sourceRegions)
            activeMask.push_back(enabled(r.name));
        auto const& owners=source.disjointSourceOwners;
        std::size_t count{};
        for(auto const owner:owners)
            if(owner<activeMask.size() && activeMask[owner])++count;
        loaded.regions=std::move(active);
        loaded.renderBlocks.clear();
        loaded.renderBlocks.reserve(count);
        for(std::size_t i=0;i<owners.size();++i)
            if(owners[i]<activeMask.size() && activeMask[owners[i]])
                loaded.renderBlocks.push_back(source.renderBlocks[i]);
        loaded.primaryBlocks=loaded.renderBlocks.size();
        assignMaterialIndices(loaded);
        loaded.renderBlocks.packFromValues(loaded.sizeX,loaded.sizeY,loaded.sizeZ);
        return;
    }
    auto const& regionCells=source.regionRenderBlocks;
    if(loaded.sourceRegions.size()!=regionCells.size()) {
        if(loaded.sourceRegions.size()!=1)throw std::runtime_error(
            "Missing region source ownership; refusing partial filter");
        if(active.empty()){
            loaded.renderBlocks.clear();loaded.primaryBlocks=0;
            loaded.regions.clear();loaded.materialCount=0;
        }
        return;
    }
    // Most composite .lholo archives are physically disjoint tiles. AABB
    // disjointness proves per-cell uniqueness; skip O(N) hash-node allocation
    // and ownership reconstruction, reserving just the selected block count.
    // Overlapping regions retain the original last-region-wins fallback.
    std::size_t selectedCells{};
    for(std::size_t i=0;i<loaded.sourceRegions.size();++i)
        if(enabled(loaded.sourceRegions[i].name))
            selectedCells+=regionCells[i].size();
    bool const disjoint=disjointRegionBounds(active);
    loaded.regions=std::move(active);
    loaded.renderBlocks.clear();
    loaded.renderBlocks.reserve(selectedCells);
    if(disjoint) {
        for(std::size_t i=0;i<loaded.sourceRegions.size();++i) {
            if(!enabled(loaded.sourceRegions[i].name))continue;
            loaded.renderBlocks.mutableValues().insert(loaded.renderBlocks.mutableValues().end(),
                regionCells[i].begin(),regionCells[i].end());
        }
    }else {
        std::unordered_map<std::uint64_t,LoadedStructure::RenderBlock> merged;
        merged.reserve(selectedCells);
        for(std::size_t i=0;i<loaded.sourceRegions.size();++i) {
            if(!enabled(loaded.sourceRegions[i].name))continue;
            for(auto const& cell:regionCells[i]) {
                auto const key=(static_cast<std::uint64_t>(cell.x)*loaded.sizeY+
                    static_cast<std::uint64_t>(cell.y))*loaded.sizeZ+
                    static_cast<std::uint64_t>(cell.z);
                merged.insert_or_assign(key,cell);
            }
        }
        for(auto& [key,cell]:merged)
            loaded.renderBlocks.push_back(std::move(cell));
    }
    std::sort(loaded.renderBlocks.mutableValues().begin(),loaded.renderBlocks.mutableValues().end(),[](auto const& a,auto const& b){
        return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z);
    });
    loaded.primaryBlocks=loaded.renderBlocks.size();
    assignMaterialIndices(loaded);
}
// Merge native Bedrock schematics using their saved world origins. Each part
// retains its original blocks as a named region, so the per-region visibility
// settings can rebuild the effective structure without Java block conversion.
std::shared_ptr<LoadedStructure> finalizeCompositeMcstructure(
    std::shared_ptr<PreparedStructureLoad> const& prepared,std::string& error
) {
    if(!prepared || !prepared->composite || prepared->components.empty()) {
        error="Invalid composite preparation";return nullptr;
    }
    std::array<std::int64_t,3> minimum{
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::max(),
        std::numeric_limits<std::int64_t>::max()
    }, maximum{
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::min()
    };
    struct Bounds {
        std::array<int,3> origin;
        std::array<int,3> size;
    };
    std::vector<Bounds> bounds;
    bounds.reserve(prepared->components.size());
    // Calculate the union BEFORE native finalization, which avoids an extra
    // pass and prevents a partially published merged schematic.
    for(auto const& part:prepared->components) {
        auto const origin=part.prepared && part.prepared->root
            ? readMcstructureWorldOrigin(*part.prepared->root):std::nullopt;
        if(!origin) {
            error="Missing world origin in "+part.name;return nullptr;
        }
        auto const& source=*part.prepared;
        std::array<int,3> dims{source.sizeX,source.sizeY,source.sizeZ};
        for(int axis=0;axis<3;++axis) {
            if(dims[axis]<=0){error="Invalid component size";return nullptr;}
            auto const lo=static_cast<std::int64_t>((*origin)[axis]);
            auto const hi=lo+static_cast<std::int64_t>(dims[axis]);
            minimum[axis]=std::min(minimum[axis],lo);
            maximum[axis]=std::max(maximum[axis],hi);
        }
        bounds.push_back({*origin,dims});
    }
    std::array<int,3> combined{};
    for(int axis=0;axis<3;++axis) {
        auto const length=maximum[axis]-minimum[axis];
        // Fail closed on extreme/unreasonable manifests rather than allocating
        // many gigabytes on the Minecraft render/main thread.
        if(length<=0 || length>2048
            || minimum[axis]<std::numeric_limits<int>::min()
            || minimum[axis]>std::numeric_limits<int>::max()){
            error="Composite schematic extent outside safe limits";
            return nullptr;
        }
        combined[axis]=static_cast<int>(length);
    }
    std::uint64_t volume{};
    if(!checkedVolume3(combined[0],combined[1],combined[2],volume)
        || volume>16000000ULL) {
        error="Composite schematic exceeds 16 million cells";
        return nullptr;
    }
    auto result=std::make_shared<LoadedStructure>();
    result->sizeX=combined[0];result->sizeY=combined[1];result->sizeZ=combined[2];
    result->volume=volume;result->sourcePath=prepared->path;
    result->sourceWorldOrigin=std::array<int,3>{
        static_cast<int>(minimum[0]),static_cast<int>(minimum[1]),
        static_cast<int>(minimum[2])
    };
    result->regions.reserve(bounds.size());
    result->regionRenderBlocks.reserve(bounds.size());
    std::size_t totalBlocks{};
    for(std::size_t i=0;i<prepared->components.size();++i){
        auto const& part=prepared->components[i];
        std::string childError;
        auto loaded=finalizeMcstructure(part.prepared,childError);
        if(!loaded) {
            error="Cannot resolve region "+part.name+": "+childError;
            return nullptr;
        }
        std::array<int,3> offsets{};
        for(int axis=0;axis<3;++axis)
            offsets[axis]=static_cast<int>(
                static_cast<std::int64_t>(bounds[i].origin[axis])-minimum[axis]);
        result->regions.push_back({
            offsets[0],offsets[1],offsets[2],
            loaded->sizeX,loaded->sizeY,loaded->sizeZ,part.name
        });
        if(loaded->renderBlocks.size()>6000000-totalBlocks) {
            error="Composite schematic exceeds six million blocks";
            return nullptr;
        }
        totalBlocks+=loaded->renderBlocks.size();
        result->paletteEntries+=loaded->paletteEntries;
        result->secondaryBlocks+=loaded->secondaryBlocks;
        auto& records=result->regionRenderBlocks.emplace_back(
            std::move(loaded->renderBlocks.mutableValues()));
        for(auto& block:records){
            block.x+=offsets[0];
            block.y+=offsets[1];
            block.z+=offsets[2];
        }
    }
    result->sourceRegions=result->regions;
    bool anyOverlap=false;
    for(std::size_t i=0;i<result->regions.size() && !anyOverlap;++i)
        for(std::size_t j=0;j<i;++j){
            auto const& a=result->regions[i];
            auto const& b=result->regions[j];
            if(a.x < b.x+b.sizeX && b.x < a.x+a.sizeX
                && a.y < b.y+b.sizeY && b.y < a.y+a.sizeY
                && a.z < b.z+b.sizeZ && b.z < a.z+a.sizeZ){
                anyOverlap=true;break;
            }
        }
    if(!anyOverlap) {
        result->renderBlocks.reserve(totalBlocks);
        for(auto const& region:result->regionRenderBlocks)
            result->renderBlocks.mutableValues().insert(
                result->renderBlocks.mutableValues().end(),region.begin(),region.end());
    }else{
        // File order is authoritative when two region bounds overlap.
        std::unordered_map<std::uint64_t,LoadedStructure::RenderBlock> latest;
        latest.reserve(totalBlocks);
        for(auto const& region:result->regionRenderBlocks)
            for(auto const& block:region){
                auto const key=(static_cast<std::uint64_t>(block.x)*result->sizeY
                    +static_cast<std::uint64_t>(block.y))*result->sizeZ
                    +static_cast<std::uint64_t>(block.z);
                latest.insert_or_assign(key,block);
            }
        result->renderBlocks.reserve(latest.size());
        for(auto& [key,value]:latest)
            result->renderBlocks.push_back(std::move(value));
    }
    std::sort(result->renderBlocks.mutableValues().begin(),result->renderBlocks.mutableValues().end(),
        [](auto const& a,auto const& b){
            return std::tie(a.x,a.y,a.z)<std::tie(b.x,b.y,b.z);
        });
    result->primaryBlocks=result->renderBlocks.size();
    assignMaterialIndices(*result);
    result->generation=gGeneration.fetch_add(1,std::memory_order_acq_rel)+1;
    return result;
}

bool supportsAsyncStructurePreparation(std::filesystem::path const& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    return extension == L".mcstructure" || extension == L".lholo";
}

std::shared_ptr<PreparedStructureLoad> prepareStructureFile(
    std::filesystem::path const& path,
    std::string&                 error
) {
    if (!supportsAsyncStructurePreparation(path)) {
        error = "该格式暂不支持异步预解析";
        return nullptr;
    }
    auto extension=path.extension().wstring();
    std::transform(extension.begin(),extension.end(),extension.begin(),
        [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    if(extension==L".mcstructure")return prepareMcstructure(path,error);
    try {
        std::error_code ec;
        if(!std::filesystem::is_regular_file(path,ec)
            || std::filesystem::file_size(path,ec)>262144 || ec){
            error="Invalid LHolo region manifest file";
            return nullptr;
        }
        std::ifstream in(path,std::ios::binary);
        if(!in){error="Cannot open LHolo region manifest";return nullptr;}
        auto manifest=nlohmann::json::parse(in);
        if(!manifest.is_object() || manifest.value("format",std::string{})!="lholo-regions"
            || manifest.value("version",0)!=1 || !manifest.contains("regions")
            || !manifest["regions"].is_array() || manifest["regions"].empty()
            || manifest["regions"].size()>64){
            error="Invalid LHolo region manifest schema";
            return nullptr;
        }
        auto prepared=std::make_shared<PreparedStructureLoad>();
        prepared->path=path;
        prepared->composite=true;
        std::set<std::string> names;
        std::set<std::filesystem::path> files;
        for(auto const& part:manifest["regions"]) {
            if(!part.is_object() || !part.contains("name") || !part["name"].is_string()
                || !part.contains("file") || !part["file"].is_string()){
                error="Invalid LHolo region entry";return nullptr;
            }
            auto name=part["name"].get<std::string>();
            auto relative=part["file"].get<std::string>();
            if(name.empty() || name.size()>128 || !names.insert(name).second
                || !safeSchematicPath(relative) ){
                error="Invalid/duplicate LHolo region name or file";return nullptr;
            }
            auto partPath=resolveSchematicPath(path.parent_path(),relative);
            auto ext=partPath.extension().wstring();
            std::transform(ext.begin(),ext.end(),ext.begin(),
                [](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
            if(ext!=L".mcstructure" || !files.insert(partPath).second){
                error="LHolo manifest requires unique mcstructure components";return nullptr;
            }
            std::string partError;
            auto child=prepareMcstructure(partPath,partError);
            if(!child) {
                error="LHolo region "+name+": "+partError;
                return nullptr;
            }
            auto actualOrigin=readMcstructureWorldOrigin(*child->root);
            if(!actualOrigin) {
                error="LHolo region "+name+" has no valid structure_world_origin";
                return nullptr;
            }
            // Manifest captures the download's original coordinates. Fail
            // when files were accidentally substituted after the manifest
            // was generated, instead of silently producing a scrambled build.
            if(part.contains("origin")) {
                auto const& expected=part["origin"];
                if(!expected.is_array() || expected.size()!=3
                    || !std::all_of(expected.begin(),expected.end(),
                        [](auto const& n){return n.is_number_integer();})
                    || expected!=nlohmann::json::array({
                        (*actualOrigin)[0],(*actualOrigin)[1],(*actualOrigin)[2]
                    })) {
                    error="LHolo region "+name+" origin does not match manifest";
                    return nullptr;
                }
            }
            if(part.contains("size")) {
                auto const& expected=part["size"];
                if(!expected.is_array() || expected.size()!=3
                    || !std::all_of(expected.begin(),expected.end(),
                        [](auto const& n){return n.is_number_integer();})
                    || expected!=nlohmann::json::array({
                        child->sizeX,child->sizeY,child->sizeZ
                    })) {
                    error="LHolo region "+name+" size does not match manifest";
                    return nullptr;
                }
            }
            prepared->components.push_back({std::move(name),std::move(child)});
        }
        return prepared;
    }catch(std::exception const& e){
        error=std::string("Invalid LHolo region manifest: ")+e.what();
        return nullptr;
    }
}

std::shared_ptr<LoadedStructure> finalizePreparedStructureFile(
    std::shared_ptr<PreparedStructureLoad> prepared,
    std::string&                          error
) try {
    auto loaded = prepared && prepared->composite
        ? finalizeCompositeMcstructure(prepared,error)
        : finalizeMcstructure(prepared, error);
    if (loaded && loaded->renderBlocks.empty()) {
        error = "结构中没有可投影方块";
        return nullptr;
    }
    return loaded;
} catch (std::exception const& exception) {
    error = std::string{"结构 finalization 失败: "} + exception.what();
    return nullptr;
} catch (...) {
    error = "结构 finalization 发生未知异常";
    return nullptr;
}

std::shared_ptr<LoadedStructure> loadStructureFile(
    std::filesystem::path const& path,
    std::string&                 error
) try {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) {
        return static_cast<wchar_t>(std::towlower(value));
    });
    std::shared_ptr<LoadedStructure> loaded;
    if (extension == L".litematic") loaded = loadLitematic(path, error);
    else if (extension == L".mcstructure") loaded = loadMcstructure(path, error);
    else if (extension == L".lholo") {
        auto prepared=prepareStructureFile(path,error);
        if(!prepared)return nullptr;
        loaded=finalizePreparedStructureFile(std::move(prepared),error);
    }
    else {
        error = "不支持的文件格式，请选择 .mcstructure 或 .litematic";
        return nullptr;
    }
    if (loaded && loaded->renderBlocks.empty()) {
        error = "结构中没有可投影方块";
        return nullptr;
    }
    return loaded;
} catch (std::exception const& exception) {
    error = std::string{"结构加载失败: "} + exception.what();
    return nullptr;
} catch (...) {
    error = "结构加载发生未知异常";
    return nullptr;
}

} // namespace lholo::structure::detail
