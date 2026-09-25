#!/usr/bin/env python3
"""Exercise actual placement functions with explicit engine test doubles, not Bedrock."""
# SPDX-License-Identifier: GPL-3.0-or-later
from __future__ import annotations
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def between(text: str, start: str, stop: str) -> str:
    begin = text.index(start)
    end = text.index(stop, begin + len(start))
    return text[begin:end]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--ubsan', action='store_true')
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if compiler is None:
        raise SystemExit(f'C++20 compiler not found: {args.cxx}')
    executor = (ROOT / 'src/place/PlacementExecutor.cpp').read_text(encoding='utf-8')
    projection = (ROOT / 'src/projection/core/ProjectionRules.cpp').read_text(encoding='utf-8')
    # Slice unmodified function text, including its real branching. Engine
    # objects/serialization/native connectionUpdate are the explicit doubles.
    functions = between(executor, 'bool sameSerializedState(', 'bool wouldMergeClickedSlab(')
    functions += between(executor, 'bool placementPredictionMatches(', 'bool resolveOrientedPlacement(')
    functions += between(executor, 'ItemFind findItemSlot(Player&', '// Server-synced slot exchange')
    connection = between(projection, 'Block const& withFlattenedConnections(', 'bool projectionStatesMatch(')
    # Ensure this is wired into the real planner, not a tested dead helper.
    assert 'predicted, ghost, expectedDoorUpper, region, cell, manualPlacement' in executor
    assert 'bool                    manualPlacement = false' in executor
    assert '            placement,\n            manualPlacement\n' in executor
    range_code = between(executor, 'void tickRangePlaceImpl(', 'void tickEasyPlaceImpl(')
    assert 'manualPlacement' not in range_code
    flags = ['-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic']
    if args.ubsan:
        flags += ['-fsanitize=undefined', '-fno-sanitize-recover=undefined']
    with tempfile.TemporaryDirectory(prefix='lholo-prediction-') as directory:
        temp = Path(directory)
        (temp / 'placement_functions.inc').write_text(functions, encoding='utf-8')
        (temp / 'connection_function.inc').write_text(connection, encoding='utf-8')
        binary = temp / 'prediction-tests'
        command = [compiler, *flags, '-I', str(temp), str(ROOT / 'tests/manual/PlacementPredictionTests.cpp'), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True)
        # Negative control: the pre-fix decision only called the original
        # comparator, without same-world normalization. It must compile but fail.
        replacement = '''bool placementPredictionMatchesInWorld(
    Block const& predicted, Block const& ghost, Block const* upper,
    BlockSource&, BlockPos const&, bool
) { return placementPredictionMatches(predicted, ghost, upper); }

'''
        original_helper = between(functions, 'bool placementPredictionMatchesInWorld(', 'ItemFind findItemSlot(Player&')
        (temp / 'placement_functions.inc').write_text(functions.replace(original_helper, replacement), encoding='utf-8')
        subprocess.run(command, check=True)
        control = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
        if control.returncode == 0:
            raise RuntimeError('Old placement decision unexpectedly passed the regression')
        if 'manual connected-block regression' not in control.stderr:
            raise RuntimeError(f'Negative control failed for an unrelated reason: {control.stderr}')
        print('Old comparison negative control: expected connected-block rejection reproduced')
    print('Planner call-site audit PASS. Minecraft native behavior/Windows ABI NOT VERIFIED by this test.')


if __name__ == '__main__':
    main()
