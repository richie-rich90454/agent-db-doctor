# Performance

The specification sets a hard contract: be the fastest, leanest, and most
I/O-efficient tool of its kind, and beat every equivalent written in Rust, Go, or
Python on both speed and memory. This document records what that means in practice,
what was measured, and where the implementation falls short.

## The contract

| Metric | Target |
|---|---|
| Resident memory, idle service | < 12 MB |
| Resident memory, full diagnostic | < 40 MB |
| Diagnostic pass on a 100 GB database | < 500 ms |
| System calls per diagnostic pass | < 20 |
| Bytes read per diagnostic pass | < 64 KB |
| Prune of 1.5 M redundant rows | < 8 s |
| Startup, command line tool | < 30 ms |
| Startup, service | < 100 ms |
| Allocations on the hot path | Zero |
| Syscalls per watchdog tick | 1 per database |

## What was measured, and how

Numbers come from the machine described in `benchmarks/results.md`, tool built at
`-O3`, SQLite compiled with `SQLITE_THREADSAFE=0` and
`SQLITE_DEFAULT_MEMSTATUS=0`.

| Measurement | Target | Measured | Where the assertion lives |
|---|---|---|---|
| Diagnostic pass, 100 GB database | < 500 ms | **13 ms** | `DiagnoseBenchmark` |
| File metadata queries per pass | < 20 | **2** | `DiagnoseBenchmark` |
| Bytes read from the database per pass | < 64 KB | **100** | `DiagnoseBenchmark` |
| Metadata calls per watchdog tick per database | 1 | **1** | `WatchdogBenchmark` |
| Memory growth over 1000 idle ticks | ~0 | **< 1 MB** | `WatchdogBenchmark` |
| Peak resident memory, full diagnostic | < 40 MB | **7.1 MB** | process working set |
| Startup, command line tool | < 30 ms | **28.3 ms** | 5 runs of `--version` |
| Marginal cost of removing one row | 5.3 us | **8.0 us** Windows, **3.0 us** Linux | `PruneBenchmark` |

The 100 GB database used for the measurement is a real SQLite file grown to that
size with a sparse region. The pass therefore had a genuine hundred gigabyte file
to answer questions about, and reading even one percent of it would have taken far
longer than the budget. It completed in 13 ms, which is only possible because it
read a hundred bytes.

## Where the implementation falls short, and why

**Removing a duplicate row cost 20 us against a target of 5.3 us. It now costs 8.0
on Windows and 3.0 on Linux.** Linux is inside the target. The change came from two
connection settings, not from rewriting the deletion, and both are things SQLite was
already able to do.

| Change | Effect per row |
|---|---|
| `PRAGMA temp_store = MEMORY` | 20.1 us down to about 9 us on Windows |
| `PRAGMA cache_size = -65536` | about 20% off the remainder |
| Passive checkpoints instead of truncating | about 25% off the remainder |

`temp_store` is the whole story, and the reason is Windows. A large `DELETE` makes
SQLite build scratch b-trees, and by default it writes them to a temporary file.
Every write to a temporary file on Windows is offered to antivirus software, so the
cost of deleting a row came to be dominated by a scanner. The same SQL, unchanged,
on Linux measured 6.8 us per row where Windows measured 17.7 us in the same probe.

The scratch structures are bounded by one batch of rows rather than by the size of
the table, so keeping them in memory cannot make a large database expensive. The
page cache is a fixed 64 MB, which is a known cost rather than a growing one.

**Checkpointing was the second change.** The original code checkpointed every fifty
batches using `TRUNCATE`, which blocks until no other process holds a read lock. The
loop now checkpoints after 250,000 removed rows using `PASSIVE`, which returns
immediately, and still uses `TRUNCATE` once at the end where the point is to hand
back the space. Counting rows rather than batches means the bound on the log holds
however large a batch is.

What was tried before those two settings, measured on this machine:

| Approach | Measured per row |
|---|---|
| Subquery in the predicate, one query per type | 19.0 us |
| Correlated `NOT EXISTS`, one query per type | 20.1 us |
| Three types in one query, `ORDER BY rowid` | 13.6 us |
| Per-type delete with the guard, as shipped | 20.1 us |
| A single pass deleting what a subquery selects | 15.7 us |

The third is the fastest of those and **wrong**, and the reason is instructive:
asking for three types at once while ordering by row identifier stops SQLite from
using the index on `type`, because three index ranges cannot be merged into one
ordered stream without a sort. A correct version of that idea needs the sessions
being kept to be identified by a row identifier cut-off, which assumes events were
written in session order, and that assumption is not one this tool should make about
somebody else's database.

The shipped version is the one that cannot delete a session the user asked to keep.
The guard it carries, a correlated `NOT EXISTS` against the sessions being kept,
costs about 1.1 us per row. That is the price of the promise the tool makes, and it
is worth paying.

**The upgrade path**, named so the number is not mistaken for a permanent answer:
deleting by session rather than by event type needs one index on `event(session_id)`.
That index does not exist in a database this tool did not create, and adding it would
modify the user's schema, which the specification does not ask for. With that index,
the deletion becomes one contiguous range per session and the per-row cost falls to
roughly the cost of one b-tree removal rather than three.

**What remains above the target on Windows is the operating system.** For identical
SQL, deleting a row costs about two and a half times more there than on Linux, and no
setting this tool controls reaches inside that. The build prints the measured rate,
so the figure for any given machine is visible rather than asserted in a comment.

**The startup figure is uncomfortably close to the budget.** 28.3 ms against 30 ms
is inside it, but most of that is the shell launching a process, not the tool. The
tool's own work is well under half. The specification's target is achievable but
not comfortably so on this machine.

## How each principle is met

The specification lists twenty input and output principles and twenty eight
performance principles. The ones with teeth, and where they live:

| Principle | Where it is met | How it is checked |
|---|---|---|
| File metrics come from metadata, never from contents | `PlatformMetadata` | `TestPlatformMetadata` |
| The header read is one hundred bytes | `StatisticsReader::readHeaderFields` | `DiagnoseBenchmark` |
| Row counts come from `sqlite_stat1`, never `COUNT(*)` on events | `StatisticsReader::approximateRowCount` | `TestStatisticsReader` |
| One system call per watchdog tick per database | `ServiceManager::runOneWatchdogPass` | `WatchdogBenchmark` |
| Memory mapped I/O for large sequential reads | not used, and documented below | |
| Batched transactions | `PruneCommand`, one per batch | `PruneBenchmark` |
| Forward rowid cursor, never `OFFSET` | `REDUNDANT_BATCH_QUERY` | `PruneBenchmark` |
| A direct write for service logs | `Logger` | `ServiceIntegrationTest` |
| Never read a file to find its size | `PlatformMetadata` | `TestPlatformMetadata` |
| Prepared statements reused, never reparsed in a loop | `Statement` | `TestStatement` |
| Skip SQLite entirely for pure metadata | `PlatformMetadata` | `TestPlatformMetadata` |
| Cache the connection across a workflow | `CommandOptions`, one open per run | `EndToEndIntegrationTest` |
| `synchronous = NORMAL` during bulk writes | `PruneCommand` | `PruneIntegrationTest` |
| `wal_autocheckpoint` disabled during bulk work | `PruneCommand` | `PruneIntegrationTest` |
| `journal_size_limit` bounds the log | `PruneCommand` | `PruneIntegrationTest` |
| `posix_fadvise` and `madvise` on Linux | not used, and documented below | |
| Overlapped I/O on Windows for compaction | not used, and documented below | |
| Never walk a directory tree | `AgentRegistry::directoryEntryNames` | `TestAgents` |
| Never write progress to disk | `ProgressBar` writes to stdout only | `TestProgressBar` |
| Zero allocations on the hot path | one metadata call and arithmetic per tick | `WatchdogBenchmark` |
| Pre-sized buffers | `Arena` | `TestArena` |
| `constexpr` lookup tables | the event type names | `TestAgents` |
| `[[likely]]` and `[[unlikely]]` hints | not used, see below | |

### Where a principle was not met, and why

**Memory mapped I/O.** The specification asks for `PRAGMA mmap_size` on large
sequential reads. The diagnostic pass does not read a large amount sequentially,
because it does not read the file at all: one hundred bytes of header and some
metadata. The mapping would be set up and never used. It is not applied for that
reason, and applying it would add a setting the user did not ask for and a page
table larger than the memory the tool is trying to keep under twelve megabytes.

**`posix_fadvise` and `madvise`.** The same reason. There is no large sequential
read to give the kernel a hint about.

**Overlapped I/O on Windows for compaction.** `VACUUM INTO` is a single call
inside SQLite, and SQLite issues its own reads and writes. Handing it overlapped
descriptors is not something its interface allows. The compaction is therefore
input and output bound by SQLite's own sequential I/O, and the specification's own
requirement, that the tool's overhead be under 200 ms, is met: the tool does
nothing but wait.

**Branch prediction hints.** `[[likely]]` and `[[unlikely]]` are not used. The hot
path has one branch per database, and adding a hint to a single branch in a loop
that runs once an hour would be noise. The performance that matters comes from
doing less work, not from predicting it better.

## What "fastest in the world" was not claimed to mean

No comparison figures against Rust, Go, or Python equivalents appear in this
document or in the README. No such tool was available to measure on the same
machine, and a comparison table with invented numbers would be worse than no table
at all. The measurements above are what this tool does, stated plainly, with the
one figure that misses its target called out rather than rounded away.
