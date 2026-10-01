#pragma once

#include "structure/formats/JavaNbtReader.h"
#include "structure/formats/BedrockNbtScanner.h"

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>

namespace lholo::tests {

template <class Check>
void runNbtTypeChecks(Check check) {
    using namespace structure::detail;
    auto integer = [](std::uint64_t value, unsigned width, bool little = false) {
        std::string result;
        for (unsigned byte = 0; byte < width; ++byte) {
            auto const shift = (little ? byte : width - 1 - byte) * 8;
            result.push_back(static_cast<char>(value >> shift));
        }
        return result;
    };
    auto payloads = [&](bool little) {
        auto word = [&](std::uint64_t value, unsigned width) { return integer(value, width, little); };
        std::array<std::string, 12> result;
        result[0] = std::string(1, '\x80');
        result[1] = word(0xff80, 2);
        result[2] = word(0xffed2979, 4); // -1234567
        result[3] = word(0x8000000000000000ull, 8);
        result[4] = word(0x3fc00000, 4); // 1.5f
        result[5] = word(0xbfe0000000000000ull, 8); // -0.5
        result[6] = word(3, 4) + std::string("\0\x80\xff", 3);
        result[7] = word(3, 2) + "NBT";
        result[8] = std::string(1, '\3') + word(2, 4) + word(0xffffffff, 4) + word(42, 4);
        result[9] = std::string(1, '\3') + word(1, 2) + "n" + word(42, 4) + std::string(1, '\0');
        result[10] = word(2, 4) + word(0x80000000, 4) + word(0x7fffffff, 4);
        result[11] = word(2, 4) + word(0x8000000000000000ull, 8) + word(0x7fffffffffffffffull, 8);
        return result;
    };
    for (bool little : {false, true}) {
        auto const values = payloads(little);
        for (std::size_t type = 1; type <= values.size(); ++type) {
            std::string wire("\x0a\0\0", 3);
            wire.push_back(static_cast<char>(type));
            wire += integer(1, 2, little); wire += 'x'; wire += values[type - 1]; wire += '\0';
            auto parse = [&](std::string_view bytes) {
                if (little) { (void)BedrockNbtScanner{bytes, 64, 4096}.scan(); }
                else { (void)JavaNbtReader{bytes, 4096}.readRoot(); }
            };
            bool accepted{};
            try { parse(wire); accepted = true; } catch (std::runtime_error const&) {}
            check(accepted, "every supported NBT payload type is accepted in each wire endian");
            if (!little) {
                auto const root = JavaNbtReader{wire}.readRoot();
                auto const& value = root.at("x").value;
                bool semantic{};
                switch (type) {
                case 1: semantic = std::get<std::int8_t>(value) == -128; break;
                case 2: semantic = std::get<std::int16_t>(value) == -128; break;
                case 3: semantic = std::get<std::int32_t>(value) == -1234567; break;
                case 4: semantic = std::get<std::int64_t>(value) == (std::numeric_limits<std::int64_t>::min)(); break;
                case 5: semantic = std::get<float>(value) == 1.5f; break;
                case 6: semantic = std::get<double>(value) == -0.5; break;
                case 7: semantic = std::get<JavaNbtTag::ByteArray>(value) == (JavaNbtTag::ByteArray{0,128,255}); break;
                case 8: semantic = std::get<std::string>(value) == "NBT"; break;
                case 9: { auto const& list = std::get<JavaNbtTag::List>(value);
                    semantic = list.size() == 2 && std::get<std::int32_t>(list[0].value) == -1
                        && std::get<std::int32_t>(list[1].value) == 42; break; }
                case 10: semantic = std::get<std::int32_t>(std::get<JavaNbtTag::Compound>(value).at("n").value) == 42; break;
                case 11: semantic = std::get<JavaNbtTag::IntArray>(value) ==
                    (JavaNbtTag::IntArray{(std::numeric_limits<std::int32_t>::min)(), (std::numeric_limits<std::int32_t>::max)()}); break;
                case 12: semantic = std::get<JavaNbtTag::LongArray>(value) ==
                    (JavaNbtTag::LongArray{(std::numeric_limits<std::int64_t>::min)(), (std::numeric_limits<std::int64_t>::max)()}); break;
                }
                check(semantic, "Java NBT scalar and container values preserve signed and floating bits");
            }
            for (std::size_t length = 0; length < wire.size(); ++length) {
                bool rejected{};
                try { parse(std::string_view{wire}.substr(0, length)); }
                catch (std::runtime_error const&) { rejected = true; }
                check(rejected, "every truncated prefix of every payload type is rejected");
            }
            // Mutations may be valid values. The contract is bounded success
            // or a descriptive parser error, with sanitizers checking accesses.
            for (std::size_t offset = 0; offset < wire.size(); ++offset) {
                for (auto const byte : {0, 7, 10, 13, 127, 255}) {
                    auto mutated = wire; mutated[offset] = static_cast<char>(byte);
                    bool bounded = true;
                    try { parse(mutated); }
                    catch (std::runtime_error const&) {}
                    catch (...) { bounded = false; }
                    check(bounded, "deterministic NBT type/name/length/value mutation stays in parser contract");
                }
            }
        }
    }
}

} // namespace lholo::tests
