# P0 / bubble-column regressions

Run `python tests/regression/run_tests.py` after setting LLVM 22 `clang++` on
`PATH`. The Windows build workflow runs this command after `LHoloLogicTests`.

The runner extracts the current production bodies of `ensureCorrectionSection`,
`JavaNbtReader`, checked volume helpers, and the three upstream bubble-column
branches into generated files under `build/regression`. Small type doubles let
these exact bodies run without a Minecraft process. It also compares the
section-indexed arrays initialized in `ProjectionLifecycle.cpp` with those
extended by `ensureCorrectionSection`, and verifies that removing the LH-01
append makes the test fail. Generated files are never checked in.

The NBT corpus includes a one-cell Java schematic-shaped root, every array
kind, List and Compound, truncated and negative lengths, INT_MAX and signed
`-1` values, nesting, dimension and packed-length overflow. A test allocator
rejects allocations above 8 MiB; malformed inputs must throw a parser error
without hitting that allocator cap. The 11-byte list-length reproducer is
included. Sparse multi-region bounding boxes are allowed even when their
empty gaps exceed the per-region cell budget. This bounds test execution
without attempting an OOM.

The bubble cases cover `.mcstructure` primary/secondary ordering, Java mapped
and direct fallback paths, and correction liquid lookup/state comparison.
They exercise production expressions with doubles. They do **not** prove
Minecraft 26.51 registry serialization, native structure loading, GPU output,
or server behavior. `RT-EXTRA-01`, `RT-FLUID-01`, and `RT-FLUID-02` remain
required in-game.
