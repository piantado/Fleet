
Implemented tools
==================

- Rational-rules style Markov Chain Monte Carlo, including insert/delete proposals
- Parallel Tempering
- Monte Carlo Tree Search
- Factorized hypotheses


Design Considerations
======================

- Simple, efficient parallelization
- Single model files
- Single output file that is a tab-separated file. Most diagnostic/debugging output that is automatically generated is prefixed with "#" as a comment (e.g. in R)


