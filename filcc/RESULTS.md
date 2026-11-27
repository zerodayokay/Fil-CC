# Fil-CC benchmark results

## Integrated compiler

The test built Clang from Fil-CC commit b5c7479f8f and installed it beside the official Fil-C 0.687 binary over the same pinned runtime. The Off and On columns use the same Fil-CC build. The separate Stock column uses the upstream release. [Linux build, test logs, and raw data](https://github.com/zerodayokay/Fil-CC/actions/runs/45926650575)

The hosted runner used Intel(R) Xeon(R) 6975P-C and Linux-7.0.0-1014-aws-x86_64-with-glibc2.39. The benchmark pinned execution to logical CPU 0 and used 11 interleaved samples after one warm-up. Times are median nanoseconds per unit. The last two columns are paired median speedups. The Off / On column includes a 95% bootstrap interval.

| Workload | Native Clang | Stock Fil-C | Off | On | Stock / On | Off / On |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Array scan, 16 KiB | 0.065 | 0.654 | 0.654 | 0.233 | 2.81× | 2.80× [2.80, 2.81] |
| Array scan, 4 MiB | 0.134 | 0.662 | 0.662 | 0.244 | 2.71× | 2.71× [2.68, 2.74] |
| Array scan, 128 MiB | 0.214 | 1.158 | 1.155 | 0.499 | 2.25× | 2.13× [1.93, 2.37] |
| Generic callback, 100% dominant | 1.054 | 2.125 | 2.119 | 1.894 | 1.12× | 1.12× [1.11, 1.12] |
| Generic callback, 90% dominant | 1.053 | 2.118 | 2.113 | 1.906 | 1.11× | 1.11× [1.10, 1.11] |
| Explicit AOT callback, 100% dominant | 0.135 | 0.665 | 0.661 | 0.250 | 2.65× | 2.64× [2.60, 2.67] |
| Explicit AOT callback, 90% dominant | 0.229 | 0.815 | 0.816 | 0.417 | 1.94× | 1.93× [1.91, 1.97] |
| Shuffled pointer chase | 13.973 | 7.953 | 7.918 | 7.943 | 1.00× | 1.00× [0.99, 1.01] |
| P6 image read and checksum | 0.454 | 0.938 | 0.930 | 0.641 | 1.47× | 1.46× [1.40, 1.56] |

The 4 MiB scan took 63.1% less time per element with batching. The explicit AOT callback dispatch ran 7.56× faster than the generic call with the same compiler in the 100% case and 4.57× faster in the 90% case. The explicit dispatch uses the application's existing knowledge of the target. The compiler keeps the generic callback indirect.

The runner restricted access to hardware performance counters. The raw artifact records the perf diagnostics.

Both Fil-C drivers and both flag settings matched the valid checksums and rejected the null, short, and freed ranges. The packaged Fil-CC binary also passed the 16-bit recurrence, the 64-bit sum, and a C++20 program using the stock runtime. The collector completed 781 cycles while the optimized scans ran. This documents progress during scanning rather than a comparison of GC throughput. [Packaged binary validation](https://github.com/zerodayokay/Fil-CC/actions/runs/45931458064)

## LLVM IR replay

This run compiled the fork's exact batching pass as a small standalone LLVM tool. It captured stock Fil-C's pre-lowering IR, applied the pass, verified the result with LLVM, then fed both the original and transformed IR back to the same stock Fil-C 0.687 compiler. Replay runs the early optimization pipeline a second time. It provides a test of the transformation and Fil-C lowering before measuring the normal integrated driver. [Replay workflow and raw measurements](https://github.com/zerodayokay/Fil-CC/actions/runs/45928127786)

The runner used an Intel Xeon 6975P-C on Linux x86-64. All builds used `-O3 -march=native`. Native Clang was 18.1.3. The test used 11 interleaved samples after one warm-up per toolchain, pinned to one logical CPU. Times are median nanoseconds per element, node, or raster byte. The last column is the paired median speedup over replay with the pass off and its 95% bootstrap interval.

| Workload | Native Clang | Stock Fil-C | Replay off | Replay on | Off / on |
| --- | ---: | ---: | ---: | ---: | ---: |
| Array scan, 16 KiB | 0.065 | 0.654 | 0.654 | 0.233 | 2.80× [2.80, 2.81] |
| Array scan, 4 MiB | 0.135 | 0.661 | 0.660 | 0.244 | 2.71× [2.70, 2.73] |
| Array scan, 128 MiB | 0.253 | 1.007 | 1.009 | 0.488 | 2.07× [1.87, 2.23] |
| Generic callback, 100% dominant | 1.049 | 2.094 | 2.104 | 1.903 | 1.11× [1.09, 1.11] |
| Generic callback, 90% dominant | 1.050 | 2.101 | 2.091 | 1.919 | 1.09× [1.09, 1.10] |
| Explicit AOT callback, 100% dominant | 0.136 | 0.660 | 0.661 | 0.252 | 2.63× [2.61, 2.66] |
| Explicit AOT callback, 90% dominant | 0.227 | 0.806 | 0.804 | 0.425 | 1.91× [1.87, 1.94] |
| Shuffled pointer chase | 13.873 | 8.109 | 8.088 | 8.100 | 1.00× [0.99, 1.01] |
| P6 image read and checksum | 0.370 | 0.880 | 0.881 | 0.521 | 1.68× [1.64, 1.71] |

For the 4 MiB scan, 0.244 ns per 4-byte element is about 16.4 GB/s of effective throughput. The replay-off result of 0.660 ns per element is about 6.1 GB/s. This measures a repeated scan over the same working set and includes the compiler-generated checks and polls.

The direct path in the callback benchmark is explicitly selected in source and can use batching. The generic callback loop remains indirect in the compiler IR. Its timing is sensitive to code placement in this experiment. The explicit path is 7.56× faster than the generic path in the 100% case and 4.52× in the 90% case, based on the replay-on medians. Those figures measure AOT specialization potential. The extra savings from a JIT over an equivalent AOT specialization remain unmeasured.

The independent replay also passed the 8-bit and 32-bit boundary tests, the 16-bit recurrence, 64-bit sums, and the three invalid-read cases. The transformed scan completed while the collector ran 612 cycles. [IR verification and runtime checks](https://github.com/zerodayokay/Fil-CC/actions/runs/45927692348)

## Generated code

Before post-lowering optimization the batched 32-bit scan loads a four-element vector. In the replay's final LLVM IR, the 16-byte upper-bound check still gates four adjacent reads, and the backedge still polls and preserves the capability root. LLVM scalarizes the vector loads in that final IR. The native Clang scan uses a wide vector reduction. This distinguishes grouping safety checks from producing SIMD arithmetic in the backend. [Replay lowering logs and final IR](https://github.com/zerodayokay/Fil-CC/actions/runs/45928127786)

## JIT compilation-cost proxy

LLVM 20 ORC took 78.17 ms to initialize a native engine in a fresh process on a Xeon 6975P-C runner. With that engine warm, adding and looking up a new arithmetic-only callback took a median of 0.830 ms over 31 materializations. The 90th percentile was 0.849 ms. This probe measures native code generation and linking for a tiny function. It does not measure Fil-C instrumentation, capability validation, or deoptimization. [ORC probe source and timings](https://github.com/zerodayokay/Fil-CC/actions/runs/45929353164)

If a Fil-C JIT saved 0.1 ns per element over the explicit AOT path, the measured native warm-engine cost alone would take about 8.3 million elements to recover. Charging the observed engine initialization to the first specialization raises that to about 790 million elements. Both calculations use an assumed net improvement because no JIT implementation has established that improvement over the measured AOT path.
