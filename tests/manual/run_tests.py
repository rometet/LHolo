#!/usr/bin/env python3
"""Portable regression checks. Mock tests do NOT validate Bedrock ABI/runtime."""
# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
TESTS = ROOT / 'tests/manual'


def run(command: list[str], *, expect_failure: bool = False) -> None:
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
    if result.stdout:
        print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if expect_failure:
        if result.returncode == 0:
            raise RuntimeError('Negative control unexpectedly passed')
        print('Baseline negative control: expected regression reproduced')
    elif result.returncode != 0:
        raise RuntimeError(f'Command failed ({result.returncode}): {command[0]}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'g++'))
    parser.add_argument('--ubsan', action='store_true')
    parser.add_argument('--baseline-block-rules', type=Path)
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        raise SystemExit(f'C++20 compiler not found: {args.cxx}')
    flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic', '-pthread']
    # LeviLamina's existing hook names intentionally use '$' identifiers.
    version = subprocess.check_output([compiler, '--version'], text=True)
    if 'clang' in version.lower():
        flags += ['-Wno-dollar-in-identifier-extension']
    if args.ubsan:
        flags += ['-fsanitize=undefined', '-fno-sanitize-recover=undefined']
    with tempfile.TemporaryDirectory(prefix='lholo-manual-regression-') as temporary:
        temp = Path(temporary)
        stubs = temp / 'stubs'
        # Only test builds see these wrappers. Production builds use MCAPI.
        for relative in [
            'place/PlaceHelper.h', 'place/PlacementState.h', 'place/PlacementExecutor.h',
            'i18n/Message.h', 'plugin/LHolo.h', 'structure/MaterialTracker.h', 'structure/StructureLoader.h',
            'll/api/memory/Hook.h', 'll/api/mod/NativeMod.h', 'll/api/service/Bedrock.h',
            'mc/client/game/ClientInstance.h', 'mc/client/game/IClientInstance.h', 'mc/client/player/LocalPlayer.h',
            'mc/world/gamemode/GameMode.h', 'mc/world/actor/player/Player.h', 'mc/world/actor/player/Inventory.h',
            'mc/world/item/HandSlot.h', 'mc/world/item/ItemStack.h', 'mc/world/item/ItemInstance.h',
            'mc/world/level/BlockPos.h', 'mc/world/level/BlockSource.h', 'mc/world/level/Tick.h',
            'mc/world/level/block/Block.h', 'mc/world/level/block/BlockType.h', 'Windows.h',
        ]:
            path = stubs / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#pragma once\n#include "MockHookRuntime.h"\n', encoding='utf-8')
        includes = ['-I', str(stubs), '-I', str(TESTS), '-I', str(ROOT / 'src')]
        policy = ROOT / 'src/place/ManualPlacementPolicy.cpp'
        rules = ROOT / 'src/block/BlockPlacementRules.cpp'
        for name, sources in [
            ('policy', [policy, TESTS / 'ManualPlacementPolicyTests.cpp']),
            ('items', [rules, TESTS / 'PlacementItemTests.cpp']),
            ('hooks', [policy, TESTS / 'HookRoutingTests.cpp']),
        ]:
            binary = temp / name
            run([compiler, *flags, *includes, *map(str, sources), '-o', str(binary)])
            run([str(binary)])
        if args.baseline_block_rules is not None:
            baseline = args.baseline_block_rules.resolve(strict=True)
            binary = temp / 'baseline-items'
            run([compiler, *flags, *includes, str(baseline), str(TESTS / 'PlacementItemTests.cpp'), '-o', str(binary)])
            run([str(binary)], expect_failure=True)

        # Syntax-only check of the actual new panel against a minimal ImGui
        # surface. Does not claim the real Dear ImGui/LeviLamina DLL linked.
        ui = temp / 'ui-stubs'
        (ui / 'ui').mkdir(parents=True)
        (ui / 'i18n').mkdir()
        (ui / 'ui/MenuWidgets.h').write_text('''#pragma once
#include <cstddef>
struct ImVec2 { float x,y; ImVec2(float a,float b):x(a),y(b){} };
namespace ImGui {
inline void TextWrapped(char const*, ...) {}
inline void TextDisabled(char const*, ...) {}
inline bool InputTextMultiline(char const*,char*,std::size_t,ImVec2) {return false;}
inline void BeginDisabled(bool) {} inline void EndDisabled() {}
inline void SameLine() {} inline bool Button(char const*) {return false;}
}
namespace lholo::ui {
struct UiMetrics {float scale{1}; bool compact{};};
template<class F> void renderSection(char const*,char const*,UiMetrics const&,F&& f) {f();}
}
''', encoding='utf-8')
        (ui / 'i18n/Translator.h').write_text('''#pragma once
#include <string_view>
namespace lholo::i18n {
inline int language() {return 0;}
inline std::string_view languageCode(int) {return "ja_JP";}
}
''', encoding='utf-8')
        run([compiler, *flags, '-I', str(ui), '-I', str(ROOT / 'src'), '-fsyntax-only', str(ROOT / 'src/ui/ManualPlacementSettings.cpp')])
        print('ManualPlacementSettings: syntax check PASS (stub ImGui)')
    print('Portable/manual regression suite PASS; Windows DLL and Minecraft runtime NOT TESTED')


if __name__ == '__main__':
    main()
