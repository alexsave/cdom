# Statistics of the benchmark runs

Each `<name>.tsv` has the median, mean, standard deviation, minimum and maximum of every framework, benchmark and metric of one run, and `<name>.loads.tsv`, where there is one, the load at each start.
`tools/result_stats.py` makes them from a run's result files.

- `fp1` to `fp5`, `mem5`, `pass1`, `pass2`: the passes of RESULTS.md (its load table is `results/loads.tsv`).
- `micro_chrome.tsv`: the micro measurements of RESULTS.md.
- `harness-last-2026-10-27`: the last results the harness held.
- The others: paired runs of `tools/pair.sh`.

The raw files were deleted on 2026-11-26.
A `-` is a value the run did not give, such as the standard deviation of a single sample.
