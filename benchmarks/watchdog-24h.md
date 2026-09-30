# Benchmark: the watchdog over twenty four hours

## What this measures

The specification's idle cost for the background service: under twelve megabytes of
resident memory, under a hundredth of one processor, and one metadata system call
per database per tick, forever.

The continuous integration runner does not wait twenty four hours, so this file
describes how to run that soak test, and the in-process benchmark measures the same
properties in seconds.

## The twenty four hour soak

```bash
# Build with the sanitizers the specification asks for.
cmake -S . -B build-soak -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build-soak

# Run the service directly rather than installing it, at the shortest interval the
# tool accepts, which is ten seconds. Twenty four hours at that interval is 8,640
# ticks. The tool refuses anything shorter and explains why rather than accepting it,
# so `--interval 1` will not work.
agent-db-doctor service run --interval 10
```

While it runs, sample the resident set size:

```bash
# Linux
while true; do
  ps -o rss= -C agent-db-doctor | awk '{print $1}' >> /tmp/rss.log
  sleep 60
done

# Windows
while ($true) {
  (Get-Process agent-db-doctor).PeakWorkingSet64 >> rss.log
  Start-Sleep 60
}
```

Then check the result:

```bash
sort -n rss.log | tail -1     # the peak, in kilobytes
head -1 rss.log              # the first sample, for comparison
```

**The assertion is that the peak never exceeds 12 MB.** Not that the average does,
and not that it usually does. A service that runs for months has to be flat.

## Two soaks, because one cannot answer both questions

A sanitized build and a resident memory measurement cannot come from the same run,
and this is worth being explicit about, because getting it wrong produces a number
that looks alarming and means nothing.

AddressSanitizer maintains a shadow map of the whole address space, and
UndefinedBehaviorSanitizer adds bookkeeping of its own. A binary built with both
reported **57 MB resident** in an early run of this soak while doing almost nothing.
That is the sanitizer, not the tool. Quoting it as the tool's memory cost would be
nonsense in the other direction: it would suggest the tool is five times heavier
than its budget, when the truth is that a sanitized binary is always several times
heavier than the program it instruments.

So the soak runs twice:

| Run | Build | What it answers |
|---|---|---|
| `/root/soak` | `-fsanitize=address,undefined` | Does the service read or write memory it should not, over 24 hours of ticks? |
| `/root/soak-release` | ordinary `-O3` release | What is the peak resident set the twelve megabyte budget is about? |

Read the memory figure from the release run and the correctness verdict from the
sanitized run. Mixing them produces a confident, wrong answer in either direction.

## The in-process benchmark

`tests/benchmark/WatchdogBenchmark.cpp` measures the same properties in under a
second, so a regression is caught on every run rather than after a day.

| Test | What it proves |
|---|---|
| a watchdog tick costs one call per database and nothing else | The call budget |
| a watchdog tick is far faster than its interval | The CPU budget |
| many idle ticks do not grow the process | The memory budget |
| a stop request ends the loop promptly | The service can be stopped |
| an interval below ten seconds is refused | The tool will not spin |

The memory test reads the process's own peak working set, which is the honest way
to check a resident memory figure, and compares it before and after a thousand
ticks. The allowance is one megabyte, because the figure is a peak and a single
spike would move it.

## What is not measured here

CPU percentage over twenty four hours. The in-process benchmark measures tick
*duration* instead, which is the quantity that determines the percentage: a tick
that finishes in microseconds, once an hour, is far below a hundredth of one
processor by an enormous margin, and measuring a percentage over a day on a build
machine would mostly measure the build machine.

## Results

See `results.md`.
