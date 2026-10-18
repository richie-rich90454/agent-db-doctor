# Roadmap

What is planned next, in the order it is being built. Everything under "planned"
is a decision, not a promise of a date.

## In progress

**The graphical interface as a thin invoker.** The window currently links the core
library. It is being changed to run the command line program and read its machine
readable output, which is what keeps the two front ends from ever disagreeing, plus
the fixes for the interface's own defects: a console window that should not appear,
clipped action buttons, no sorting or filtering in a list of hundreds of databases,
stale numbers after an action, and no way to cancel one.

## Planned

**Space attribution and a forecast.** `explain-space` reads SQLite's own page
accounting and reports which tables, indexes, and sessions hold the bytes, so the
question "why is this database forty gigabytes" gets an answer with numbers.
History recorded by the watchdog turns into a growth line and a date at which the
disk fills at the current rate.

**One command that reclaims space (`reclaim --target 20GB`).** People think in
"give me twenty gigabytes back", not in operations. The command picks the cheapest
safe sequence — compaction, then vacuum, then offered session deletions — shows the
plan, and stops when the target is met.

**Session tools.** List sessions with their sizes, export one or many to Markdown or
JSON, and delete a single oversized session with the same copy-verify-restore
machinery the other destructive commands use. The last one matters for the case
where four gigabytes are one week-long agent run that nobody will read again.

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
