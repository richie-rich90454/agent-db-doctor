# agent-db-doctor

**A doctor for AI coding agent databases. Diagnose, prune, recover, and monitor.**

AI coding agents keep your work in local SQLite databases and JSONL files. They are
built for streaming, not for keeping: every partial response, every tool call, and
every edit is written as a full snapshot. A developer watching their OpenCode
database saw it grow from **40 GB to 124 GB in three weeks**, unnoticed, until
their disk was 96% full. When they tried to reclaim the space they made it worse,
and by the end **every session from three weeks was unrecoverable**.

This tool exists so that the next person does not lose theirs.

---

## Installation

```bash
# Build from source (the only way to get it today)
git clone https://github.com/your-org/agent-db-doctor
cd agent-db-doctor
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary lands at `build/bin/agent-db-doctor`. Put it somewhere on your path.

<details>
<summary>Requirements</summary>

| Requirement | Version |
|---|---|
| Compiler | GCC 13+, Clang 16+, or MSVC 19.36+ |
| CMake | 3.24 or newer |
| Standard | C++23 |
| Network | Needed once, at configure time, to fetch the five dependencies |

Everything else is vendored. There is no `boost`, no ORM, and no JSON library.

</details>

<details>
<summary>What has actually been verified, and where</summary>

The table below is what has been built and run, not what the code is intended to do.

| Platform | Built | Full suite run | Notes |
|---|---|---|---|
| Windows 11, GCC 16.2, MinGW | yes | 22 of 22 pass | The reference platform. Every measurement in this file is from here. |
| Ubuntu 24.04, GCC 13.3 | yes | 22 of 22 pass | A second toolchain, and it caught four real bugs the Windows build never could. |
| macOS | sources compile-checked | not run | See below. |

**The macOS sources are compiled on every Linux build, and linked into nothing.**
That is deliberate. The macOS implementation is portable POSIX apart from one branch
naming the modification time field, so it can be compiled anywhere with Berkeley
headers. Building it as a check caught four mistakes that had been sitting in those
files for the life of the project: a missing include, a namespace qualification that
resolved to `AgentDbDoctor::AgentDbDoctor`, and a function defined below its own call
site. It is the same class of bug the Linux build found, and both went unnoticed
because neither file had ever been compiled by anything.

What cannot be done from a Windows machine is run the thing on a Mac, and this file
does not claim otherwise. Two specific things are therefore unverified on macOS:
that the `launchctl` calls behave as documented, and that the service installs and
starts under launchd. Everything the tool does without root, which is the whole of
`diagnose`, `prune`, `compact`, and `recover`, uses only `stat`, `statfs`, `clock_gettime`,
`getpid`, and `getenv`, and that code is compiled and unit-tested on Linux.

</details>

---

## Quick start

```bash
# 1. See what you have.
$ agent-db-doctor diagnose

OpenCode database: ~/.local/share/opencode/opencode.db
Size on disk:          257.7 MB
Page size:             4,096 bytes
Pages:                 65,977
Unused pages inside:   0 (0 bytes)
How it stores changes: wal - changes are written to a side file first, which is fast
Integrity:             every page checked out, the database is sound
  This check took 348 milliseconds and made 2 file system queries.

Summary
Looked at 2 databases. 2 healthy, 0 need attention.
```

```bash
# 2. Remove the duplicate rows, after copying your database.
$ agent-db-doctor prune --keep 200

What would be removed
Older copies of changed messages: 1,200
Sessions kept untouched:          200 of 412
A copy of your database was made first
Nothing has been changed yet. The copy is at:
  /home/ann/.local/share/opencode/opencode.db.backup-20260930-162913

About to remove 1,200 older copies of changed messages.
Type yes to go ahead, or anything else to stop: yes
```

```bash
# 3. Hand the space back to the drive.
$ agent-db-doctor compact

Size before: 257.7 MB
Size after:  198.4 MB
Given back to the drive: 59.3 MB (23.0% of the file)
```

---

## The three emergencies this tool was written for

### 1. "My disk is full and the agent database is enormous"

**Symptom:** the disk is nearly full, and the agent database is the largest thing
on it.

```bash
agent-db-doctor diagnose                    # what is it, and how bad
agent-db-doctor prune --keep 200            # remove the duplicates
agent-db-doctor compact                     # give the space back
agent-db-doctor service install             # so it never happens unannounced
```

`prune` alone will not shrink the file. Removing rows marks the space reusable;
only `compact` hands it to the file system. The tool says this after every prune,
because it is the single most common surprise.

### 2. "The agent will not open and I think I have lost everything"

**Symptom:** the coding agent hangs on launch, or the database will not open.

```bash
agent-db-doctor recover --database ~/.local/share/opencode/opencode.db
```

The first thing it prints is *stop using the computer*. That is not a formality: in
the real case this was written for, the database had already been damaged, and
every further write to the disk destroyed more of it. The plan that follows names
what happened, what is still recoverable, and what no program can bring back.

### 3. "I compressed my database to save space and now it is worse"

**Symptom:** the file was compressed, or marked sparse, while the agent had it
open. The agent now fails to save, and the file will not read.

```bash
agent-db-doctor recover --database ~/.local/share/opencode/opencode.db
```

See [Why filesystem compression is dangerous](#why-filesystem-compression-is-dangerous)
below, and `docs/recovery-playbook.md` for the full story.

---

## Why filesystem compression is dangerous

**Compressing a SQLite database does not make it smaller.** SQLite pages have no
repeated byte patterns for a compressor to find, so the algorithm writes new
clusters beside the old ones and the file gets *bigger*. The real failure was worse
than that:

1. A naive prune script deleted 791,846 rows. SQLite does not shrink a file on
   `DELETE`, so nothing was gained and the user assumed the script had failed.
2. NTFS `compact` with the default LZNT1 algorithm produced a **1.0:1 ratio**.
   124 GB in, 124 GB out.
3. LZX compression would have worked on text, so it was started. It needed more
   than the 36 GB that remained.
4. The machine was rebooted mid-write. The compression process was killed.
5. The file was left with a corrupted Master File Table entry. Explorer reported
   11 GB. NTFS reported 124 GB allocated. SQLite returned
   `ERROR_READ_FAULT (266)` on every read.
6. `chkdsk`, `sqlite3 .recover`, PhotoRec, and filesystem tools all failed. The data
   blocks had been overwritten by the partial write.

Three weeks of sessions, gone. Not from hardware failure, not from malware, not
from a mistake in the user's own code. From a database bloat problem that nothing
warned them about and nothing diagnosed correctly.

**This tool never uses filesystem compression, and the recover command names this
case explicitly when it sees the signature.**

---

## System service

The service is what turns this from a tool you remember into one that notices.

```bash
agent-db-doctor service install     # needs administrator rights
agent-db-doctor service status
```

It checks every supported agent database once an hour and writes one line to its
log when one crosses a threshold you choose. The check costs **one metadata system
call per database and reads zero bytes from them**, so it is safe to leave running
for months.

Configure it in plain text, with no need to rebuild:

```json
{
  "watchedDatabases": [],
  "warningThresholdBytes": 53687091200,
  "criticalThresholdBytes": 107374182400,
  "checkIntervalSeconds": 3600,
  "allowAutomaticPruning": false
}
```

`docs/system-service.md` has the paths on each platform and what every field does.

---

## Performance

Every number below is measured by the benchmark suite, and every one of them is an
assertion in `tests/benchmark/`, so a regression fails the build rather than
quietly arriving in a release.

| Measurement | Target | Measured | How |
|---|---|---|---|
| Diagnostic pass, 100 GB database | < 500 ms | **13 ms** | `DiagnoseBenchmark` |
| Whole `diagnose` command on that file, process start included | < 500 ms | **29 to 41 ms** | measured over 3 runs |
| File metadata queries per diagnostic pass | < 20 | **2** | `DiagnoseBenchmark` |
| Bytes read from the database per pass | < 64 KB | **100** | `DiagnoseBenchmark` |
| Metadata calls per watchdog tick, per database | 1 | **1** | `WatchdogBenchmark` |
| Memory growth over 1000 idle watchdog ticks | ~0 | **< 1 MB** | `WatchdogBenchmark` |
| Peak resident memory, whole process, full diagnostic | < 40 MB | **7.1 MB** | measured with the process working set |
| Startup, command line tool | < 30 ms | **28.3 ms** | average of 5 runs of `--version` |
| Marginal cost of removing one duplicate row | 5.3 us | **8.0 us** Windows, **3.0 us** Linux | `PruneBenchmark` |

Measured on the machine described in `benchmarks/results.md`, with the tool built
at `-O3` and SQLite compiled with `SQLITE_THREADSAFE=0`. The 100 GB database is a
real SQLite file grown to that size with a sparse region, so the pass had a genuine
hundred gigabyte file to answer questions about and could not have read it.

**One measurement is above its target on Windows, and inside it on Linux.** The
specification's figure of 5.3 microseconds per row would put 1.5 million rows inside
eight seconds. This build measures about 8 on Windows and about 3 on Linux. It used
to measure 20: keeping SQLite's scratch structures in memory rather than in temporary
files, and checkpointing passively instead of blocking on a truncate, moved it to 8.
The remainder is the operating system rather than the query, because for identical
SQL a row deletion costs about two and a half times more on Windows, where every
temporary file and log write passes through antivirus software.

The test prints the rate it measured, and asserts that tripling the work does not
triple the cost per row. That second assertion is the one that matters: it is a
ratio of two measurements taken at the same moment, so a busy machine cannot make it
fail, while a design that had become quadratic could not pass it. See
`docs/performance.md`.

**The startup figure is a measurement of the whole process, launcher included.**
`--version` takes 28.3 ms averaged over five runs from a PowerShell shell, which
includes starting PowerShell's child process. The tool's own work is well under
half of that. The specification's budget is 30 ms, so this is inside it, but only
just, and it is worth knowing that most of the figure is not the tool.

**No comparison figures for Rust, Go, or Python equivalents are given here.** No
such tool was available to measure on the same machine, and inventing numbers for
a comparison table would be the opposite of what this project is for. When one is
measured, the table gets a row.

---

## Supported agents

| Agent | Database location | What the tool does |
|---|---|---|
| OpenCode | `~/.local/share/opencode/opencode.db` | Diagnose, prune, compact |
| Cursor | platform-specific, one SQLite file per workspace | Diagnose only; its tables are undocumented |
| Codex CLI | `~/.codex/sessions/` (JSONL) | Diagnose only; no duplicate snapshots |
| Claude Code | `~/.claude/projects/` (JSONL) | Diagnose only; no duplicate snapshots |
| Continue | `~/.continue/` | Diagnose only; the index is rebuildable |
| Aider | `.aider.chat.history.md` | Diagnose only; a transcript is the only copy |

Only OpenCode stores redundant snapshots, so only OpenCode is pruned. The others are
reported and left alone, because a plain text transcript is the only copy of that
conversation and deleting anything from it would be the tool's decision to make,
not yours.

`docs/agent-adapters.md` explains how a new agent is added, which is a small change
in one place.

---

## Commands and flags

| Command | What it does | Changes your data? |
|---|---|---|
| `diagnose` | Reports size, contents, and health | No |
| `prune` | Removes older copies of changed messages | Yes, after a copy and a confirmation |
| `compact` | Rewrites the database to its smallest useful size | Yes, after a copy and a confirmation |
| `recover` | Explains what is wrong and what can be fixed | Only with `--rebuild` |
| `service` | Installs and controls the background watcher | No |

<details>
<summary>All flags</summary>

| Flag | Meaning |
|---|---|
| `-d, --database PATH` | The file to work on. Leave it out and the tool finds it. |
| `-a, --agent NAME` | Only look at this coding agent. |
| `-y, --yes` | Do not ask before changing something. Required with no terminal. |
| `-n, --dry-run` | Report what would happen and change nothing. |
| `-q, --quiet` | Print only the lines that matter. |
| `-v, --verbose` | Write extra detail to the log. |
| `-k, --keep N` | How many of the most recent sessions to keep when pruning. |
| `--max-fraction F` | Refuse to prune more than this share of the event table. Default `0.5`. |
| `--batch-size N` | Rows removed per transaction. Default `10000`. |
| `--backup-to PATH` | Where to write the safety copy. |
| `--warning-threshold N` | Size in bytes at which the service mentions a database. |
| `--interval N` | Seconds between service checks. |
| `--rebuild` | When recovering, build a working copy of what can still be read. |
| `--check` | Run the full page by page integrity check. |
| `--machine-readable` | Print numbers rather than sentences. |

</details>

---

## What makes it safe

Every destructive operation does the same five things, in this order, and a
failure at any step stops before the next one begins:

1. **Refuses to run** if the message text might live only in the rows it is about to
   delete.
2. **Copies the file**, including the `-wal` and `-shm` side files, and prints where
   the copy is.
3. **Asks**, in plain language, and treats anything other than an exact `yes` as no.
4. **Deletes in batches** inside transactions, so an interrupted run leaves a
   consistent database.
5. **Verifies afterwards** that the oldest sessions still hold the same text, and
   puts the copy back if they do not.

The tool also refuses to remove more than half the event table without being told
to, because a number that large usually means a miscount rather than a problem.

---

## Documentation

| File | What is in it |
|---|---|
| [docs/architecture.md](docs/architecture.md) | How the code is put together, and why it looks like Java |
| [docs/performance.md](docs/performance.md) | The performance contract, and how each principle is met |
| [docs/recovery-playbook.md](docs/recovery-playbook.md) | Every failure pattern the tool recognises, and what to do |
| [docs/agent-adapters.md](docs/agent-adapters.md) | How each supported agent stores data, and how to add one |
| [docs/system-service.md](docs/system-service.md) | Installing and configuring the service on each platform |
| [benchmarks/results.md](benchmarks/results.md) | Measured numbers and the machine they came from |
| [examples/diagnose-bloated-database.md](examples/diagnose-bloated-database.md) | A worked example, start to finish |

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The one rule that is not negotiable: nothing
you add may destroy a user's session history without their explicit confirmation.

This repository is private and is not configured for continuous integration,
because a hosted runner costs money. The suites run locally with:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## License

Dual licensed, at your option:

- [MIT](LICENSE-MIT)
- [Apache-2.0](LICENSE-APACHE)

## Acknowledgements

- The SQLite authors, for the database engine this tool is built on and for
  `quick_check` and `VACUUM INTO`, which do the two hardest jobs here.
- The authors of OpenCode, Codex CLI, Claude Code, Cursor, Continue, and Aider,
  whose storage design created the problem.
- Everyone who has ever lost work to a full disk and had nobody to ask.
