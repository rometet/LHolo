#!/usr/bin/env python3
"""Compile current production comparison bodies with explicit engine doubles.
No checked-in snapshots; every run re-extracts source. Not a Minecraft/ABI test.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def function(text: str, signature: str) -> str:
    if text.count(signature) != 1:
        raise ValueError(f"Expected one production definition: {signature}")
    start = text.index(signature)
    # Mask comments/quoted literals so their braces cannot affect extraction.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                    lambda m: ' ' * len(m.group()), text, flags=re.S)
    brace = masked.index('{', start)
    depth = 0
    for end in range(brace, len(masked)):
        depth += (masked[end] == '{') - (masked[end] == '}')
        if depth == 0:
            return text[start:end+1] + '\n'
    raise ValueError(f"Unbalanced production function: {signature}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument('--sanitize', action='store_true')
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    compiler = shutil.which(args.cxx)
    if not compiler:
        raise SystemExit(f"Compiler not found: {args.cxx}")
    source = {}
    for path in ('src/place/PlacementExecutor.cpp', 'src/block/BlockPlacementRules.h',
                 'src/projection/core/ProjectionRules.cpp'):
        data = (ROOT / path).read_bytes()
        source[path] = data.decode('utf-8')
        print(f'SOURCE {path} sha256={hashlib.sha256(data).hexdigest()}', flush=True)
    placement = source['src/place/PlacementExecutor.cpp']
    correction = source['src/projection/core/ProjectionRules.cpp']
    prediction = function(placement, 'bool placementPredictionMatches(')
    production = 'namespace lholo::block {\n' + function(
        source['src/block/BlockPlacementRules.h'], 'inline std::string_view placeableBaseName(') + '}\n'
    production += 'namespace lholo::place {\n' + '\n'.join(function(placement, sig) for sig in (
        'bool sameSerializedState(', 'bool horizontalPlacementDirectionMatches(',
        'bool manualSerializedPlacementMatches(', 'bool isTwoBlockDoor(')) + prediction + '}\n'
    production += 'namespace lholo::projection::detail {\n' + '\n'.join(
        function(correction, sig) for sig in ('bool serializedHorizontalDirectionMatches(',
            'bool serializedStatesMatchExcept(', 'bool projectionStatesMatch(')) + '}\n'
    variants = [('current', production, None)]
    if args.negative_controls:
        begin = prediction.index('    // Orientation is a veto')
        end = prediction.index('    auto const& name = ghost.getTypeName();', begin)
        variants.append(('no-guard', production.replace(prediction[begin:end], '', 1), 'wrong orientation'))
        variants.append(('old-door-key', production.replace(
            '&& horizontalPlacementDirectionMatches(predicted, ghost)',
            '&& sameSerializedState(predicted, ghost, "direction")', 1), 'modern door accepted'))
        variants.append(('old-correction-key', production.replace(
            ': serializedHorizontalDirectionMatches(expected, actual)',
            ': stateMatches(VanillaStates::Direction())', 1), 'modern correction'))
    with tempfile.TemporaryDirectory(prefix='lholo-orientation-') as work:
        directory = Path(work)
        for name, snippet, failure_marker in variants:
            (directory / 'production.inc').write_text(snippet, encoding='utf-8')
            exe = directory / name
            command = [compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror',
                       '-I'+str(ROOT/'src'), '-I'+str(directory),
                       str(ROOT/'tests/orientation/tests.cpp'), '-o', str(exe)]
            if args.sanitize:
                command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g']
            subprocess.run(command, check=True, timeout=120)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            if failure_marker is None:
                print(result.stdout, end='')
                if result.returncode:
                    raise SystemExit(result.stderr or 'Orientation tests failed')
            else:
                if (result.returncode != 1 or failure_marker not in result.stdout
                        or 'SUMMARY' not in result.stdout or result.stderr):
                    raise SystemExit(f'Negative control {name} did not fail as expected:\n'
                                     + result.stdout + result.stderr)
                print(f'NEGATIVE CONTROL {name}: expected assertion failure')
    print('PASS: source-body regressions (engine doubles; native result injected)')


if __name__ == '__main__':
    main()
