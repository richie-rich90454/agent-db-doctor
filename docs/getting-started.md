# Getting started

## Building it

```bash
git clone https://github.com/richie-rich90454/agent-db-doctor
cd agent-db-doctor
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary lands at `build/bin/agent-db-doctor` (`agent-db-doctor.exe` on Windows).
Put it somewhere on your path, or call it by its full path. The configure step
needs the network once, to fetch five dependencies, and every one of them is pinned
by hash or by commit.

To skip the graphical interface, which needs SDL3 and Dear ImGui, configure with
`-DAGENT_DB_DOCTOR_BUILD_GUI=OFF`. The command line program is unaffected.

## The first run

```bash
agent-db-doctor look
```

This diagnoses every database it can find and changes nothing. It prints, for each
one, the size, what is inside it, how it stores changes, and whether anything is
wrong. The closing line counts the databases that are healthy and the ones that
need attention, and the process exit code is one when anything needs attention,
which is what makes it usable from a monitoring script.

Nothing was installed and nothing was created: the pass uses a fixed list of
candidate paths, one metadata call each, and reads a few dozen kilobytes from each
database it opens.

## The four jobs

The tool has four jobs, and the subcommands are named after them. Each precise
command is also listed, because scripts prefer precision.

### 1. Look

```bash
agent-db-doctor look                      # everything, in one pass
agent-db-doctor diagnose --database /path/to/opencode.db --trace-reads
```

`--trace-reads` reports how many bytes were actually read, counted through a
wrapper around SQLite's own file layer. On a real 2.8 GB OpenCode database the
whole pass reads about 49 kilobytes.

### 2. Reclaim

```bash
agent-db-doctor reclaim --database ~/.local/share/opencode/opencode.db --target 20GB
```

The target is how much space you want back, not which operation to run. The command
works out the cheapest safe sequence — compaction first, because it makes the vacuum
smaller and faster — shows the plan, asks once, and then runs the compact and vacuum
commands with the full safety machinery each: a copy before anything changes, a
typed confirmation once, a check afterwards, and the copy removed again when its
check passes. It ends by reporting how many bytes the drive actually got back.

The two operations are also available on their own when you want to see each step:

```bash
agent-db-doctor compact --database ~/.local/share/opencode/opencode.db
agent-db-doctor vacuum  --database ~/.local/share/opencode/opencode.db
```

`compact` is event log compaction: it keeps the newest snapshot of every message
and removes the older copies, which is where most of the bloat comes from.
`compact` frees space *inside* the file; `vacuum` is what hands it back to the
drive, by writing a smaller database beside the original, checking it, and swapping
it in. A database that was set up for incremental auto-vacuum takes a cheaper path
that releases the pages in place; `vacuum --enable-auto-vacuum` is how a database
is converted, once.

Neither command needs `--yes` when you run it in a terminal: the question is asked
there, and anything other than exactly `yes` stops.

### 3. Understand it

```bash
agent-db-doctor why --database ~/.local/share/opencode/opencode.db
agent-db-doctor explain-space --database ~/.local/share/opencode/opencode.db --sessions 10
agent-db-doctor history
agent-db-doctor watch
```

`why` ranks the causes of the size and names the one command worth running next.
`explain-space` divides the file's bytes between tables, indexes, and the largest
conversations. `history` turns the watchdog's record into a rate and a forecast.
`watch` is a live table that opens no database at all.

### 4. Rescue

```bash
agent-db-doctor rescue --database ~/.local/share/opencode/opencode.db
```

The recovery plan names what happened, what is still readable, and what no program
can bring back. `--rebuild` builds a working copy holding every row that can still
be read; the original is never modified.

### 5. Watch

```bash
agent-db-doctor service install    # needs administrator rights
agent-db-doctor service status
```

The watchdog checks every supported database once an hour, with one metadata call
per database, and writes one line to its log when one crosses a threshold. Once an
hour it also records each database's size, which is what `history` reports, and its
schema layout, so a later diagnose can warn when an agent has changed its schema and
the tool's knowledge of it may be out of date. See
[system-service.md](system-service.md).

## Keeping it healthy

The watchdog can act, not only report. With `allowAutomaticCompaction` true -
globally or for one database through `databasePolicies` - a database above its
critical threshold, idle for fifteen minutes, and not locked by another process is
compacted (when there is anything to compact) and then vacuumed by the service,
each with its own safety copy. Every step lands in the audit log beside the
service configuration:

```bash
agent-db-doctor audit --limit 20
```

A desktop notification is attempted when a threshold is crossed and after
automatic maintenance, best effort; the log line is always written as well. See
[system-service.md](system-service.md).

## Reading a conversation before deleting it

```bash
agent-db-doctor sessions --database ~/.local/share/opencode/opencode.db --top 10
agent-db-doctor export --database ~/.local/share/opencode/opencode.db --session <id> --output session.md
agent-db-doctor sessions delete --database ~/.local/share/opencode/opencode.db --session <id>
```

`sessions` ranks the conversations by the payload they store. `export` writes one
out as Markdown, JSON, or HTML, which is the copy to keep before anything is
deleted. `delete` removes one conversation with the same copy, typed `yes`, and
sampled check the other destructive commands use, and refuses on a database whose
second generation tables are in use.

## The safety copies

```bash
agent-db-doctor backups list --database ~/.local/share/opencode/opencode.db
agent-db-doctor backups verify --database ~/.local/share/opencode/opencode.db
agent-db-doctor backups restore --database ~/.local/share/opencode/opencode.db
agent-db-doctor backups delete --older-than 7 --database ~/.local/share/opencode/opencode.db
```

`verify` is a restore drill: it copies the backup to a scratch file, opens that
copy, runs SQLite's integrity check, counts its sessions, and removes the scratch
file. The database itself is never touched.

`restore` moves the current file aside first, so nothing is lost either way, and
checks the restored file afterwards.

## When something refuses

Every refusal names the cause and the next command. The common ones:

- **"Another program has this database open"** means the agent is running, or
  another process has a change in progress. Close it and run the command again.
- **"not enough free space"** means the vacuum needs about the size of the file
  free while it works, because it writes a new file before replacing the old one.
  Compacting first makes the vacuum smaller.
- **"this database is not laid out in a way the tool recognises"** means the agent
  changed its schema, or the file belongs to a version of the agent that this
  version of the tool does not know. Nothing is changed; the
  [recovery playbook](recovery-playbook.md) explains the options.
