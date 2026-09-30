# Worked example: a bloated database, start to finish

This is one session, from noticing a problem to stopping it happening again. The
database is a real OpenCode database of about 258 MB with 20 sessions.

---

## 1. Something is wrong

The disk is filling up faster than expected, and the coding agent is slow to start.

```bash
$ agent-db-doctor diagnose
```

```
OpenCode database: ~/.local/share/opencode/opencode.db
Size on disk:          257.7 MB
Space the file uses:   257.7 MB, which is 100.0% of the size the file reports
Last written:          2026-09-30 16:02:29
Write ahead log:       11.4 MB (a copy of the newest changes that has not been merged into the main file yet)
Page size:             4,096 bytes
Pages:                 65,977
Unused pages inside:   0 (0 bytes)
How it stores changes: wal - changes are written to a side file first, which is fast
Returns unused space itself: no, so the compact command is what shrinks the file
Integrity:             every page checked out, the database is sound

  This check took 348 milliseconds and made 2 file system queries.
```

The database is sound. The problem is that it holds no unused space, so there is
nothing to reclaim without removing something. And it never returns space on its
own, which is the sentence that matters.

**Note what the tool did not do.** It did not read the file to find its size, it
did not count the event table, and it did not take 348 milliseconds to look at 258
MB. It read a hundred bytes of header and asked the file system two questions. The
same pass on a 100 GB database takes the same 13 milliseconds.

## 2. Find out what is actually removable

```bash
$ agent-db-doctor prune --database ~/.local/share/opencode/opencode.db --keep 200
```

```
Older copies of changed messages: 1,200
Sessions kept untouched:          200 of 412
```

Every time a message is edited, OpenCode writes a new row describing the new state
and leaves the old row in place. Those 1,200 rows are complete copies of states
the conversation has already moved past. The current text for every message is
stored in the `part` table, not in those rows.

The tool refuses to go further than half the event table without being told to,
because a number that large usually means a miscount rather than a problem.

## 3. Make a copy, and confirm

```
A copy of your database was made first
Nothing has been changed yet. The copy is at:
  /home/ann/.local/share/opencode/opencode.db.backup-20260930-162913
  It is 257.7 MB. It will not be deleted by this tool. Remove it yourself once you are happy.

About to remove 1,200 older copies of changed messages.

Your original file has not been touched yet, and a complete copy of it has been
made. If anything goes wrong, the copy is still there.

Type yes to go ahead, or anything else to stop: yes
```

Note the order. The copy is made **before** the question, so the promise in the
question is about something that already exists. The copy includes the `-wal` file,
because a copy of only the main file would be missing changes that are committed
but not yet merged.

## 4. It is done, and here is the proof

```
Done
Older copies of changed messages removed: 1,200
Sessions checked afterwards: 10
All 10 sampled sessions still hold their original message text.

! The file is still about 257.7 MB on disk. Removing rows marks the space as
  reusable but does not hand it back to the drive. Run the compact command next,
  and it will be.
```

Ten sessions were sampled before the deletion and read again afterwards. If the
number of readable text parts in any of them had gone down, the tool would have put
the copy back and told you so.

**The file is still the same size.** This surprises people every time, and it is
the single most important thing to know: SQLite does not shrink a file when rows are
deleted. The space is reusable by SQLite, but it is still the file's.

## 5. Hand the space back

```bash
$ agent-db-doctor compact --database ~/.local/share/opencode/opencode.db
```

```
This database is 257.7 MB and needs about 273.0 MB of free space while it works,
because it writes a new file before it replaces the old one.
Right now the drive has 412.6 GB free.

Size before: 257.7 MB
Size after:  198.4 MB
Given back to the drive: 59.3 MB (23.0% of the file)
The rewrite took 1.8 seconds.
```

The space requirement is stated before the run starts, because this is the step
that could fill a nearly full drive. `VACUUM INTO` writes a brand new database
containing only the pages still in use, and only swaps it in once the new file has
passed its own integrity check. The original is never modified in place.

## 6. Stop it happening again

```bash
$ agent-db-doctor service install
```

The database grew because every change to a message writes a full new copy of it.
Now the service checks the size once an hour and writes one line when it crosses a
threshold you chose, so the next time it grows you find out on a day when it does
not matter.

To change the threshold:

```bash
$ sudo nano /etc/agent-db-doctor/config.json
```

```json
{ "warningThresholdBytes": 21474836480, "criticalThresholdBytes": 42949672960 }
```

```bash
$ sudo systemctl restart agent-db-doctor
```

## What the whole session cost

| | |
|---|---|
| Time | about 5 seconds of the user's attention |
| Disk read to diagnose a 258 MB database | 100 bytes |
| Data lost | none, and the tool proved it rather than promising it |
| Space recovered | 59.3 MB |
| Time to stop it recurring | one command |

The last row is the one that matters. Everything above is a single afternoon of
somebody's attention. The service is what turns it into nothing.
