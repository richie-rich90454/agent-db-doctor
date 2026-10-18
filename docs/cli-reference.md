# Command line reference

Every command, every flag, and every exit code, as the program itself registers
them. `--help` on any subcommand prints the same list.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | The command finished. For `diagnose`, this means nothing needs attention. |
| 1 | The command refused to run, or a database needs attention, or a run failed. |
| 2 | The command line itself was wrong. |

## Commands

| Command | Alias | What it does | Changes data |
|---|---|---|---|
| `diagnose` | `look` | Reports size, contents, and health | No |
| `compact` | | Event log compaction: removes redundant snapshots | Yes, after a copy and a confirmation |
| `vacuum` | | Rewrites the database at its smallest useful size | Yes, after a copy and a confirmation |
| `recover` | `rescue` | Explains what is wrong and what can be fixed | Only with `--rebuild` |
| `backups` | | Lists, verifies, restores, or deletes safety copies | `restore` and `delete` only |
| `service` | | Installs and controls the background watchdog | No |
| `agents` | | Lists every supported agent and where its data was found | No |

Every flag is registered only on the commands that read it, so an option that
appears in a help text is an option that works.

## Shared flags

These are available on `diagnose`, `compact`, `vacuum`, `recover`, and `agents`,
except where noted.

| Flag | Meaning |
|---|---|
| `-d, --database PATH` | The file to work on. Leave it out and the tool finds it. |
| `-a, --agent NAME` | Only look at this coding agent, for example `OpenCode`. |
| `-q, --quiet` | Print only the lines that matter. |
| `-v, --verbose` | Print extra detail: the paths the discovery considered, and the settings a run uses. |
| `--machine-readable` | Print one JSON object per line instead of sentences. See [json-api.md](json-api.md). |

## diagnose (look)

```
agent-db-doctor diagnose [flags]
```

| Flag | Meaning |
|---|---|
| `--check` | Run the full page by page integrity check. It reads the whole file, so it is skipped by default above 2 GB, and the report says so when it is. |
| `--trace-reads` | Also report how many bytes and read calls the run made, counted through a wrapper around SQLite's own file layer. |

Reports the size, the page counts, the free pages, the journal mode, the row counts
it can answer cheaply, the schema generation, and the integrity status. The exit
code is one when anything needs attention.

## compact

```
agent-db-doctor compact [flags]
```

| Flag | Meaning |
|---|---|
| `-y, --yes` | Do not ask before acting. Required with no terminal. |
| `-n, --dry-run` | Report what would happen and change nothing. A preview opens the database read only and writes nothing at all. |
| `-k, --keep N` | How many of the most recent sessions to keep. Zero, the default, keeps the newest half. |
| `--max-fraction F` | Refuse to remove more than this share of the event table. Default `0.5`. The guard runs even when SQLite's statistics table is missing. |
| `--batch-size N` | Rows removed per transaction. Default `10000`. |
| `--backup-to PATH` | Where to write the safety copy. Naming a path keeps the copy. |
| `--keep-backup` | Keep the safety copy after a successful run. By default it is removed once the checks pass, and the bytes are reported. |

Refuses to run when the message text might live only in the rows it would remove,
when the schema is one it does not know, when another process has a change in
progress, when the change would be larger than `--max-fraction`, or when a safety
copy cannot be made. Every refusal exits with code one and changes nothing.

## vacuum

```
agent-db-doctor vacuum [flags]
```

Takes the same confirmation, preview, and backup flags as `compact`. It writes the
smaller database beside the original, runs SQLite's integrity check on it, and only
then swaps it in, moving the original and its side files aside rather than deleting
them. The swap is journaled: an interruption between any two of its steps is
resolved on the next run.

The free space needed is about the size of the file, because the new file exists
beside the old one until the swap. It refuses, before anything is written, when
that space is not there.

When the safety copy is kept, the report states the net change in free space
rather than letting the reclaimed figure stand alone.

## recover (rescue)

```
agent-db-doctor recover [flags]
```

| Flag | Meaning |
|---|---|
| `--rebuild` | Build a working copy holding every row that can still be read. The original is never modified. |

The plan names what happened, what is still readable, and what no program can bring
back. Recognised patterns include an interrupted write, a database damaged while
open, a file compressed or made sparse while the agent had it open, and the
signature of the failure this tool was written for.

## backups

```
agent-db-doctor backups [list|verify|restore|delete] [flags]
```

| Flag | Meaning |
|---|---|
| `--file PATH` | Act on this copy instead of the ones beside the database. |
| `--all` | For `verify`, test every copy rather than the newest. For `delete`, remove every copy. |
| `--older-than DAYS` | For `delete`, remove copies older than this many days. Required unless `--all` is given. |
| `-y, --yes` | Do not ask before a restore or a delete. |

`list` is the default. `verify` is a restore drill: it copies a copy to a scratch
file, opens it, runs the integrity check, counts its sessions, and removes the
scratch file. `restore` moves the current database aside, puts the copy in its
place, and checks the result. `delete` names exactly what it will remove and asks.

## service

```
agent-db-doctor service [install|uninstall|start|stop|status|run] [flags]
```

| Flag | Meaning |
|---|---|
| `--warning-threshold BYTES` | The size at which the watchdog mentions a database. Default 50 GB. |
| `--interval SECONDS` | Seconds between checks, at least ten and at most seven days. Default 3600. |

`run` is the loop the operating system runs on the service's behalf. It never
returns and is not meant to be run by hand.

## agents

```
agent-db-doctor agents [flags]
```

| Flag | Meaning |
|---|---|
| `--verify` | Also show every candidate path that was looked at, including the ones that were not found. `--verbose` does the same. |

Lists each supported agent, what kind of storage it keeps, and the paths found on
this machine. Exit code zero whatever the answer: having no agent installed is not
a failure.
