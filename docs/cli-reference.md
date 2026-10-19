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
| `explain-space` | | Divides the file's bytes between tables, indexes, and (with `--sessions`) conversations | No |
| `history` | | Reports the growth the watchdog recorded, and when the drive would fill | No |
| `why` | | Ranks the causes of one database's size and names the command worth running next | No |
| `export` | | Writes one session out as Markdown, JSON, or HTML | No |
| `sessions` | | Lists the largest conversations, or deletes one after copying and checking | `delete` only |
| `reclaim` | | Reaches a space target: compaction first, then the vacuum | Yes, after a copy and a confirmation |
| `watch` | | A live table of every database, sorted by size, with the growth between refreshes | No |
| `compact` | | Event log compaction: removes redundant snapshots | Yes, after a copy and a confirmation |
| `vacuum` | | Rewrites the database at its smallest useful size | Yes, after a copy and a confirmation |
| `recover` | `rescue` | Explains what is wrong and what can be fixed | Only with `--rebuild` |
| `backups` | | Lists, verifies, restores, or deletes safety copies | `restore` and `delete` only |
| `service` | | Installs and controls the background watchdog | No |
| `audit` | | Shows the record of destructive runs and automatic maintenance | No |
| `agents` | | Lists every supported agent and where its data was found | No |

Every flag is registered only on the commands that read it, so an option that
appears in a help text is an option that works.

## Shared flags

These are the options most commands have in common.

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
| `-o, --report PATH` | Also write a standalone HTML report of the whole pass to this path. |
| `--prometheus` | Also write node_exporter textfile metrics - `agent_db_doctor_size_bytes`, `agent_db_doctor_reclaimable_bytes`, `agent_db_doctor_healthy`, and `agent_db_doctor_health_score` - each labelled with the agent and the path. |

Reports the size, the page counts, the free pages, the journal mode, the row counts
it can answer cheaply, the schema generation, the health score, and the integrity
status. The health score runs from 0 to 100; diagnose prints it as
`Health: 78 of 100`, the machine readable output calls it `healthScore`, damage,
warnings, unused space, and growth each reduce it, a healthy idle database scores
100, and a score of -1 means it could not be computed. The exit code is one when
anything needs attention.

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

| Flag | Meaning |
|---|---|
| `--enable-auto-vacuum` | Also convert the database to incremental auto-vacuum, so later reclaims hand pages back in place. This is a persistent setting stored in the file, applied during the rewrite. |

Takes the same confirmation, preview, and backup flags as `compact`. It writes the
smaller database beside the original, runs SQLite's integrity check on it, and only
then swaps it in, moving the original and its side files aside rather than deleting
them. The swap is journaled: an interruption between any two of its steps is
resolved on the next run.

A database that was set up for incremental auto-vacuum takes the cheap path
instead: the unused pages are released in place with SQLite's own
`PRAGMA incremental_vacuum`, so no second file is written and no free space is
needed. The command says which path it took, and `--enable-auto-vacuum` is how a
database is converted the first time.

The free space needed for the rewrite is about the size of the file, because the new
file exists beside the old one until the swap. It refuses, before anything is
written, when that space is not there.

When the safety copy is kept, the report states the net change in free space
rather than letting the reclaimed figure stand alone.

## explain-space

```
agent-db-doctor explain-space [flags]
```

| Flag | Meaning |
|---|---|
| `--sessions N` | Also rank the N largest conversations by the payload they store. This reads the part table. |

Reports the bytes each table and index occupies, largest first, read from SQLite's
own page accounting, plus the free pages waiting inside the file. With `--sessions`
it ranks the conversations by the payload they store, which is what a session
deletion gives back.

## history

```
agent-db-doctor history [flags]
```

| Flag | Meaning |
|---|---|
| `--days N` | How many days of recorded sizes to report. The default is 30. |
| `-d, --database PATH` | One database instead of every database the record knows about. |

The sizes come from the background watchdog, which records one size per database per
hour, and only when the size changed. The report ends with a straight-line forecast:
the rate per day, and about how many days the drive's free space lasts at that rate.
Nothing is recorded by this command; it only reads.

## why

```
agent-db-doctor why --database PATH
```

Runs the cheap parts of diagnose, the page accounting, and the growth record, then
ranks the causes by what they would give back: unused pages inside the file first
(exact, and a vacuum returns all of them), the redundant snapshots next (counted
exactly; the byte count comes from the compact preview), then the stored content
itself. It ends with the one command worth running next.

## export

```
agent-db-doctor export --database PATH --session ID [flags]
```

| Flag | Meaning |
|---|---|
| `--session ID` | The session to write out. `sessions` or `explain-space --sessions` prints identifiers. |
| `--format NAME` | `markdown`, `json`, or `html`. The default is markdown. |
| `-o, --output PATH` | Where to write the document. A single dash means standard output, which is the default. |

The document carries the session's messages in order, with the text of each message
and the name of every tool that ran. It reads only.

## sessions

```
agent-db-doctor sessions [list|delete] [flags]
```

| Flag | Meaning |
|---|---|
| `--top N` | How many sessions the listing shows. The default is 10. |
| `--session ID` | The session to delete, for the delete action. |
| `-y, --yes` | Do not ask before deleting. |

`list` is the default and ranks the conversations by stored payload. `delete`
removes one conversation after making a safety copy, asking for a typed `yes`,
running the deletion in one transaction, and checking that every other sampled
conversation still holds its readable text. A failed check puts the copy back and
checks the restored file again.

Deleting is refused on a database whose second generation tables are in use,
because their layout is not documented anywhere this tool trusts; the compact and
vacuum commands are safe there instead.

## reclaim

```
agent-db-doctor reclaim --database PATH --target SIZE [flags]
```

| Flag | Meaning |
|---|---|
| `--target SIZE` | How much space to give back, for example `20GB`. Accepts KB, MB, GB, TB, or a plain number of bytes. |

Takes the confirmation and backup flags, and the compaction settings (`-k`,
`--max-fraction`, `--batch-size`), which are passed to the compaction step. The
command works out the cheapest safe sequence — compaction first, because it makes
the vacuum smaller and faster — prints the plan, asks once, and then runs the
compact and vacuum commands with the full safety machinery each. It ends by
reporting how many bytes the drive actually got back, and whether the target was
met; when it was not, what is left is the conversation itself, and the report names
the session tools for going further.

## watch

```
agent-db-doctor watch [flags]
```

| Flag | Meaning |
|---|---|
| `--once` | Render one snapshot and exit, for a script. |
| `--interval N` | How many seconds between refreshes. The default is five. |

A live table of every discovered database, sorted by size, with the growth since
the last refresh beside each one. The sizes come from file metadata: no database is
ever opened, so the view can run for days without touching anything. `--once` and
`--machine-readable` render one snapshot and exit.

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

The configuration file it reads carries the thresholds, the per-database
`databasePolicies`, and the notification permissions;
[system-service.md](system-service.md) documents every field.

## audit

```
agent-db-doctor audit [list|tail] [flags]
```

| Flag | Meaning |
|---|---|
| `--limit N` | How many entries to show. |

`tail` is the default and prints the most recent entries of the audit log; `list`
prints the whole log. The log is an append-only, tab separated file beside the
service configuration, with one line per destructive run and per step automatic
maintenance takes: the time, the action, the path, the sizes before and after, the
safety copy's path, the outcome, and the exit code. It is kept for one hundred and
eighty days and capped in size, and it is written when its location is writable
and always by the service. `--machine-readable` turns each entry into one JSON
object.

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
