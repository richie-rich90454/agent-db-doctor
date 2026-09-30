# Benchmark results

Every number this project claims is measured here, on the machine described below,
by an assertion in `tests/benchmark/`. A number that is not in this file is not a
claim this project makes.

## The machine

| | |
|---|---|
| Processor | x86-64, single core used for the measurements |
| Memory | 16 GB |
| Storage | NTFS volume, solid state |
| Operating system | Windows, version 10 or 11 |
| Compiler | GCC 16.2.0, targeting `x86_64-w64-mingw32` |
| Build | `-O3 -DNDEBUG` |
| SQLite | 3.50.4, vendored, `SQLITE_THREADSAFE=0`, `SQLITE_DEFAULT_MEMSTATUS=0` |
| Binary | 4.0 MB |

**These numbers are from one machine.** Absolute figures will differ elsewhere.
The ones that matter are the ratios, and those are structural: the diagnostic pass
reads a fixed hundred bytes whatever the file's size, and the watchdog makes a
fixed number of calls whatever the database count.

## Diagnostic pass

The database used is a real SQLite file grown to one hundred gigabytes with a
sparse region, so it is a genuine 100 GB file to answer questions about and cannot
have been read.

| Measurement | Target | Measured | Asserted in |
|---|---|---|---|
| Time for one pass | < 500 ms | **13 ms** | `DiagnoseBenchmark` |
| Whole command, process start included | < 500 ms | **29 to 41 ms** | measured, 3 runs |
| File metadata queries per pass | < 20 | **2** | `DiagnoseBenchmark` |
| Bytes read from the database | < 64 KB | **100** | `DiagnoseBenchmark` |
| Passes over a 10 MB file and a 100 GB file | equal | **equal** | `DiagnoseBenchmark` |

That last row is the one that matters. Both passes make the same number of calls,
which is what makes a scheduled check affordable no matter how large a user's
database grows.

### Why the integrity check is not in the pass

A quick integrity check reads every page, so its cost is proportional to the size
of the file. At 2 GB it takes a few seconds, which is a fair price for a default
run. Above that the number of seconds grows with the size, and 100 GB would take
minutes, which is exactly the behaviour this tool exists to remove.

So above 2 GB the pass skips the check, **says plainly that it skipped it**, and
names the flag that runs it:

```
! The full page by page integrity check was not run, because this database is
  larger than 2.0 GB and the check reads the whole file. Everything else below is
  complete. Run the check on purpose with: agent-db-doctor diagnose --check
```

Skipping silently would be dishonest, and a diagnostic tool that lies about what it
checked is worse than one that does not check.

## Watchdog

| Measurement | Target | Measured | Asserted in |
|---|---|---|---|
| Metadata calls per tick, per database | 1 | **1** | `WatchdogBenchmark` |
| Bytes read per tick | 0 | **0** | by construction |
| Log lines while below threshold | 0 | **0** | `ServiceIntegrationTest` |
| Memory growth over 1000 idle ticks | ~0 | **< 1 MB** | `WatchdogBenchmark` |
| Peak working set, whole process | < 12 MB idle | **7.1 MB** during a diagnostic | process working set |
| Slowest tick over 20 ticks | far below 1 s | **well under 50 ms** | `WatchdogBenchmark` |
| Stop request honoured | promptly | **immediate** | `WatchdogBenchmark` |

## Prune

| Measurement | Target | Measured | Asserted in |
|---|---|---|---|
| Marginal cost of one row | 5.3 us | **20.1 us** | `PruneBenchmark` |
| Rows kept at the boundary | exact | **exact** | `PruneBenchmark` |
| Readable text after a prune | unchanged | **unchanged** | `PruneIntegrationTest` |

**The row cost is over target and the project says so.** 1.5 million rows would
take roughly thirty seconds rather than eight. The cost is SQLite removing a row
from a table with three b-trees, and the full analysis, including the four
approaches that were measured and why the fastest one is wrong, is in
`docs/performance.md`.

The benchmark measures the *marginal* cost, as a slope between two runs of
different sizes, rather than dividing one run by its row count. A single run also
pays for the safety copy, the integrity check, and the verification pass, none of
which scale with the rows. Measuring the slope removes all of them and leaves the
number the specification is actually about.

## Startup

| Measurement | Target | Measured |
|---|---|---|
| `agent-db-doctor --version`, whole process | < 30 ms | **28.3 ms** average of 5 runs |
| Binary size | not specified | **4.0 MB** |

The startup figure includes the shell launching a child process, so most of it is
not the tool. It is inside the budget, but only just, and that is worth knowing.

## Comparisons against other languages

**None are given.** No equivalent tool in Rust, Go, or Python was available to
measure on this machine, and inventing figures for a comparison table would
invert the point of this project.

What can be said from the measurements above, without a comparison: the diagnostic
pass is **O(1) in the size of the database**, and the only cost that grows with the
data is the row count from `sqlite_stat1`, which is a single indexed read. That is
a structural property, not a tuning result, and it is why the 100 GB figure and the
10 MB figure are the same.
