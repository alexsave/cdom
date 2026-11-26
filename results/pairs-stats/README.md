# Statistics of the paired runs

Each `<tag>.tsv` has the median, mean, standard deviation, minimum and maximum of every framework, benchmark and metric of one `tools/pair.sh` run, and `<tag>.loads.tsv` the load at each start.
`tools/pairs_stats.py` made them from `results/pairs/<tag>`.
The raw runs (per-iteration values, harness logs and browser traces) were deleted on 2026-11-26.
A `-` is a standard deviation of a single sample.
`harness-last-2026-10-27.tsv` is the same summary of the last results of the harness in RESULTS.md.
