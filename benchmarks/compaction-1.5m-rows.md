# Benchmark: compacting one and a half million redundant snapshots

## What this measures

The specification's claim that 1.5 million redundant snapshots can be removed in under
eight seconds, and the three design decisions that are supposed to make it possible:

1. A **forward rowid cursor** rather than offset pagination. `LIMIT n OFFSET m`
   makes SQLite walk past `m` rows before returning the next `n`, which makes a
   sequence of batches quadratic. `WHERE rowid > lastSeenPerType ORDER BY rowid LIMIT n`, with one cursor per event type
   walks the b-tree directly.
2. **One transaction per batch.** Batching 10,000 deletions per transaction reduces
   the flush count from one per row to one per batch.
3. **Prepared statements reused**, never reparsed inside the loop.

## How to run it

```bash
cmake --build build
ctest --test-dir build -R CompactBenchmark --output-on-failure
```

## Why the rate is measured as a slope

A single run also pays for the safety copy, the integrity check, and the
verification pass. None of those scale with the number of rows, so dividing a
single run's time by its row count measures the wrong thing.

Two runs of different sizes are timed instead, and the difference between them
divided by the difference in row counts is the cost of one more row. Every fixed
cost cancels out and what remains is the throughput figure the specification is
about.

```cpp
const pair<long long, long long> smallRun = timeOneRun(150, COPIES_PER_MESSAGE);
const pair<long long, long long> largeRun = timeOneRun(450, COPIES_PER_MESSAGE);
const double nanosecondsPerRow =
    static_cast<double>(largeRun.first - smallRun.first)
    / static_cast<double>(largeRun.second - smallRun.second);
```

## What is asserted

| Assertion | Why it is there |
|---|---|
| Rows were actually removed | Otherwise the timing measures nothing |
| Tripling the work does not triple the cost per row | Catches a quadratic regression, and cannot be broken by a busy machine |
| The measured rate is printed | So the number is visible on the machine that ran it |
| The event rows of every kept session survive, exactly | A range delete that ignored the boundary would pass a weaker check |
| No event rows at all remain from the old sessions | One leaked row means the boundary was drawn in the wrong place |
| The run does not accumulate per row state | A regression that kept every deleted identifier would grow memory |

## The result

The measured marginal cost is **8.0 microseconds per row on Windows and 3.0 on
Linux**, against a target of 5.3. It was 20.1 before the connection settings
described in `docs/performance.md` were applied. Linux is inside the target; Windows
is about one and a half times above it, and the remainder is the cost of the
operating system rather than of the query.

## Results

See `results.md`.
