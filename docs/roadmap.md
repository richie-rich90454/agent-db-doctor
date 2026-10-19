# Roadmap

What is planned next, in the order it is being built. Everything under "Later"
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
- **The window as a thin invoker.** The window no longer links the diagnostics
  library: it runs the `agent-db-doctor` binary that sits beside it, streams its
  output, and reads the machine readable JSON, so the two front ends cannot
  disagree about a number. Five tabs, one primary action each: Overview, Databases,
  History, Service, and Settings. The console is hidden, a running command shows a
  progress bar with a Cancel button, a destructive run goes through a preview or a
  plan first and then a typed `yes`, and the window's size, theme, and settings
  persist.
- **Automatic maintenance and per-database policies.** With
  `allowAutomaticCompaction` true, the watchdog compacts (when there is anything to
  compact) and then vacuums a database that is above its critical threshold, idle
  for fifteen minutes, and not locked by another process, each step with its own
  safety copy, removed again when its check passes. `databasePolicies` overrides
  the global thresholds and permissions for one database.
- **Desktop notifications.** Best effort, when a critical threshold is crossed and
  after automatic maintenance; the log line is always written as well.
- **The audit log and `audit`.** Every destructive run and every automatic
  maintenance step is appended to a tab separated audit log beside the service
  configuration, kept for one hundred and eighty days and capped in size, and
  `audit` prints it, with machine readable output.
- **The health score.** Every diagnosis carries a score from 0 to 100, shown in the
  window as its headline number; damage, warnings, unused space, and growth each
  reduce it, and a healthy idle database scores 100.
- **`diagnose --prometheus`.** node_exporter textfile metrics for every database,
  with the agent and the path as labels.
- **Packaging templates.** winget manifest templates and a Homebrew tap formula
  template, with generators that fill in the release URL and the SHA-256 and the
  submission steps, in `packaging/`. winget takes a manifest pull request to
  `microsoft/winget-pkgs`, and the Homebrew formula goes to a personal tap
  repository named `homebrew-tap`.
- **A pipeline guard.** A composite GitHub Action that builds the command line
  program and runs `diagnose`, or `reclaim --target`, and fails when a database
  needs attention.
- **Keyless release signing.** Every release package on a tag is signed with
  cosign using the workflow's own OpenID Connect identity, and the `.sig` and
  `.pem` files are uploaded beside the artifacts.
- **The generated verification page.** Continuous integration writes its own run -
  job names, conclusions, times, and the run link - into `docs/verification.md`
  and commits it on `main`.
- **A fuzz target for the configuration parser**, behind
  `-DAGENT_DB_DOCTOR_ENABLE_FUZZING=ON` (Clang only).

## Later

**Package availability.** Until a release has been submitted, installation is from
source; the winget manifest and the Homebrew tap formula become available at the
next tagged release.

**Official homebrew-core.** The prepared formula goes to a personal tap first;
homebrew-core is a later step.

**Cross-agent session search.** One local read-only index over every agent's
sessions, so "find that conversation from March" works across all of them.

**A snapshot timeline.** Copy-on-write snapshots kept per database, with the
interface showing restore points over time.

**Undo in the interface.** Because the safety copy exists, "undo" after a run is a
restore rather than a fiction.
