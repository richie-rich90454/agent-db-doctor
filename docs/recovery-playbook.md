# The recovery playbook

Every failure this tool recognises, what it means, and what to do about it. The
tool prints this information itself, in this order, when you run
`agent-db-doctor recover`. This document is the longer version.

The order matters, and it is not the order you would guess. The first thing to do
in every case is **stop writing to the disk**, because in every case below the
damage that is already done can be made permanent by one more write.

---

## The story this was written for

A developer running OpenCode on a large codebase watched their database grow from
40 GB to 124 GB in three weeks. The growth was invisible until the disk was 96%
full. When they tried to reclaim the space:

1. A naive prune script deleted 791,846 redundant event rows. The script worked,
   but the file stayed at its original size, because SQLite does not shrink a
   database on `DELETE`. They saw no space gained and assumed the script had
   failed.
2. NTFS `compact` with the default LZNT1 algorithm produced a **1.0:1 ratio**.
   124 GB in, 124 GB out. The pages have no local repetition for a small window to
   find.
3. LZX compression, which would have worked on text, was started. It writes new
   compressed clusters beside the originals. The drive had 36 GB free and LZX needed
   more than that.
4. The machine was rebooted mid-write. The compression process was killed.
5. The file was left with a corrupted Master File Table entry. Explorer reported
   11 GB on disk. NTFS reported 124 GB allocated. SQLite returned
   `ERROR_READ_FAULT (266)` on every read.
6. The coding agent stopped launching. The interface hung on a black screen,
   because opening the database timed out at the driver level.
7. `chkdsk`, `sqlite3 .recover`, PhotoRec, and filesystem tools all failed. The
   data blocks had been overwritten by the partial write.

Every session from three weeks was unrecoverable. Not from hardware failure, not
from malware, not from a mistake in the user's own code. From a database bloat
problem that nothing warned them about.

**Every pattern below exists because of that story or a variation of it.**

---

## What to do first, in every case

1. **Close the coding agent.** Not minimise it. Close it.
2. **Close backup software, cloud sync, and antivirus scans.** Anything holding
   the file open can checkpoint it, and checkpointing a damaged file can make it
   worse.
3. **Do not restart the computer yet.** A restart replays journals and runs file
   system repair, and either can write to the file.
4. **Do not run a repair tool yet.** In the story above, the repairs ran after the
   data was gone.
5. **Copy the file somewhere else.** The database and any `-wal` or `-shm` file
   next to it. A copy of only the main file is not a complete copy when a write
   ahead log is present.

Steps 4 and 5 are in that order on purpose. The copy is the thing that makes
everything after it safe to attempt, and it is the step almost everyone skips.

---

## Pattern: interrupted compression

**What you see:** the file will not read. SQLite reports error 266,
`ERROR_READ_FAULT`. A file browser shows one size, the file system reports a much
larger allocated size.

**Why it happens:** the file was compressed or marked sparse while the database
had it open, and the operation did not finish. The file system kept the original
clusters and the file was rewritten shorter, so the two no longer agree about how
large the file is.

**Is it recoverable:** the overwritten bytes are gone. This tool will not pretend
otherwise.

**What to do:**

1. Stop using the computer, as above.
2. Copy what is left. Even a partial copy is worth having.
3. Check the drive. `chkdsk D: /f` on Windows, `sudo smartctl -a /dev/sdX` on
   Linux, Disk Utility's First Aid on macOS. **If the drive reports errors, replace
   it before writing anything**, because every further read makes it worse.
4. Run `agent-db-doctor recover --database <path>`. It will name this case and
   print the plan.
5. Accept that your own project, your configuration, and any session the agent
   had already sent to a server are the realistic recovery. The database file's
   overwritten blocks are not coming back.

## Pattern: compressed or sparse database

**What you see:** SQLite reports `SQLITE_READONLY` or `SQLITE_CANTOPEN` on a file
whose allocated size is far below its logical size. The agent fails to save.

**Why it happens:** someone compressed the database to save space, on the reasonable
assumption that compression would help.

**Is it recoverable:** yes, usually. Nothing is damaged; SQLite simply cannot write
to a file the file system has compressed.

**Why it happens at all:** SQLite pages are fixed size and contain no repeated byte
patterns, so a compressor finds nothing to compress. NTFS's default LZNT1 algorithm
operates on small windows and produced a 1.0:1 ratio on a 124 GB database. LZX uses
larger windows and would compress text well, but writing new clusters beside the
originals made the file *larger* before the drive ran out of room.

**What to do:** clear the compression attribute, or let this tool rebuild the
database:

```bash
agent-db-doctor compact --database <path>
```

`compact` writes a fresh database through SQLite's own machinery, which produces a
file the file system will not try to compress.

## Pattern: an interrupted write

**What you see:** a `-journal` or `-wal` file next to the database, and sometimes a
warning that the database was not shut down cleanly.

**Why it happens:** the machine lost power or the process was killed mid write.

**Is it recoverable:** almost always. SQLite replays the journal the next time the
database is opened, and the database becomes consistent again.

**What to do:** open the agent once, or run any command that opens the database, and
let SQLite replay the journal. Then run `agent-db-doctor diagnose` to confirm.

## Pattern: structurally damaged

**What you see:** `SQLITE_CORRUPT`. The file reads, but SQLite reports damage
inside it.

**Why it happens:** an interrupted write on a nearly full drive is the usual cause.
The damage is normally in an index or in the free page list rather than in the
conversation data.

**Is it recoverable:** often. Indexes can be rebuilt from the data they describe.

**What to do:**

```bash
agent-db-doctor recover --database <path> --rebuild
```

This copies the file, rebuilds every index in the copy, and tells you where the
copy is. **Your original is never modified.** Move the copy into place yourself
once you have checked it, keeping the original under a different name.

**One flag covers both repairs.** `--rebuild` is the only rebuild flag, and it
chooses between the two methods from the diagnosis it has already made:

| What the tool found | What `--rebuild` does |
|---|---|
| The rows can still be read, an index is damaged | Rebuilds every index on a copy |
| The rows cannot be read | Dumps what SQLite can still read into a fresh database |

The second case leaves out the sessions that are too badly damaged rather than
writing them half way, so the result opens cleanly. You do not choose: the tool
picks the method that fits what it found, which is why the plan it prints tells you
what it is about to do before you agree to it.

There is no `--rebuild-indexes` or `--rebuild-from-dump` flag. An earlier version of
this tool's own printed plan named both, and neither existed, so following the plan
produced "the following argument was not expected".

## Pattern: the file is not a database

**What you see:** `SQLITE_NOTADB`. The first sixteen bytes are not the SQLite
signature.

**Why it happens:** either the path points at a different kind of file, or the
beginning of the file has been overwritten.

**Is it recoverable:** depends entirely on which of those it is.

**What to do:** run `agent-db-doctor diagnose --database <path>` first. If the path
is wrong, that command will find the right one. If the path is right, something
overwrote the start of the file, and the sessions inside it are probably gone.

## Pattern: the drive is nearly full

**What you see:** this one is not a failure. It is the cause behind a surprising
share of the others.

**Why it matters:** SQLite cannot finish a write it has already started. A
database that fills the disk mid write is how structurally damaged databases
happen.

**What to do:** free space before doing anything else with the database, and check
the drive is healthy. Then `diagnose`, then `compact`, then `vacuum`.

## Pattern: another program has it open

**What you see:** a refusal to proceed, naming the operation that would be unsafe.

**Why it matters:** nothing is wrong. SQLite's locking is advisory, so a coding
agent and this tool can both have the file open, and every guarantee the tool makes
would become untrue.

**What to do:** close the coding agent and run the command again.

---

## What this tool will not do

- It will not delete a backup it made. Remove it yourself.
- It will not delete your original file. Every destructive operation works on a
  copy or leaves the original in place.
- It will not change a database when it cannot prove the change is safe. A refusal
  is the correct answer when the tool is unsure.
- It will not claim to recover bytes that are gone. If the data is unrecoverable,
  it says so in the first warning, not in a footnote.
- It will not use filesystem compression, ever, for the reasons above.
