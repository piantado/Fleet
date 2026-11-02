# Experimental inference

These methods are correct as far as the tests can tell, but their interfaces may still change and their
usefulness is not yet established. They are on the default include path (`Fleet.mk` adds
`-Isrc/Inference/Experimental`), so `#include "PartitionMCMC.h"` and the others work unchanged.

| File | What it is | Tests |
|---|---|---|
| `PartitionMCMC.h` | Expands the grammar to a given depth, then runs a separate MCMC chain in each partial tree (partition). | `Testing/TestPartitionMCMC.cpp` |
| `AStarSearch.h` | A* search over partial programs. | `Testing/AStarSearch/` |
| `RebagProposal.h` | The rebag Metropolis–Hastings move. It regenerates a node, then resamples among all trees built from the resulting multiset of rules: the whole tree (`Scope::Global`) or only the regenerated subtree (`Scope::Local`). Call it from a hypothesis's `propose_tempered(data, T) const`; `MCMCChain` calls that method instead of `propose()` whenever it exists. | `Testing/TestRebag.cpp`; benchmark in `Testing/RebagBenchmark/` |

`Grammar::generate_nonterminal_skeleton` stays in `Grammar`. The skeleton multiple-try proposal that uses it
lives in ARCClaude, not in Fleet.
