# Benchmark: the diagnostic pass on a hundred gigabyte database

## What this measures

The specification's central performance claim: a full diagnostic pass over a
hundred gigabyte database completes in under five hundred milliseconds, makes fewer
than twenty system calls, and reads fewer than sixty four kilobytes.

## How to run it

```bash
cmake --build build
ctest --test-dir build -R DiagnoseBenchmark --output-on-failure
```

## How the hundred gigabyte database is made

The file is a real SQLite database built by the shared test helper, then grown with
a sparse region:

```cpp
std::fstream growingFile(databasePath, std::ios::in | std::ios::out | std::ios::binary);
growingFile.seekp(HUNDRED_GIGABYTES - 1);
growingFile.put('\0');
```

A sparse region costs almost nothing on disk, so the test runs in seconds while the
file system reports a hundred gigabytes. That is the point: a pass that read any
meaningful fraction of the file could not possibly finish inside the budget, so the
pass cannot be cheating.

## What is asserted

| Assertion | Why it is there |
|---|---|
| The file system really reports more than 90 GB | Otherwise the test proves nothing |
| The report carries that same size | The size came from metadata, not from reading |
| Fewer than 20 metadata calls | The call budget |
| Under 500 milliseconds | The time budget |
| A 10 MB file and a 100 GB file cost the same number of calls | The cost does not depend on the size |
| Four databases still fit in the call budget | A machine with every agent installed |
| The integrity check is skipped above 2 GB, and the report says so | Skipping silently would be dishonest |

## What it does not measure

The page by page integrity check, because that check is O(pages) by definition. It
is exercised on demand with `--check` and on small databases by default.

## Results

See `results.md`.
