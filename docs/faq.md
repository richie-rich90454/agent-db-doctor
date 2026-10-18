# Frequently asked questions

## Why not just delete the database?

Because the database is the only copy of your conversations. Deleting it removes
everything, including the parts you still want, and there is nothing left to
recover from. Compaction removes only the redundant snapshots — the older copies of
messages that the agent left behind when the message changed — and keeps every
session, message, and word that was there.

The tool reports what it is about to remove, makes a copy, and asks before it
touches anything. If that is more ceremony than you want, `--yes` skips the
question, and the copy and the checks still happen.

## Is it safe to run while my coding agent is open?

`diagnose` and `agents` open databases read only and are safe at any time. The
destructive commands refuse to run when the agent is using the database: the tool
checks the file's locks, the side files SQLite keeps, and, on Linux and macOS,
whether another process has a transaction open. On Windows any open handle is
detected. On Linux and macOS an *idle* agent that holds no transaction cannot be
detected as reliably; close the agent before a destructive run.

## Where did my safety copy go?

If the run finished and its checks passed, the copy was removed and the bytes it
held were reported. That is deliberate: a copy of a ten gigabyte file cancels out
ten gigabytes of reclaimed space, and the point of the run was to reclaim space.
`--keep-backup` keeps the copy, `--backup-to` keeps it in a place you chose, and a
run that fails or is refused always keeps it. `agent-db-doctor backups list` shows
what is on disk.

## How is this different from running `VACUUM` myself?

`sqlite3 file.db "VACUUM"` rewrites the file in place. If it runs out of space or is
interrupted partway, you have a half-rewritten database and no copy. This tool
writes the new file beside the original, checks it with SQLite's own integrity
check, and only then swaps it in; the original and its side files are moved aside
rather than deleted, and the swap is journaled so an interruption is recoverable.

It also knows which databases are agent databases, refuses to work on one that is
in use, and measures a diagnostic pass in milliseconds rather than minutes.

## Does compacting change what my agent sees?

The event log keeps the same logical content: the newest snapshot of every message.
Older snapshots are removed, which is what the bloat is made of. The sessions,
messages, and stored parts — the tables the agent reads to show your conversation —
are never touched. After every deletion the tool re-reads a sample of the oldest
sessions and compares the readable text against what it saw before; if anything is
missing, the copy is put back and the database is checked again.

## Why is the reported row count "unknown"?

The count comes from SQLite's statistics table, which one indexed read can answer.
When that table has never been populated, which is the normal state of a database
nobody has run `ANALYZE` on, the exact count would mean reading the whole table.
The tool reports the number as unknown rather than spending minutes on it, and says
so in the same sentence. Running `ANALYZE` on the database makes the counts appear;
the tool will not run it for you, because it is a change to your database.

## Why does it say the schema layout changed?

The background service records a fingerprint of each database's schema once an hour.
When a later `diagnose` computes a different fingerprint, the agent has changed the
layout of its database, usually because it was updated. The tool still reads it, but
its knowledge of the layout — which columns mean what, which event types are older
copies — may be out of date. That is worth knowing before you act on a number, and
it is worth an issue report.

## What does the graphical interface do?

It reads the same machine readable objects the command line produces, and it runs
the command line program for anything that changes data. That is what keeps the two
front ends from disagreeing: there is one implementation of every operation, and
the window is a view of it.

## Does it work on a network drive or a synced folder?

SQLite on a network share is unreliable, and a cloud sync client writing to a
database while the agent has it open is one of the way databases get corrupted. The
tool will diagnose a database in such a folder and tell you what it sees, but the
honest advice is to keep agent databases on a local disk and out of a synced
folder.

## What does it cost to run?

The watchdog's tick is one metadata call per database, which is microseconds. A
diagnostic pass on a hundred gigabyte database is 13 milliseconds and reads fewer
than 64 kilobytes. Compaction is the expensive one: about 8 microseconds per row
removed on Windows and 3 on Linux, plus copying the file, which is a file system
clone on the platforms that support one.
