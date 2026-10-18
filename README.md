# agent-db-doctor

<img src="assets/logo.svg" width="128" alt="The agent-db-doctor mark: three database rings threaded by a heartbeat line ending in a spark">

**A doctor for AI coding agent databases. Diagnose, compact, and recover.**

[![CI](https://github.com/richie-rich90454/agent-db-doctor/actions/workflows/ci.yml/badge.svg)](https://github.com/richie-rich90454/agent-db-doctor/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![Platforms: Windows, macOS, Linux](https://img.shields.io/badge/platforms-Windows%20%7C%20macOS%20%7C%20Linux-blue)

AI coding agents keep your work in local SQLite databases and JSONL files. They are
built for streaming, not for keeping: every partial response, every tool call, and
every edit is written as a full snapshot. A developer watching their OpenCode
database saw it grow from **40 GB to 124 GB in three weeks**, unnoticed, until
their disk was 96% full. When they tried to reclaim the space they made it worse,
and by the end **every session from three weeks was unrecoverable**.

This tool exists so that the next person does not lose theirs.

---

## The 90 second version

```bash
# 1. See what you have. Nothing is changed, and the pass is measured in
#    milliseconds and kilobytes, not minutes and gigabytes.
agent-db-doctor look

# 2. See where the bytes actually are, and what can be handed back.
agent-db-doctor diagnose --database ~/.local/share/opencode/opencode.db --trace-reads

# 3. Remove the redundant snapshots, with a safety copy that is removed again
#    once the check passes.
agent-db-doctor compact --database ~/.local/share/opencode/opencode.db

# 4. Hand the space the file only marked as reusable back to the drive.
agent-db-doctor vacuum --database ~/.local/share/opencode/opencode.db
```

On a real 10.9 GB OpenCode database, that flow removed **415,856 redundant
snapshots**, left all **479 sessions untouched and verified**, and handed
**8.3 GB (76.3%) back to the drive**. The check that follows the deletion re-reads
the oldest sessions and takes the safety copy back if anything is missing.

---

## What you get

| | |
|---|---|
| **A diagnostic pass that reads nothing** | 13 ms on a 100 GB database, 2 file system queries, and fewer than 64 KB read. The byte count is not a promise: it is counted through SQLite's own file layer and asserted by the test suite, and you can see it yourself with `--trace-reads`. |
| **Event log compaction** | The operation Kafka calls log compaction, for the one agent whose event log layout is documented: keep the newest snapshot of every message, remove the older copies, keep every word of the conversation. |
| **Vacuuming for every SQLite agent** | Seven agents keep SQLite databases now, and giving unused space back needs no knowledge of a schema, so it is offered for all seven. |
| **A safety copy that is honest** | Made before anything changes, verified after, and removed again once the check passes so it cannot cancel out the space the run reclaimed. `--keep-backup` keeps it. A failed check puts it back, for real, and re-checks the result. |
| **A restore drill** | `backups verify` copies a backup to a scratch file, opens it, runs the integrity check, counts its sessions, and removes the scratch file. A copy you cannot restore is not a safety net. |
| **Machine readable output** | `--machine-readable` turns every report into one JSON object per line: every number the commands carry, typed, with a final exit object. The graphical interface reads the same objects. |
| **Watchdog monitoring** | A system service (systemd, launchd, or the Windows service manager) that measures every database once an hour with one metadata call each, logs when a threshold is crossed, and remembers each database's schema layout so a diagnose can notice when an agent changes it. |

## What makes it safe

Every destructive operation does the same things, in this order, and a failure at
any step stops before the next one begins:

1. **Refuses to run** if the message text might live only in the rows it is about
   to delete, if another process has a change in progress, or if the database is
   damaged.
2. **Copies the file**, including the side files, as a file system clone where the
   platform supports one, and prints where the copy is.
3. **Asks**, in plain language, and treats anything other than an exact `yes` as no.
4. **Deletes in batches** inside transactions, checking every batch, so an
   interruption leaves a consistent database. The test suite kills a compaction
   partway through and checks exactly that.
5. **Verifies afterwards** that the oldest sessions still hold their text, and puts
   the copy back if they do not, then checks the restored database again.

The vacuum is built the same way: a new file is written beside the original, checked
with SQLite's own integrity check, and only then swapped in. The swap is journaled,
so a power loss between any two of its steps is detected and resolved on the next
run. The original is moved aside rather than deleted, and its side files move with
it.

The full story is in [docs/safety.md](docs/safety.md).

---

## Installation

```bash
# Today, from source. Package managers are planned.
git clone https://github.com/richie-rich90454/agent-db-doctor
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

Everything else is vendored, and every download is pinned by hash or by commit.

</details>

## Commands

| Command | What it does | Changes your data? |
|---|---|---|
| `diagnose`, `look` | Reports size, contents, and health | No |
| `compact` | Event log compaction: removes redundant snapshots | Yes, after a copy and a confirmation |
| `vacuum` | Rewrites the database at its smallest useful size | Yes, after a copy and a confirmation |
| `recover`, `rescue` | Explains what is wrong and what can be fixed | Only with `--rebuild` |
| `backups` | Lists, verifies, restores, or deletes safety copies | Only `restore` and `delete` |
| `service` | Installs and controls the background watchdog | No |
| `agents` | Lists every supported agent and where its data was found | No |

Every flag is documented in [docs/cli-reference.md](docs/cli-reference.md), and the
exit codes are part of the contract: `0` for success, `1` for a refusal or a
problem, `2` for a mistake in the command line.

## Machine readable output

```bash
agent-db-doctor diagnose --database ~/.local/share/opencode/opencode.db --machine-readable --quiet
```

```json
{"event":"database","path":"...","agent":"OpenCode","sizeBytes":3045969920,"pageCount":743645,"freePageCount":362934,"reclaimableBytes":1486577664,"journal":"wal","autoVacuum":false,"compactionSupported":true,"schema":"second generation rows only; the older duplicate snapshots are absent","integrity":"not_checked","rows":{"session":{"count":536,"approximate":false}},"warnings":[...],"nextSteps":[...]}
{"event":"exit","exitCode":0}
```

One object per line, flushed as it is written, so a script can follow a long
operation. The full schema is in [docs/json-api.md](docs/json-api.md).

---

## Supported agents

Sixty two agents are supported: seven keep SQLite databases and are diagnosed and
vacuumed, and the rest keep text or configuration and are diagnosed and left alone.

| Agent | Database location | What the tool does |
|---|---|---|
| OpenCode | `~/.local/share/opencode/opencode.db` | Diagnose, compact the event log, vacuum |
| Cursor | platform-specific, one SQLite file per workspace | Diagnose and vacuum |
| VS Code | platform-specific, one `state.vscdb` per workspace | Diagnose and vacuum |
| Cline | `~/.cline/data/db/sessions.db` | Diagnose and vacuum |
| Windsurf | platform-specific data dir | Diagnose and vacuum |
| Zed | platform-specific user data dir | Diagnose and vacuum |
| Void | platform-specific data dir | Diagnose and vacuum |
| Codex CLI | `~/.codex/sessions/` (JSONL) | Diagnose only |

The other fifty four, with the path each one uses and what the tool may do to it,
are in [docs/agents.md](docs/agents.md). `agent-db-doctor agents --verify` prints
every candidate path on your machine, including the ones that were not found.

Only OpenCode stores redundant snapshots, so only OpenCode's event log is
compacted. Every SQLite database can be vacuumed, because giving unused space back
needs no knowledge of the tables' contents.

## Performance

Every number below is measured by the benchmark suite, and every one of them is an
assertion in `tests/benchmark/`, so a regression fails the build rather than
quietly arriving in a release.

| Measurement | Target | Measured | How |
|---|---|---|---|
| Diagnostic pass, 100 GB database | < 500 ms | **13 ms** | `DiagnoseBenchmark` |
| Whole `diagnose` command on that file, process start included | < 500 ms | **29 to 41 ms** | measured over 3 runs |
| File metadata queries per diagnostic pass | < 20 | **2** | `DiagnoseBenchmark` |
| Bytes read from the database per pass | < 64 KB | **49 KB measured on a real 2.8 GB database**, counted through SQLite's own file layer | `DiagnoseBenchmark` |
| Metadata calls per watchdog tick, per database | 1 | **1** | `WatchdogBenchmark` |
| Memory growth over 1000 idle watchdog ticks | ~0 | **< 1 MB** | `WatchdogBenchmark` |
| Marginal cost of removing one redundant snapshot | 5.3 us | **8.0 us** Windows, **3.0 us** Linux | `CompactBenchmark` |

See [benchmarks/results.md](benchmarks/results.md) for the machine and the full
method.

---

## Documentation

| File | What is in it |
|---|---|
| [docs/index.md](docs/index.md) | The documentation home, and where to start |
| [docs/getting-started.md](docs/getting-started.md) | Install, first run, the four jobs |
| [docs/cli-reference.md](docs/cli-reference.md) | Every command, every flag, every exit code |
| [docs/json-api.md](docs/json-api.md) | The machine readable output contract |
| [docs/safety.md](docs/safety.md) | The guarantees, and how each one is enforced |
| [docs/agents.md](docs/agents.md) | All sixty two agents, and how to add one |
| [docs/faq.md](docs/faq.md) | Why not just delete the database, and other questions |
| [docs/architecture.md](docs/architecture.md) | How the code is put together |
| [docs/performance.md](docs/performance.md) | The performance contract, principle by principle |
| [docs/recovery-playbook.md](docs/recovery-playbook.md) | Every failure pattern the tool recognises |
| [docs/agent-adapters.md](docs/agent-adapters.md) | How each agent stores data, for contributors |
| [docs/system-service.md](docs/system-service.md) | The watchdog on each platform |
| [docs/roadmap.md](docs/roadmap.md) | What is planned next |
| [benchmarks/results.md](benchmarks/results.md) | Measured numbers and the machine they came from |

## What this tool does not do

The list is here because knowing where a tool stops is more useful than being
surprised by it later.

**It does not compact any agent except OpenCode.** Only OpenCode produces redundant
snapshots, and inventing a deletion rule for another agent's undocumented tables
would be guessing with someone's history.

**It does not walk a directory tree.** Finding databases uses a fixed list of
candidate paths, one metadata call each.

**It does not check every page of a database over two gigabytes** unless asked with
`--check`, and it says so rather than skipping silently.

**It does not report an exact row count for a database that has never been
analysed.** The count comes from SQLite's statistics table, which is one indexed
read. When that table has never been populated, the report says the count is
unknown rather than spending minutes counting rows.

**It does not turn on filesystem compression, and it does not repair its effects.**
Compressing a live SQLite database is what caused the emergency this tool was
written for.

**It does not delete a safety copy that a failed run may need**, and it never
deletes a database file without an explicit confirmation and a copy.

---

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The one rule that is not negotiable: nothing
you add may destroy a user's session history without their explicit confirmation.

Every change is built and tested by the workflow in `.github/workflows/ci.yml` with
GCC and Clang on Linux, MSVC on Windows, and AppleClang on macOS.

## License

[MIT](LICENSE)

## Acknowledgements

- The SQLite authors, for the database engine this tool is built on and for
  `quick_check` and `VACUUM INTO`, which do the two hardest jobs here.
- The authors of OpenCode, Codex CLI, Claude Code, Cursor, Continue, and Aider,
  whose storage design created the problem.
- Everyone who has ever lost work to a full disk and had nobody to ask.

## Trademark disclaimer

OpenCode, Cursor, VS Code, Cline, Roo Code, Reasonix, Gemini, Windsurf, Zed,
GitHub Copilot, Warp, Kilo Code, Augment, Trae, PearAI, Amp, CommandCode, Pi,
Void, Tabnine, Cody, Codeium, Supermaven, Bito, CodeGPT, Blackbox, AskCodi,
Phind, IntelliCode, Tabby, Pieces, Amazon Q, Junie, Qoder, Lingma, Comate,
Droid, GitLab Duo, WorkBuddy, Kimi, Kiro, Qwen Code, CodeBuddy, MarsCode,
Qodo, Sourcery, Snyk, SonarQube, AnythingLLM, GPT4All, Jan, LM Studio,
Open WebUI, AIChat, gptme, LLM, ShellGPT, Raycast, and all other product names
mentioned here are trademarks of their respective owners. They appear in this
project for one purpose only: to identify where those programs keep their data
so this tool can find it. There is no affiliation with and no endorsement by
any of them, and the names are not used for any other purpose.
