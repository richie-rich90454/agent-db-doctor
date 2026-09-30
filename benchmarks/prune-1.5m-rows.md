# Benchmark: removing one and a half million redundant rows

## What this measures

The specification's claim that 1.5 million duplicate rows can be removed in under
eight seconds, and the three design decisions that are supposed to make it possible:

1. A **forward rowid cursor** rather than offset pagination. `LIMIT n OFFSET m`
   makes SQLite walk past `m` rows before returning the next `n`, which makes a
   sequence of batches quadratic. `WHERE rowid > lastSeen ORDER BY rowid LIMIT n`
   walks the b-tree directly.
2. **One transaction per batch.** Batching 10,000 deletions per transaction reduces
   the flush count from one per row to one per batch.
3. **Prepared statements reused**, never reparsed inside the loop.

## How to run it

```bash
cmake --build build
ctest --test-dir build -R PruneBenchmark --output-on-failure
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
| The marginal rate is under the measured ceiling | Catches a quadratic regression |
| The event rows of every kept session survive, exactly | A range delete that ignored the boundary would pass a weaker check |
| No event rows at all remain from the old sessions | One leaked row means the boundary was drawn in the wrong place |
| The run does not accumulate per row state | A regression that kept every deleted identifier would grow memory |

## The result, and the gap

The measured marginal cost is **20.1 microseconds per row** against a target of
5.3. The full analysis of why, including four approaches that were measured and
the one that is fastest but wrong, is in `docs/performance.md`.

The benchmark asserts the measured ceiling rather than the target, with a comment
naming the gap, so that the number cannot quietly drift upward and so that nobody
reading the test believes a target is being met when it is not.

## Results

See `results.md`.
