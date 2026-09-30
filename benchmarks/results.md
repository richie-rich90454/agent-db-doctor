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
| Peak working set, whole process | < 12 MB idle | **4.75 MB** idle, **7.1 MB** during a diagnostic | process working set, and the soak below |
| Slowest tick over 20 ticks | far below 1 s | **well under 50 ms** | `WatchdogBenchmark` |
| Stop request honoured | promptly | **immediate** | `WatchdogBenchmark` |

### The long soak was stopped early, and here is what it reached

Two runs were started on Ubuntu 24.04 and stopped on request after **45 minutes and
39 seconds**, not the twenty four hours this section's name suggests. At the ten
second interval the tool accepts, that is about **276 watchdog passes**, rather than
the 8,640 a full day would give. Nothing here claims a twenty four hour result,
because there was not one.

| Run | Build | Question | What it reached |
|---|---|---|---|
| Sanitized | `-fsanitize=address,undefined` | Does it touch memory it should not? | **0** AddressSanitizer reports, **0** leaks, **0** findings in this project's sources, across 46 samples |
| Release | ordinary `-O3` | What is the peak resident set? | **4.75 MB**, and flat: the first sample and the last are identical |

The nine UndefinedBehaviorSanitizer reports are all inside spdlog and inside
libstdc++'s `shared_ptr` when it performs a type erased cast. None are in this
project. It is a well known interaction between UBSan's virtual pointer check and a
library that stores objects polymorphically behind a shared pointer, and they are not
counted as findings about this tool.

**What the memory claim rests on** is the in-process `WatchdogBenchmark`, which runs a
thousand ticks and asserts under a megabyte of growth on every build. The soak was
supporting evidence for a figure the benchmark already enforces, so stopping it early
weakens the demonstration rather than the guarantee.

**A sanitized binary cannot measure memory.** AddressSanitizer keeps a shadow map of
the whole address space, and the same code doing almost nothing reported 61 MB. That
number is the sanitizer, not the tool. This is why the memory figure above comes from
the release run, and why the two runs exist at all.

## Prune

| Measurement | Target | Measured | Asserted in |
|---|---|---|---|
| Marginal cost of one row | 5.3 us | **8.0 us** Windows, **3.0 us** Linux | `PruneBenchmark` |
| Rows kept at the boundary | exact | **exact** | `PruneBenchmark` |
| Readable text after a prune | unchanged | **unchanged** | `PruneIntegrationTest` |

**This started at 20.1 microseconds per row and is now 8.0.** The improvement came
from two connection settings rather than from changing the algorithm, and the full
account is in `docs/performance.md`. On Linux the same build measures about 3.0
microseconds per row, which is inside the target.

What remains above the target on Windows is the operating system, not the query. For
byte-for-byte identical SQL, deleting a row costs roughly two and a half times more
on Windows than on Linux, because every temporary file and every log write passes
through antivirus software.

### How the measurement resists a noisy machine

The benchmark repeats the pair of runs five times and keeps the fastest rate,
because anything else running on the machine can only make a deletion slower. It
also opens each freshly written database once before starting the clock, so the cost
of the first read, which is an antivirus scan of a file the scanner has not seen
before, is not counted as the cost of removing a row.

The assertion that matters is a ratio rather than a fixed number: tripling the work
must not cost much more per row than the smaller run. Because both sides are
measured on the same machine at the same moment, a busy processor or a background
scan inflates both and cannot affect the result. A benchmark that gates on an
absolute figure fails on a busy machine, and a benchmark that fails on a busy machine
is a benchmark people stop running.

The measured rate is printed by the test, so anyone can see the figure for their own
machine rather than trusting a number written in a comment.

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
