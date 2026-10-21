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

#include "plugin/LHolo.h"

#include "app/AppKernel.h"

#include "ll/api/mod/NativeMod.h"
#include "ll/api/mod/RegisterHelper.h"

namespace lholo {

LHolo& LHolo::getInstance() {
    static LHolo instance;
    return instance;
}

LHolo::LHolo() : mSelf(*ll::mod::NativeMod::current()) {}

ll::mod::NativeMod& LHolo::getSelf() const { return mSelf; }

bool LHolo::load() {
    return app::AppKernel::getInstance().load();
}

bool LHolo::enable() {
    return app::AppKernel::getInstance().enable();
}

bool LHolo::disable() {
    return app::AppKernel::getInstance().disable();
}

bool LHolo::unload() {
    return app::AppKernel::getInstance().unload();
}

} // namespace lholo

LL_REGISTER_MOD(lholo::LHolo, lholo::LHolo::getInstance());
