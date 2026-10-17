# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Sixty two agents instead of six, OpenCode v1 and v2 schemas, and a graphical
interface. The application version is unchanged: this is all new coverage in
the same v1 tool, verified the same way (full suite green on Windows and
Linux, real databases on the author's machine).

### Added

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

### Added

- A continuous integration workflow builds and tests every change with GCC and
  Clang on Linux, MSVC on Windows, and AppleClang on macOS, starts both the
  command line program and the window on each platform, and packages a runnable
  copy of the tool for every system. Pushing a tag attaches the packages to the
  release.

### Fixed

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
- Prune, compact, and recover fill the refusal explanation into their result
  objects, so the interface's dialogues show why something did not run instead
  of an empty window.
- The interface's activity bar returns to idle when the work finishes.
- Rescanning while a scan is already running is ignored instead of freezing
  the window.
- The diagnostics discovery runs once instead of once per agent.
- The window is paced by the display refresh rate instead of redrawing as
  fast as the processor allows.
- The README points at the one license file the project ships.

## [1.0.0] - 2026-09-30

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
