# Fil-CC design and evaluation

Fil-CC builds on Fil-C 0.687 at commit `2db72c646daa1acad2754bcb0e2f220f732e9789`. Its first optimization replaces four adjacent scalar reads with one checked vector read. The optimization is off by default and uses Fil-C's existing runtime, ABI, and memory-access lowering. [Fil-C's compiler pipeline](https://fil-c.org/compiler)

## Fil-C and Fil-CC

| Area | Fil-C 0.687 | Fil-CC with batching enabled |
| --- | --- | --- |
| Scalar array scan | Lowers dynamic scalar memory accesses and schedules the checks it can prove redundant | Strip-mines eligible loops into four-element batches and a scalar tail |
| Spatial safety | Validates the capability and address range of each surviving check | Uses the same lowering for the full vector byte range |
| Lifetime and GC | Keeps roots live across calls and backedge polls. Mutability can invalidate facts established before a poll | Rechecks each vector range and lets the original lowering track roots and emit batch and scalar backedge polls |
| Callback scan | Validates indirect calls using the capability and calling convention | Keeps the generic path. The benchmark also measures a separate source-level AOT specialization |
| Linked structures | Validates data and pointer accesses | Processes these loops through the existing optimizer |
| Build and ABI | Clang 20.1.8 and Fil-C runtime | Same driver and runtime ABI. This fork's installer and CI target Linux x86-64 |
| Runtime specialization | Ahead-of-time compilation | Ahead-of-time compilation, with JIT feasibility evaluated below |

The runtime may revoke an allocation even while a pointer to it is available as a GC root. A capability range established before a safepoint therefore cannot serve as blanket permission for later iterations. Fil-CC's entry test selects the loop shape from the count. Each batch establishes the current capability and range facts when the vector load executes. [Fil-C capability model](https://fil-c.org/invisicaps) Â· [Fil-C safepoints](https://fil-c.org/safepoints)

## Transformation

The optimizer recognizes zero-based, unit-stride, 64-bit induction loops with a single ordinary scalar integer load. The load must address an argument pointer using that induction variable. The body may perform integer additions, subtractions, multiplications, bitwise operations, and integer casts. It accepts a self-latching loop or a header test with a single backedge from a straight-line body. Stores, calls, atomic and volatile loads, pointer-valued loads, multi-exit bodies, and loops in size-optimized or opt-out functions keep their original lowering.

1. Keep the original loop and exit. On the entry edge, test that the count exceeds four and that the count times the element size fits in a signed 64-bit byte offset. A count of zero still reaches the original zero-trip test without accessing the pointer.
2. In the batch loop, form the address for the current index without new pointer or integer no-wrap promises. Read one four-element vector at byte alignment and apply the original integer operations in lane order. The vector operation goes through Fil-C's normal access lowering.
3. Continue batching while at least five elements remain. Pass the induction variable and loop-carried state to the original loop for its one-to-four-element tail. This keeps the original exit values.

The range check can report the larger vector range when a batch includes an invalid element. Polls occur per batch in the batched part of the loop, which changes the granularity of deferred signal delivery. Exact first-failing scalar diagnostics would require a nontrapping batch probe and a branch to the scalar loop at the current index. The extra probe and branch would need a separate performance evaluation.

### Check-work estimate

For `n >= 5`, the pass emits `b = floor((n - 1) / 4)` batch iterations and `t = n - 4b` scalar iterations. If stock Fil-C needs one range check per element, the transformed count is `b + t` before later LLVM simplifications.

For `n = 1,048,577`, `b = 262,144` and `t = 1`. The logical count falls from 1,048,577 checks to 262,145, a reduction of 786,432 checks or about 75%. Every source byte still has to be read. LLVM code generation, branch behavior, runtime polling, cache misses, and other work determine elapsed time. For example, if removed work accounts for only 30% of elapsed time, Amdahl's law caps the speedup at `1 / (1 - 0.30) = 1.43x` even if that work disappears entirely.

## Callback specialization and JIT feasibility

The callback workloads run an integer transform through a generic function pointer or through an explicit AOT branch on the known application selector. Both forms use identical callback bodies and input data. The 100% and 90% dominant-target cases estimate what callers with known targets can gain. They are also controls because Fil-CC's batch pass leaves the indirect call in the generic loop.

A general implementation should evaluate sampled AOT profile-guided indirect-call promotion and LTO after that experiment. The pinned distribution builds compiler-rt with its profile runtime disabled, so instrumentation-based PGO needs a compatible profile runtime or a different profiling path. [Fil-C 0.687 compiler-rt build](https://github.com/pizlonator/fil-c/blob/v0.687/build_compiler_rt.sh) A runtime optimizer is justified only by residual gains over AOT on held-out workloads with changing callback targets. The candidate must guard the callable capability and address using the existing validation rules, preserve the hidden Fil-C ABI and safepoint root maps, and keep the generic checked path reachable on a miss. The dynamic compiler would need trusted Fil-C input, executable-memory lifecycle management, module and code lifetime tracking, and tests for signals and concurrent GC.

Break-even elements are `compile_ns / (AOT_ns_per_element - JIT_ns_per_element)` when the denominator is positive. For example, 2 ms of dynamic compilation and 0.2 ns per element of additional savings over AOT need 10 million elements to repay compilation. A callback's frequency can guide the decision to specialize. It cannot establish authority to use the callee or extend the lifetime of an array capability.

## Reproducing the comparison

The benchmark records all samples, CPU model, flags, toolchain versions, input hash, and the LLVM IR and lowering logs. It runs each workload in randomized toolchain order with one warm-up on a fixed logical CPU. Timings come from a monotonic clock inside the process. Results show median nanoseconds per visited element, node, or raster byte and a paired bootstrap interval for the Fil-C to Fil-CC ratio.

- Array scans cover 16 KiB, 4 MiB, and 128 MiB working sets.
- Callback tests compare the generic call with the source-level AOT version for stable and mixed targets.
- Pointer chasing follows a shuffled ring of separately allocated nodes.
- The image workload reads a real 5,000 by 3,417 P6 chart from the inherited benchmark data. It times opening the file, parsing the dimensions, allocating and reading the raster, summing the 51,255,000 raster bytes, and freeing the buffer.
- The safety tests exercise zero-count null input, batch and tail boundaries, interior pointers, and invalid accesses with null, short, and freed ranges.
- The extended regression test compares an order-dependent 16-bit recurrence and 64-bit sums with scalar references. It also runs scans concurrently with the collector and requires GC cycles to complete during scanning.
- The lowering check confirms the batch is present in eligible code before post-lowering optimization. For hardware cycles and instructions, pass `--perf`. Those counters cover the complete process, including setup and teardown. Container or WSL kernel policy may restrict access to counters.

Hosted Linux results characterize one machine and these workloads. PizBench, AArch64, broader C and C++ corpora, and worst-case GC response require separate measurements.
