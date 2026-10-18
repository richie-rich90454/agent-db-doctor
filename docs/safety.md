# Safety

This tool deletes rows from databases that hold years of someone's work. Everything
below exists so that it can be trusted with that, and every guarantee is enforced
by code and covered by a test rather than promised in prose.

## The five steps

Every destructive operation does the same five things, in this order. A failure at
any step stops before the next one begins.

1. **Refuses to run** when it cannot prove the operation is safe: when the message
   text might live only in the rows about to be deleted, when the schema is one it
   does not know, when another process has a change in progress, when the file is
   damaged, when the change would be larger than the limit it sets for itself, or
   when there is not enough free space to make a copy.
2. **Copies the file**, including the write ahead log, the shared memory file, and
   the rollback journal when they exist, and prints where the copy is. On APFS, on
   btrfs and XFS, and on ReFS or a Windows Dev Drive the copy is attempted as a file
   system clone first, where it costs no space and almost no time, with a plain byte
   copy as the fallback.
3. **Asks**, in plain language, and treats anything other than an exact `yes` as
   no. With no terminal there is nobody to answer, so the question stops the run
   rather than assuming permission; that is what `--yes` exists for.
4. **Changes in batches** inside transactions, checking every batch. An
   interruption — a power loss, a kill, a full disk — leaves a database that is
   structurally sound and holds every word of every conversation. The test suite
   kills a compaction partway through and checks exactly that.
5. **Verifies afterwards.** The oldest sessions are sampled before the deletion and
   read again after it; any lost readable text fails the run, the safety copy is put
   back over the database, and the restored file is checked again. The check that
   cannot finish counts as a failure, not as a pass.

## The safety copy's whole life

The copy is made before the question is asked, so the question can promise
something that already exists.

- **Success.** The checks pass, the copy is removed, and the bytes it held are
  reported. This is what makes the space a run reclaims real: a copy of a ten
  gigabyte file cancels out ten gigabytes of reclaimed space, which used to be
  exactly what happened.
- **`--keep-backup`, or `--backup-to`.** The copy stays, and the report states the
  net change in free space rather than letting the reclaimed figure stand alone.
- **Any failure, refusal, or failed check.** The copy stays, because it is the way
  back. When the check after a deletion fails, the copy is put back automatically,
  and the restored database is checked again before the tool reports anything.
- **A copy that cannot be removed** leaves a warning with the exact path, and the
  `backups delete` command removes it later.

`backups verify` is the drill that proves a copy can actually be restored: it
copies the copy to a scratch file, opens the scratch file, runs SQLite's integrity
check, counts its sessions, and removes it. The database is never touched.

## The vacuum's swap

The vacuum never modifies the original in place. It writes a new database beside
it, runs SQLite's own integrity check on that new file, and only then swaps it in.
The swap is four steps, and a small journal is written before the first one:

1. Move the original and its side files aside.
2. Move the new file into place.
3. Clear away the displaced file.
4. Remove the journal.

A power loss between any two steps is detected on the next run and resolved: the
original is put back if the new file was never in place, and the leftovers are
cleared if it was. Leaving the side files behind is not an option the tool takes:
a write ahead log beside a rewritten database is one of the ways a database gets
corrupted, so the old log moves with the old file.

## What the tool will not do

- It will not delete rows from a schema it does not know. Only OpenCode's event log
  is compacted, because only OpenCode's event log layout is documented.
- It will not count rows in a huge table to answer a question. When the number is
  not available cheaply the report says it is unknown, and the reason.
- It will not compress a file, or change any setting of the database it was asked
  to look at.
- It will not modify a database during `diagnose` or `agents` in any way. Those
  commands open databases read only and never create a file.
- It will not delete a database file. Nothing in the tool removes a database; the
  only files it removes are its own safety copies and its own intermediate files.

## Limits, stated plainly

- **A copy of a file that changes underneath it is not a copy of one moment.** The
  tool refuses when another process has a change in progress and when the file is
  locked, but on Linux and macOS an *idle* connection that holds no transaction
  cannot be detected as reliably as it can on Windows. Close the coding agent
  before a destructive run; that is the honest instruction, and the refusal
  messages say so when the tool can tell.
- **Verification is a sample, not a proof.** Ten sessions are compared before and
  after. A change that lost content in a session outside the sample while leaving
  the sampled ones intact would not be caught. The sample is the oldest sessions,
  which are the ones a large deletion is most likely to touch.
- **The macOS service's `launchctl` calls are unverified.** They are compiled on
  every build and reviewed, but no continuous integration runner has ever executed
  them on a Mac.
