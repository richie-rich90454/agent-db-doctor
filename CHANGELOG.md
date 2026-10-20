# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

No changes recorded yet.

## [1.0.0] - 2026-10-19

Sixty two agents instead of six, OpenCode v1 and v2 schemas, and a graphical
interface. The application version is unchanged: this is all new coverage in
the same v1 tool, verified the same way (full suite green on Windows and
Linux, and the compaction flow run end to end on a real 10.9 GB database, which
gave 76% of its file back to the drive).

### Added

**The insight commands.** `why` ranks the causes of one database's size and ends
with the one command worth running next. `explain-space` divides the file's bytes
between tables and indexes from SQLite's own page accounting — which needed
`SQLITE_ENABLE_DBSTAT_VTAB` — and, with `--sessions`, ranks the conversations by
the payload they store. `history` reports the growth the watchdog has recorded and
forecasts when the drive would fill at that rate; the record is one line per changed
database per hour, pruned to ninety days.

**The rescue commands.** `export` writes one session out as Markdown, JSON, or
HTML, so a conversation can be kept before anything is deleted or rebuilt.
`sessions` lists the largest conversations and `sessions delete` removes one with
the same copy, typed `yes`, and sampled check every destructive command uses, and
refuses on a database whose second generation tables are in use because their
layout is not documented.

**The reclaim target.** `reclaim --target 20GB` works out the cheapest safe
sequence — compaction first, because it makes the vacuum smaller — prints the plan,
asks once, and then runs the compact and the vacuum commands with their own copies,
checks, and restores. It reports the bytes the drive actually got back and whether
the target was met.

**The live view.** `watch` renders a table of every database sorted by size, with
the growth between refreshes, from file metadata alone: no database is opened.
`--once` and `--machine-readable` render one snapshot for a script.

**An in-place vacuum.** A database set up for incremental auto-vacuum hands its
unused pages back with SQLite's own `PRAGMA incremental_vacuum`: no second file, no
free space needed, and a quick check afterwards that puts the safety copy back if it
fails. `vacuum --enable-auto-vacuum` converts a database during an ordinary rewrite,
so one expensive pass buys cheap reclaims from then on.

**An HTML report.** `diagnose --report report.html` writes one standalone page with
every number, warning, and next step, escaped and ready to attach to an issue.

### Fixed

**The most recent step result is now recorded for rows as well as for finishes.**
`Statement::step` reported the result of the last step that was not a row, so a
caller asking for the code after a single row read a stale value; that is what made
the new in-place vacuum report a perfectly sound database as failing its check.

### Added (the machine readable work)

**Machine readable output.** `--machine-readable` turns every report into one JSON
object per line, flushed as it is written: the diagnostic report with every number
typed, a progress object per percent, a typed result object per command, a plan
object with the recovery steps, a service status object, and a final exit object.
The contract is documented in `docs/json-api.md`, and the graphical interface reads
the same objects.

**A measured read budget.** `--trace-reads` reports how many bytes and read calls a
run made, counted through a wrapper around SQLite's own file layer. The benchmark
that asserts the sixty four kilobyte budget now asserts the measured number rather
than the intention, and the fixture's hundred gigabyte pass reads a few kilobytes.

**The `backups` command** and **the `agents` command** are described above; the
first manages the safety copies, and the second lists every supported agent with
the paths found on this machine and, with `--verify`, every candidate path that was
looked at.

**A schema fingerprint.** The watchdog records a fingerprint of each database's
schema once an hour, and a later diagnose warns when the layout has changed since —
which is the one change that can make every number in a report quietly wrong.

**The `look` and `rescue` aliases** for `diagnose` and `recover`, so the help text,
the errors, and the documentation speak the four job names.

**A new mark.** The logo is redrawn as three database rings threaded by a heartbeat
line ending in a spark: flat colour, four numbers per shape, legible at sixteen
pixels, with the PNG sizes and the multi size Windows icon rendered from the same
geometry by `tools/render-logo.ps1`. The README, the documentation index, and the
new reference pages (`docs/getting-started.md`, `docs/cli-reference.md`,
`docs/json-api.md`, `docs/safety.md`, `docs/agents.md`, `docs/faq.md`,
`docs/roadmap.md`, `SECURITY.md`) were rebuilt around it.

### Changed

**The diagnostic pass no longer scans the event table to name its generation.** The
first generation probe was `LIKE '%.1'`, a pattern with a leading wildcard that no
index can answer, so on a database whose rows are all second generation the pass
read the entire event table. It is now an equality comparison over the known type
names, and the second generation probe is a prefix range rather than a wildcard
pattern.

**A database named directly is identified.** `diagnose --database <path>` used to
report without knowing whose database it was, so a compaction-capable agent was
reported as ordinary. The path is now matched against the agents' candidate paths.

### Fixed

**An integrity check that cannot finish is no longer a pass**, in both the diagnose
report and the pre-flight check that authorises row deletion.

**The count of rows in the `message` and `part` tables is no longer taken on
diagnose.** When SQLite's statistics table was missing, the report ran a full count
of both tables, which is where all the conversation text lives: a routine diagnose
on a large database read the whole of it. The counts are reported as unknown, which
is what the documentation already claimed.

### Added (the safety work)

**The `backups` command.** Lists every safety copy beside a database with its
size and age, proves one restorable by copying it to a scratch file and
checking that copy (the restore drill), puts one back over the database after
moving the current file aside rather than deleting it, and removes old copies
only after naming exactly what it will remove and asking.

**Copy on write safety copies.** The safety copy is attempted as a file system
clone first (APFS, btrfs and XFS, ReFS or a Windows Dev Drive), where it costs
no space and almost no time, with a full byte copy as the fallback everywhere
else.

**A recoverable vacuum swap.** The vacuum writes a small journal before its
three renames and resolves an interrupted swap on the next run, putting the
original back if the new file was never in place.

**A lock check that sees write ahead log databases.** The `-wal` and `-shm`
side files are probed as well as the database itself, because a write ahead
log connection is held through the side files rather than through the main
file. On Linux and macOS this closes the gap that let a live database be
changed on a different platform.

### Changed

**A safety copy is removed again once the run passes its checks.** This is what
makes the vacuum actually free the space it reports: a copy of the file would
otherwise cancel out every byte the run gave back. `--keep-backup` keeps the
copy, `--backup-to` implies keeping it, a failed or refused run always keeps
it, and a kept copy's net effect on free space is stated rather than hidden.
A failure of the post-change check now puts the copy back for real, and the
database is checked again afterwards.

**Refusals exit with code one.** A compaction or vacuum that stopped because a
check refused it is no longer reported to a shell as success.

**Every option is registered on the commands that read it.** `--check` exists
on `diagnose`, `--rebuild` on `recover`, `-k`, `--max-fraction` and
`--batch-size` on `compact`, and the service thresholds on `service`, instead
of every command accepting every flag and ignoring most of them. `--verbose`
now prints the discovery results and the settings a run actually uses.

### Fixed

**The compaction rollback claim is now true.** When the check after a deletion
failed, the tool printed that the safety copy was being put back, and no code
put it back. The restore is implemented, its result is verified, and the
outcome says what actually happened.

**Every removal step is checked.** The keep list, every batch delete, the
rewrite result, the removal of a stale intermediate file, and the copying of
the side files all check their results; a failure restores the database and
reports honestly rather than leaving a change the report cannot account for.

**The half-the-table guard always runs.** When the statistics table was
missing, which is the normal state of a database that has never been analysed,
the guard was skipped entirely; the exact count is taken instead.

**The service can no longer be turned into a processor spin**, refuses
configurations that nest without end, keeps running when its configuration
file cannot be read, and escapes the values it writes into the launchd
property list. The watched path list is rediscovered once an hour instead of
on every tick, and the discovery calls are counted in the tick's total.

### Added (earlier in this release)

**Fifty six more agents.** Cline, Roo Code, Reasonix, VS Code, Gemini CLI,
Windsurf, Zed, GitHub Copilot, Warp, Kilo Code, Augment, Trae, PearAI, Amp,
CommandCode, Pi, Void, Tabnine, Cody, Codeium, Supermaven, Bito, CodeGPT,
Blackbox AI, AskCodi, Phind, IntelliCode, Tabby, Pieces, Amazon Q, Junie,
Qoder, Lingma, Comate, Droid, GitLab Duo, WorkBuddy, Kimi, Kiro, Qwen Code,
CodeBuddy, MarsCode, Qodo, Sourcery, Snyk, SonarQube, AnythingLLM, GPT4All,
Jan, LM Studio, Open WebUI, AIChat, gptme, LLM, ShellGPT, and Raycast. All
are diagnose-only; only OpenCode is pruned. Paths confirmed on a real machine
where installed, vendor references where checked (Kimi, Kiro), published
defaults otherwise, and the docs say which is which.

**OpenCode v2 schema support.** A detector names the generation from the rows
(`Version1`, `Version1And2`, `Version2Only`, `Unknown`); diagnose prints it;
prune refuses unknown layouts instead of reporting nothing to do. Second
generation tables are never touched.

**Graphical interface.** `agent-db-doctor-gui` (SDL3 + Dear ImGui, always
built, off with `-DAGENT_DB_DOCTOR_BUILD_GUI=OFF`): dashboard over all
agents, sortable session browser, prune/compact previews with typed-`yes`
confirmations, recovery plans rendered verbatim, service view, and a live
activity bar fed by the same progress numbers as the console bar.

**Logo and icons.** A `threaded pulse` master SVG, a simplified favicon, PNG
renders at every size, a multi-size Windows icon stamped into both binaries
with version information.

**Continuous integration.** A workflow builds and tests every change with GCC
and Clang on Linux, MSVC on Windows, and AppleClang on macOS, starts both the
command line program and the window on each platform, and packages a runnable
copy of the tool for every system. Creating a release for a tag builds the same
three packages, attaches them to the release, and signs them; the release
workflow can also be run by hand, which builds the packages and leaves them as
run artifacts without touching a release.

**Vacuuming for every SQLite based database.** The vacuum command needs no
knowledge of any schema, only of the SQLite file format, so it is now offered
for every SQLite based agent (OpenCode, Cursor, VS Code, Cline, Windsurf, Zed,
and Void) in the command line and in the interface, not only for OpenCode. The
interface says plainly that compaction is only offered where the event log
layout is documented.

### Changed

**The `prune` command is now `compact`.** Event log compaction is the precise
name for the operation, the same term Kafka and other systems use: keep the
newest copy of every message snapshot and remove the older copies, without
changing the logical content of the log. The command, its classes
(`EventLogCompactor`, `EventLogCompactionReport`, `EventLogCompactionPlan`),
its messages, and the documentation all use that name now.

**The old `compact` command is now `vacuum`.** Vacuuming rewrites the file at
its smallest useful size, which is what hands space back to the drive; it is a
separate operation from compaction and now has its own verb. The service
configuration key `allowAutomaticPruning` is `allowAutomaticCompaction`.

### Fixed

- **Compaction did nothing on real OpenCode databases.** The deletion queries
  matched an event column that the real schema does not have, so every run
  reported "nothing needed doing" while hundreds of thousands of redundant
  snapshots sat in the file. The queries now use the schema's own
  `aggregate_id`, which carries the session each event belongs to, and the
  synthetic test database has been migrated to the real table shape so the
  tests guard the real thing.
- **One compaction run now removes everything it promised.** The deletion loop
  advanced a single row identifier cursor across all three event types, so on
  real data it skipped most of the rows; measured on the real database, one run
  removed 88,837 of the 415,856 rows it had counted. Each event type now keeps
  its own forward cursor, and a run removes exactly the count the preview
  showed.
- **`--dry-run` and the interface's preview buttons no longer change
  anything.** Both commands ignored the flag, so pressing "preview" in the
  interface performed the real operation. A preview now reads, counts, and
  stops before any copy, question, or deletion; the tests compare the row count
  before and after a preview.
- The OpenCode location now follows the vendor's own resolution (`OPENCODE_DB`,
  channel file names, the XDG-style path even on Windows) and prefers a file
  with a size over zero byte placeholders.
- `diagnose` reports duplicate counts again (the redundant types were never
  wired into the report).
- Roo Code no longer claims the Cline extension's storage folder, which made
  Cline's data appear in reports under Roo Code's name.
- Continue is discovered again, and the large files inside its folder are
  listed as the adapter documentation has always described.
- The graphical interface no longer offers the service body (`run`), which
  never returns; the whole program used to wedge on that one button.
- A failure inside the action worker can no longer end the process: the worker
  catches everything, and `recover --rebuild` now catches its own failure
  types like the other commands.
- Compact, vacuum, and recover fill the refusal explanation into their result
  objects, so the interface's dialogues show why something did not run instead
  of an empty window.
- The interface's activity bar returns to idle when the work finishes.
- Rescanning while a scan is already running is ignored instead of freezing
  the window.
- The diagnostics discovery runs once instead of once per agent.
- The window is paced by the display refresh rate instead of redrawing as
  fast as the processor allows.
- The macOS build compiles: the host name buffer used a constant that only
  Linux defines.
- The README points at the one license file the project ships.

### Added (the window as an invoker)

- **The window no longer links the diagnostics library.** It runs the
  `agent-db-doctor` binary that sits beside it, streams its output, and reads
  the machine readable JSON, so the two front ends cannot disagree about a number.
- **Five tabs, one primary action each.** Overview: total size, total reclaimable,
  one "Reclaim the biggest" button, and the five largest databases. Databases: a
  filter box, a sortable table of agent, path, size, reclaimable, and health, and a
  drawer for the selected database with its metrics, warnings, suggested next
  steps, and its actions: Reclaim, Preview compaction, Preview vacuum, Recovery
  plan, Backups, Sessions, Export session, and Diagnose, with an optional full
  integrity check. History: the recorded growth and the forecast. Service: the
  real status (installed, running, the state sentence, the locations, the most
  recent log line) and the install, start, stop, and uninstall buttons with the
  threshold and interval fields; installing needs administrator rights. Settings:
  the default reclaim target, whether the safety copy is kept, a backup
  destination, and the resolved path of the command line program.
- **The console window is hidden.** The window is built as a windows-subsystem
  application, and every child command runs with no console window. A running
  command shows a progress bar with a Cancel button; its result appears in an
  opaque, scrollable dialog; a destructive run always goes through a preview or a
  plan first, then a typed `yes`; a successful change rescans by itself. The
  window's size, theme, and settings persist.

### Fixed (the window layout)

- **Nothing in the window is clipped any more.** The table columns were fixed
  pixel widths, so a scaled display cut "Reclaimable" down to "Reclaim..." and a
  Reclaim button lost its last letter at the table's edge; every fixed column is
  now sized from the text it holds. The totals card cut its own "Reclaim the
  biggest" button in half; the card now sizes itself to what it holds and the
  button takes its natural width.
- **The newest log line is fully visible.** The log pane's height was a constant
  while the rows around it grew with the font, so the pane's last line was cut by
  the window's bottom edge; the room the bottom bar keeps for itself is now
  computed from the current font, and the pane is sized from it.
- **A tab that is asked for opens at its top.** The tab area kept the scroll
  position of the tab that was showing, so the next tab opened part way down with
  its first row cut off.
- **The interface is opaque, and the window draws no border of its own.** Dear
  ImGui clears each frame with the window background colour, which carries a small
  alpha by default; where the display composites that, whatever sat behind the
  window tinted the interface. The background is now fully opaque, and the border
  line the full-viewport window drew inside the operating system's own frame is
  gone.
- **The log pane starts closed.** The tabs are what a person came for, the
  activity bar still shows how the last command ended, and one press opens the
  pane. The choice is remembered.

### Fixed (the continuous integration)

- **The window's JSON reader names `std::move`.** Clang rejects an unqualified call
  to a standard cast under this project's `using namespace std`, so the Clang and
  AppleClang jobs refused to compile `gui/Json.cpp` while the GCC and MSVC jobs
  accepted it. Every call is qualified now, and the warning is named in the build
  configuration rather than left to a compiler default, so the requirement is
  visible where the flags are.
- **The Windows job reads the window's exit code.** A windows subsystem program
  returns to PowerShell immediately, so the smoke test's result was never looked at
  and a crash would have passed; it is started, waited for with a time limit, and
  its exit code decides the step.
- **The verification job may read the run it reports on.** Listing a run's jobs and
  reading their logs needs the `actions: read` permission, which the narrowed token
  did not grant, so the page could not have been generated.
- **`xvfb-run` has what it needs on the Linux jobs.** `xauth` is installed with
  `xvfb`, because the virtual display cannot start without it.
- **The jobs name stable runner images.** `windows-2025` replaces the preview label
  `windows-2025-vs2026`; both carry Visual Studio 2026, and the stable label is the
  one that will keep meaning what it means.
- **The idle memory check reads the unit each platform reports.** `getrusage`
  reports the peak resident set in bytes on macOS and in kilobytes on Linux, and the
  benchmark multiplied both by 1024, so a macOS process of 4.1 MB was read as 4.4 GB
  and the macOS job failed on it. The conversion now follows the platform.
- **The shared library's loader name is only linked when it differs from the file's
  own name.** On macOS SDL3 sets a soname but no version, so the library file is
  already named after the soname; the post-build link from that name to itself
  replaced the library with a link to itself instead of failing, which would have
  left every macOS package with a library the loader cannot open. Linux, where the
  two names differ, keeps the link and the package is unchanged.
- **The verification page names the compilers again.** The page is generated while
  its own run is still open, and GitHub serves a job's log only after the run that
  owns it has finished, so reading the banners back from the logs put a dash in
  every row. Each platform job now writes its banner to a file and uploads it as an
  artifact, and the page reads those.
- **The compact benchmark no longer fails on a busy runner.** Its ceiling was 60
  microseconds per row, and a Windows runner in continuous integration measured 62.6
  and then 77.9 for code the reference machine measures at 8: every repetition was
  that slow, so no number of repetitions would have helped. The assertion that
  compares the two runs with each other is the one that decides whether a change is
  a regression, and it passed in both runs; the ceiling is now a backstop for a
  design broken by an order of magnitude, which is what its comment always said it
  was for.

### Added (the automation work)

- **Automatic maintenance.** With `allowAutomaticCompaction` true, globally or for
  one database, the watchdog compacts (when there is anything to compact) and then
  vacuums a database that is above its critical threshold, idle for fifteen
  minutes, and not locked by another process, each step with its own safety copy,
  removed again when its check passes. The log line says what it did, and every
  step is written to the audit log.
- **Per-database policies.** The configuration gains `databasePolicies`, an array
  of `{ "path", "criticalThresholdBytes", "allowAutomaticCompaction",
  "notifyOnThreshold" }` entries that override the global thresholds and
  permissions for one database.
- **Desktop notifications.** A notification is attempted when a critical threshold
  is crossed and after automatic maintenance: a balloon through PowerShell on
  Windows, `notify-send` on Linux, `osascript` on macOS. It is best effort - a
  service running in another session may not be able to show one - which is why
  the log line is always written as well.
- **The audit log and the `audit` command.** Every destructive run, and every step
  automatic maintenance takes, is appended to a tab separated audit log beside the
  service configuration: the time, the action, the path, the sizes before and
  after, the safety copy's path, the outcome, and the exit code. It is kept for one
  hundred and eighty days and capped in size, and it is written when its location
  is writable and always by the service. `audit` prints it: `tail` by default,
  `list` for the whole record, `--limit N` for a shorter view, and
  `--machine-readable` for one JSON object per entry.
- **The health score.** Every diagnosis carries a score from 0 to 100, shown as
  `Health: 78 of 100` and called `healthScore` in the machine readable output (-1
  when it cannot be computed). Damage, warnings, unused space, and growth each
  reduce it, and a healthy idle database scores 100; the window shows it as the
  headline number.
- **`diagnose --prometheus`.** Writes node_exporter textfile metrics -
  `agent_db_doctor_size_bytes`, `agent_db_doctor_reclaimable_bytes`,
  `agent_db_doctor_healthy`, and `agent_db_doctor_health_score` - with the agent
  and the path as labels, so a team can chart every database.

### Added (the packaging and integration work)

- **Package manager templates.** `packaging/` holds winget manifest templates and
  a Homebrew tap formula template, with generators that fill in the release URL
  and the SHA-256, and the submission steps. No registration or fee is involved:
  winget takes a manifest pull request to `microsoft/winget-pkgs`, and the
  Homebrew formula goes to a personal tap repository named `homebrew-tap`.
- **1.0.0 is published to the package managers.** `brew tap richie-rich90454/tap`
  then `brew install agent-db-doctor` installs the release on macOS on Apple silicon
  and Linux on x86_64. The winget manifests are in review at
  [microsoft/winget-pkgs#456765](https://github.com/microsoft/winget-pkgs/pull/456765),
  and once they merge, `winget install richie-rich90454.agent-db-doctor` installs the
  same release. The winget installer manifest sets `ArchiveBinariesDependOnPath`,
  because the window loads SDL3 and the artwork from beside its own binary, and it
  uses manifest schema 1.9.0, the first that carries that field.
- **A pipeline guard.** A composite GitHub Action builds the command line program
  and runs `diagnose`, or `reclaim --target`, failing the step when a database
  needs attention.
- **A generated verification page.** Continuous integration writes the run's own
  job names, conclusions, times, and run link into `docs/verification.md` and
  commits it on `main`.
- **A release workflow.** `.github/workflows/release.yml` builds the three platform
  packages when a release is published, attaches them to that release, and signs
  them; it refuses a tag that does not name the version in `CMakeLists.txt`. Started
  by hand instead, it builds the same packages and leaves them as run artifacts, so
  the packages can be produced before the release is written, without touching one.
- **Completing a release by hand.** The same workflow accepts the tag of an existing
  release, builds the three packages from that tag, attaches them, and signs them:
  that is how a release whose run failed gets its missing package, without moving a
  tag or re-creating the release. Assets of the same name are replaced and missing
  ones are added, and signing runs even when one platform job failed, so the
  packages that did build still carry their signatures.
- **Keyless release signing.** On a release, every package the release workflow
  built is signed with cosign using the workflow's own OpenID Connect identity, and
  the `.sig` and `.pem` files are uploaded beside the artifacts.
- **A fuzz target for the configuration parser**, behind
  `-DAGENT_DB_DOCTOR_ENABLE_FUZZING=ON` (Clang only).

### Initial implementation (September 30, 2026)

The first release. Everything below is implemented and verified against real agent
databases. The full test suite is run and passes on Windows and on Linux; the macOS
sources are compiled on every Linux build and are not run, because the machine this
was written on is not a Mac. `README.md` says exactly what was verified where.

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

### Fixed before release, by building on a second platform

The Linux and macOS implementations had never been compiled by anything, because the
project was developed on one machine. Building them on Linux found nine real
mistakes, all of them of the kind that only a compiler notices:

| Where | What was wrong |
|---|---|
| `PlatformMetadataLinux.cpp` | `AT_FDCWD` used without including `<fcntl.h>`, so the file did not compile |
| `PlatformMetadataLinux.cpp` | `readPasswdHomeDirectory` defined below its own call site |
| `FileLockDetector.cpp` | A namespace written twice, resolving to `AgentDbDoctor::Common::AgentDbDoctor::Common` |
| `ServiceManagerLinux.cpp` | `<fstream>` missing, and `ServiceOperationException` never brought into scope |
| `ServiceManagerMacOS.cpp` | Neither the exceptions header nor the `Common` namespace, on the one platform the file exists for |
| `PlatformMetadataMacOS.cpp` | `kill` and `HOST_NAME_MAX` used without `<signal.h>` or `<limits.h>` |
| `PlatformMetadataMacOS.cpp` | The same passwd helper defined below its own call site |
| `PlatformMetadataMacOS.cpp` | Two unused Apple-only includes, `sys/sysctl.h` and `sys/param.h` |
| `RecoverCommand.cpp` | A metadata pattern could overwrite the integrity verdict, so a file that is not a database could be reported as recoverable |

The last one was the most serious. A database whose first bytes are not the SQLite
signature is unrecoverable by definition, but if a lock or a full drive was also
noticed, the pattern's answer replaced that verdict. It is now the case that
unrecoverable is sticky: a pattern may add what it found, and may not soften a
verdict the integrity check already gave.

Two of these would have failed on macOS itself. The rest would have failed the
moment anyone built the project on a second machine.

To stop the class of mistake rather than just the instances, every Linux build now
also compiles the macOS sources into an object library that is never linked.

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

[1.0.0]: https://github.com/richie-rich90454/agent-db-doctor/releases/tag/v1.0.0