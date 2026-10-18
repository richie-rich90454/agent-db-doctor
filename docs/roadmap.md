# Roadmap

What is planned next, in the order it is being built. Everything under "planned"
is a decision, not a promise of a date.

## Built in this cycle

- **`explain-space` and `why`.** Where a database's bytes actually are, from
  SQLite's own page accounting, and the ranked causes with the one command worth
  running next.
- **`history` and the growth record.** The watchdog records one size per database
  per hour, and the report turns it into a rate and a date at which the drive would
  fill.
- **`reclaim --target`.** A target in gigabytes rather than a choice of operations.
- **`sessions`, `export`, and `sessions delete`.** Rank the conversations by
  payload, write one out as Markdown, JSON, or HTML, and remove one with the same
  copy, typed yes, and sampled check as every other destructive command.
- **`watch`.** A live table of every database, rendered from file metadata alone.
- **The incremental vacuum.** A database set up for incremental auto-vacuum hands
  its unused pages back in place, with no second file and no scratch space;
  `vacuum --enable-auto-vacuum` converts a database once so later reclaims are
  cheap.
- **`diagnose --report report.html`.** One standalone page with every number and
  warning, ready to attach to an issue.

## In progress

**The graphical interface as a thin invoker.** The window currently links the core
library. It is being changed to run the command line program and read its machine
readable output, which is what keeps the two front ends from ever disagreeing, plus
the fixes for the interface's own defects: a console window that should not appear,
clipped action buttons, no sorting or filtering in a list of hundreds of databases,
stale numbers after an action, and no way to cancel one.

## Planned

**Watchdog notifications and automatic maintenance.** A desktop notification when a
threshold is crossed or maintenance runs; declarative per-database policies; and a
maintenance window so nothing runs while the agent is active.

**An audit log.** Every destructive action recorded with its path, sizes, safety
copy, verification result, and time.

**Package managers.** winget and a Homebrew tap, so installation is one line. The
release artifacts they will use are already built by continuous integration.

**A generated verification page.** Continuous integration writes the matrix — every
platform, compiler, and result — into a page in the repository, so a reader can see
what was actually run rather than what is claimed.

## Later

**Cross-agent session search.** One local read-only index over every agent's
sessions, so "find that conversation from March" works across all of them.

**A snapshot timeline.** Copy-on-write snapshots kept per database, with the
interface showing restore points over time.

**Undo in the interface.** Because the safety copy exists, "undo" after a run is a
restore rather than a fiction.
