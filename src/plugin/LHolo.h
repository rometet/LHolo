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

#pragma once

namespace ll::mod {
class NativeMod;
}

namespace lholo {

class LHolo {
public:
    static LHolo& getInstance();

    LHolo(LHolo const&)            = delete;
    LHolo(LHolo&&)                 = delete;
    LHolo& operator=(LHolo const&) = delete;
    LHolo& operator=(LHolo&&)      = delete;

    bool load();
    bool enable();
    bool disable();
    bool unload();

    [[nodiscard]] ll::mod::NativeMod& getSelf() const;

private:
    LHolo();

    ll::mod::NativeMod& mSelf;
};

} // namespace lholo
