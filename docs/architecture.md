# Architecture

This document explains how the code is put together, and why it looks the way it
does. The short version is that the shape is dictated by two rules that are easier
to state than to obey: **no inheritance anywhere**, and **the code must read like
Java** to someone who has never written C++.

## The two rules, and what they cost

### No inheritance

Nothing in this codebase derives from anything. Every class is `final`. There are
no base classes, no interfaces, no `virtual` methods, no `protected` members.

This is expensive in the ordinary sense. Shared behaviour is achieved by
composition: a class that needs another's behaviour holds an instance of it and
calls methods on it.

```cpp
// The agent contains a locator rather than deriving from one.
class OpenCodeAgent final {
private:
    AgentDatabaseLocator databaseLocator;
};
```

It is more expensive than that, in two places worth naming.

**Exceptions do not share a base class.** A C++ exception is normally a class
hierarchy, so these exceptions are standalone classes that do *not* derive from
`std::exception`. Each one holds an `AgentDbDoctorException` value as a private
field and forwards `what()` and `message()` to it. The consequence is real: no
single `catch` clause covers them, so every command names each type it can be
handed. `CompactCommand::execute` has eight catch clauses where a Java developer would
write one. That verbosity is the price of the rule, and it is paid deliberately.

**`Result<T>` cannot use a `variant`.** The natural implementation holds either a
value or a message in a `variant<T, string>`, but a variant requires its
alternatives to be distinct types, and `Result<string>` asks for a variant of two
strings. The implementation therefore holds an `optional<T>`, a `bool`, and a
`string`, which works for every type.

### The code must look like Java

A reader scrolling any file should not be able to tell whether they are reading
Java or C++ without noticing `#include`, `std::`, and `::`.

That is why:

- Every file has `using namespace std;` after its includes.
- Fields are private with reading and writing methods, never public fields on a
  class that has behaviour. Where a type is a pure value, it uses public fields and
  no methods, which is what a Java record looks like.
- `DiagnosisReport` uses `getDatabaseFilePath()` rather than
  `databaseFilePath()`, because C++ forbids a method and a field sharing one name
  and Java solves the same problem with a prefix.
- Loop indices are named. There is no `i`, `j`, or `k` anywhere.

## One deliberate departure from the namespace rule

`using namespace AgentDbDoctor;` appears in every implementation file, as the
specification requires. It does **not** appear in headers, and the reason is
technical rather than stylistic: a `using` directive naming a namespace is an error
if that namespace has not been declared yet, and a header cannot know whether it is
the first thing a translation unit includes.

Headers therefore contain, after their includes:

```cpp
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}
```

A header that names a type from another subsystem adds one directive for that
subsystem, placed after the include that declares it. Everything else in the
headers is unchanged.

## The layers

```
                    main.cpp
                        |
        +---------------+---------------+
        |               |               |
   Command/         Service/        Presentation/
        |               |               |
        +-------+-------+               |
                |                       |
            Doctor/  <------------------+
                |
        +-------+-------+
        |               |
    Agents/         FileSystem/
                        |
                   Persistence/
                        |
                     sqlite3
```

| Layer | What lives there | What it must never do |
|---|---|---|
| `Common` | Exceptions, `Result`, `Arena`, `Logger` | Know about SQLite |
| `Persistence` | `Database`, `Statement` | Know about agents or reports |
| `FileSystem` | Metadata, disk space, lock detection | Open a database |
| `Agents` | Where each agent keeps its data | Open a file |
| `Doctor` | The diagnostic pass and the safety checks | Write to anything |
| `Recovery` | Failure patterns and recovery plans | Run a repair |
| `Presentation` | Every user facing line | Decide anything |
| `Command` | The five commands | Answer a question itself |
| `Service` | Installation and the watchdog | Know about SQLite |

`Presentation` is the only layer allowed to write to the user's screen. `Doctor`
is the only layer allowed to read a database. Those two rules are what keep the
tool's behaviour auditable: every byte a user sees comes from one file, and every
byte read from their database comes from one place.

## The two files not in the original layout

Two files were added beyond the layout in the specification, both because sharing
once beats repeating:

- `Command/CommandOptions.hpp` holds the settings all five commands read. Written
  five times, twenty fields would have drifted apart between commands.
- `Command/DestructiveOperationGuard.hpp` holds the copy-then-ask-then-verify
  rules used by compact, vacuum, and recover. Written three times, the safety rules
  would have drifted apart, and those are the rules that must not drift.

## The data path of one diagnostic pass

The performance contract is a contract, so the pass is worth tracing.

1. `PlatformMetadata::readFileMetadata` — **one** system call. Returns the logical
   size, the allocated size, and the modification time. The file is never opened.
2. `PlatformMetadata::readFileMetadata` on the `-wal` sibling — **one** call.
3. `StatisticsReader::readHeaderFields` — **one** open, one 100-byte read, one
   close. This is the only read of the database's own bytes in the whole pass.
4. `Database` open, then a handful of constant-time queries: row counts from
   `sqlite_stat1`, one indexed count per event type, two single-value pragmas.
5. `PlatformMetadata::monotonicNanoseconds` — a clock read, not a system call on
   any of the three platforms.

That is **two** metadata calls and **100 bytes**, regardless of whether the file is
ten megabytes or a hundred gigabytes. `tests/benchmark/DiagnoseBenchmark.cpp`
asserts exactly those numbers against a real hundred gigabyte database, so the
claim cannot rot.

The event table is never counted exactly. On a hundred gigabyte database that would
read the whole file, which is the behaviour this tool exists to remove. The report
says the count is unknown and names the command that would make it available.

## Testing

| Suite | What it proves | Files |
|---|---|---|
| Unit | Each class behaves as its own contract says | `tests/unit/` |
| Integration | Whole commands against real SQLite files | `tests/integration/` |
| Benchmark | The performance contract, as assertions | `tests/benchmark/` |

Every integration test builds a real database through the same `Database` wrapper
the tool uses, so nothing is mocked. The synthetic builder writes a schema with
the four tables a real agent uses, and can write several hundred thousand
duplicated event rows in a few seconds.

The most important test in the suite is
`CompactIntegrationTest.cpp`, because the compact command is the only one that deletes
rows. Its first case measures every readable character of the conversation before
and after, and requires the two to be equal.
