#ifndef LLVM_LIB_TRANSFORMS_INSTRUMENTATION_FILCCBATCHREADS_H
#define LLVM_LIB_TRANSFORMS_INSTRUMENTATION_FILCCBATCHREADS_H

namespace llvm {
class Function;

// Strip-mine straight-line scalar read loops before Fil-C lowers memory
// accesses.
void filccBatchReads(Function &F);
} // namespace llvm

#endif
