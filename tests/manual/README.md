# Manual placement regression tests

Run on a system with Python 3 and a C++20 GCC/Clang toolchain:

```sh
python tests/manual/run_tests.py --ubsan
python tests/manual/run_tests.py --cxx clang++ --ubsan
```

The suite runs the real allow-list parser and persistence implementation. It also compiles the
actual `BlockPlacementRules.cpp` and `PlaceHelper.cpp` against **test doubles**, plus a syntax-only
check of the new UI against a stub ImGui surface. It does not require game assets, a network
connection, or Minecraft. Temporary files are confined to fresh system temporary directories.

The doubles do NOT establish compatibility with LeviLamina Fake Headers, Windows linking,
Minecraft's registry or its real input event order. Run the existing Windows release build,
`LHoloLogicTests` and in-game checks before merging/releasing. No mock files are added to production
include paths or the existing xmake targets.

An auditor can reproduce the old material-resolution failure with:

```sh
python tests/manual/run_tests.py --ubsan --baseline-block-rules /path/to/old/BlockPlacementRules.cpp
```

That option requires the old item-test binary to fail and reports it as an expected negative
control. Do not interpret the displayed `FAILED: check 4` in that control as a failed new build.

The audit and runtime checklist are in `docs/MANUAL_PLACEMENT_AUDIT_20260925.md`.
