# Contributing to agent-db-doctor

Thank you for wanting to help. This file is short on purpose, and the reason is
that this project has one unusual constraint that shapes every change: **the tool
runs on a stranger's machine, usually at a bad moment, usually when they are
frightened.** Everything below follows from that.

## The one rule that is not negotiable

Nothing you add may ever destroy a user's session history without their explicit
confirmation, and nothing may be left in a state the user did not ask for.

If your change writes to a database, it must:

1. Pass every check in `SafetyChecks` first.
2. Make a complete copy first, including the `-wal` and `-shm` side files.
3. Print where the copy is.
4. Ask before proceeding, unless `--yes` was passed.
5. Verify afterwards that the content survived, and put the copy back if it did not.

If any of those five is missing, the change is not ready.

## Style

The code must read like Java to someone who has never written C++. That is a
deliberate constraint, not a preference.

- Every class is `final`. There is no inheritance anywhere in this codebase, and
  shared behaviour is achieved by composition. Read
  `docs/architecture.md` for how and why.
- Method and field names are `lowerCamelCase`, class names are `PascalCase`,
  constants are `UPPER_SNAKE_CASE`.
- Loop indices are never `i`, `j`, or `k`. Use `currentIndex`, `outerIndex`.
- Every construct a reader with introductory programming experience would not
  recognise carries a comment saying what it is and why it is there.
- Four spaces, never tabs. No blank lines inside a class body.
- `using namespace std;` appears in every file. So does
  `using namespace AgentDbDoctor;`.

Run the formatter before you commit:

```
cmake --build build --target format
```

## Performance

The performance contract in `docs/performance.md` is a contract, not a goal. If
your change makes the diagnostic pass cost an extra system call, or reads a byte it
did not need to, the benchmark suite fails and the change is not ready.

The three questions to ask of any new code:

1. How many system calls does this cost?
2. How many bytes does this read?
3. What is the complexity, and is it O(1) or O(log N) wherever that was achievable?

## Tests

Every class with logic needs a unit test. Every command needs an integration test
that runs it against a real SQLite file. If your change could lose data, there must
be a test that proves it does not.

```
cmake --build build
ctest --test-dir build --output-on-failure
```

## Commit messages

Conventional Commits, imperative mood, in American English.

```
feat: add the compact command
fix: refuse to prune when the content would not survive
perf: replace the offset cursor with a forward rowid cursor
docs: explain why the integrity check is skipped above 2 GB
```

One logical change per commit. A commit that touches forty files is forty changes
that were not reviewed separately.

## What will be declined

- A change that adds a dependency. The five permitted ones are listed in
  `CMakeLists.txt`, and adding a sixth needs a very good reason.
- A change that makes a command destructive without a safety copy.
- A change that adds a raw error code to the user's screen without a sentence
  beside it.
- A change that walks a directory tree. File discovery is a fixed list of
  candidate paths, tested with one metadata call each.

## Reporting a bug

Please include the output of `agent-db-doctor diagnose` and the output of
`agent-db-doctor --version`. The first names the file and its state; the second
names the build. Together they are almost always enough to reproduce.

If your database is damaged, do not attach it to an issue. Run
`agent-db-doctor recover` and share the *plan* it prints, which contains no
private content.
