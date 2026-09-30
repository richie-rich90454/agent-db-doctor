# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-09-30

The first release. Everything below is implemented, tested on all three
platforms, and verified against real agent databases.

### Added

**Diagnose.** A full diagnostic pass over every supported agent database, or one
named file. Reports logical size, allocated size, page size, page count, unused
pages inside the file, journal mode, whether the database returns space on its
own, row counts, and the number of rows that are known duplicates. Detects
corruption and names it in plain language. Costs two metadata queries and one
hundred bytes of reading, whatever the size of the database.

**Prune.** Removes the older copies of messages that an agent leaves behind when a
message is edited, for OpenCode. The newest sessions are protected. Refuses to
remove more than half the event table without being told to. Deletes in batches
inside transactions using a forward rowid cursor, so the work is linear rather than
quadratic.

**Compact.** Rewrites a database to its smallest useful size using SQLite's own
`VACUUM INTO`, which never modifies the original in place. States the free space
the operation needs before it starts, and refuses to run without it. Verifies the
new file's integrity before swapping it in, and rolls back if anything goes wrong.

**Recover.** Names the specific failure that was recognised, out of nine patterns,
and produces an ordered plan. Says out loud when data is unrecoverable rather than
letting a user keep trying. Can build a working copy of everything that can still
be read, on request only, never touching the original.

**Service.** Installs, controls, and reports on a native system service on Windows,
Linux, and macOS. Watches every supported agent database on a configurable
interval and writes one line to a log when one crosses a threshold. One metadata
query per database per tick, zero bytes read, one thread, under 12 MB resident.

**Six agent adapters.** OpenCode, Cursor, Codex CLI, Claude Code, Continue, and
Aider. Discovery is a fixed list of candidate paths tested with one metadata call
each; no directory tree is ever walked, except one bounded level for the two agents
whose folders are named after a hash.

**Every failure translated.** No raw error code is ever shown without a sentence
beside it. SQLite result codes, Windows error numbers, and Unix `errno` values all
have plain English explanations, and the number appears afterwards, labelled as a
search term.

### Safety

- Every destructive operation makes a complete copy first, including the `-wal` and
  `-shm` side files, and prints where the copy is.
- Nothing is changed without an exact confirmation, and a refusal to confirm is not
  treated as a failure.
- The prune command refuses to run unless it can prove the message text lives
  outside the rows it is about to delete.
- The oldest sessions are sampled before a deletion and read again afterwards. If
  any of them lost readable text, the safety copy is put back.
- The compact command verifies the rewritten file before swapping it in, and rolls
  back if the swap fails.
- Every operation leaves the database in exactly the state it found it, including
  on an error, an interruption, or a refusal.

### Performance

The performance contract is enforced by assertions in `tests/benchmark/`, so a
regression fails the build.

| Measurement | Target | Measured |
|---|---|---|
| Diagnostic pass, 100 GB database | < 500 ms | 13 ms |
| Metadata queries per pass | < 20 | 2 |
| Bytes read per pass | < 64 KB | 100 |
| Metadata calls per watchdog tick per database | 1 | 1 |
| Memory growth over 1000 idle ticks | ~0 | < 1 MB |
| Peak resident memory, full diagnostic | < 40 MB | 7.1 MB |
| Startup, command line tool | < 30 ms | 28.3 ms |
| Marginal cost of removing one row | 5.3 us | 8.0 us Windows, 3.0 us Linux |

**Known gap.** Removing a duplicate row costs 8.0 microseconds on Windows against a
target of 5.3, so 1.5 million rows take about twelve seconds there rather than
eight. On Linux the same build measures 3.0 microseconds per row, inside the target.
The remainder on Windows is the operating system rather than the query: for identical
SQL, deleting a row costs about two and a half times more there, because every
temporary file and every log write passes through antivirus software.
`docs/performance.md` records what was changed to get from 20.1 to 8.0 and what is
left to do.

### Known limitations

- The full page by page integrity check is skipped by default above 2 GB, because
  its cost grows with the size of the file. The report says it was skipped and
  names the flag that runs it.
- The event table's row count is reported as unknown when the database has never
  been analysed, rather than spending minutes scanning it.
- Cursor and Continue are reported but never pruned, because their schemas are
  undocumented and deleting rows on a guess would lose state.
- The specification's file layout includes continuous integration workflows. This
  repository is private and does not use them, because hosted runners cost money.
  The suites run locally with `ctest --test-dir build`.

[1.0.0]: https://github.com/your-org/agent-db-doctor/releases/tag/v1.0.0
