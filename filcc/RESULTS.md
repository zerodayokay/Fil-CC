# Fil-CC benchmark results

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

For the 4 MiB scan, 0.244 ns per 4-byte element is about 16.4 GB/s of effective throughput. The replay-off result of 0.660 ns per element is about 6.1 GB/s. This measures an in-cache loop and includes the compiler-generated checks and polls.

The direct path in the callback benchmark is explicitly selected in source and can use batching. The generic callback loop remains indirect in the compiler IR. Its timing is sensitive to code placement in this experiment. The explicit path is 7.56× faster than the generic path in the 100% case and 4.52× in the 90% case, based on the replay-on medians. Those figures measure AOT specialization potential. The extra savings from a JIT over an equivalent AOT specialization remain unmeasured.

The independent replay also passed the 8-bit and 32-bit boundary tests, the 16-bit recurrence, 64-bit sums, and the three invalid-read cases. The transformed scan completed while the collector ran 612 cycles. [IR verification and runtime checks](https://github.com/zerodayokay/Fil-CC/actions/runs/45927692348)
