# The machine readable output contract

`--machine-readable` turns every report into one JSON object per line, written to
standard output and flushed as it is written, so a script or another program can
follow a long operation rather than waiting for the process to end. The graphical
interface reads the same objects, which is why the two front ends cannot disagree
about a number.

The output is a stream of objects, not one document: read it line by line and parse
each line on its own.

## The stream

| `event` | When | Fields |
|---|---|---|
| `command` | Once, at the start | `name`, `purpose` |
| `database` | Once per database, from `diagnose` | see below |
| `agent` | Once per agent, from `agents` | `name`, `storage`, `supportsCompaction`, `foundPaths`, and `candidatePaths` with `--verify` |
| `heading`, `line`, `detail` | Whenever a command has something to say | `text` |
| `warning` | Whenever a command warns | `text` |
| `question` | When something needs an answer; the answer is still read from standard input | `text` |
| `backup` | When a safety copy is made | `path`, `sizeBytes` |
| `progress` | During a long operation, one object per percent and a final one | `task`, `done`, `total`, `finished` |
| `plan` | From `recover`, once the plan is built | `agent`, `path`, `steps` |
| `service` | From `service status` | `installed`, `running`, `state`, `locations`, `lastLogLine` |
| `result` | Once per run that reaches an outcome, with the typed numbers | `command`, `fields` |
| `error` | When something is refused or fails | `message`, `code` |
| `next` | The suggested next steps | `steps` |
| `reads` | From `diagnose --trace-reads` | `bytes`, `calls` |
| `exit` | Always last | `exitCode` |

Nothing else is written to standard output in this mode, so a line that does not
parse is a bug worth reporting.

## The `database` object

The fields are the whole diagnostic pass. A field that does not apply to a database
still appears, with a zero or an empty value, so a reader never has to test for
missing keys.

```json
{
  "event": "database",
  "path": "/home/ann/.local/share/opencode/opencode.db",
  "agent": "OpenCode",
  "exists": true,
  "isDirectory": false,
  "healthy": true,
  "sizeBytes": 3045969920,
  "allocatedBytes": 3045969920,
  "hasWriteAheadLog": true,
  "writeAheadLogBytes": 70810472,
  "modifiedMilliseconds": 1792315695017,
  "pageSizeBytes": 4096,
  "pageCount": 743645,
  "freePageCount": 362934,
  "reclaimableBytes": 1486577664,
  "journal": "wal",
  "autoVacuum": false,
  "compactionSupported": true,
  "schema": "second generation rows only; the older duplicate snapshots are absent",
  "redundantEventRows": 0,
  "redundantEventTypes": ["message.updated.1", "message.part.updated.1", "session.updated.1"],
  "integrity": "not_checked",
  "integrityMessage": "",
  "databaseOpenSucceeded": true,
  "openFailureCode": 0,
  "durationNanoseconds": 2182800,
  "metadataCallCount": 2,
  "rows": {"session": {"count": 536, "approximate": false}},
  "warnings": ["..."],
  "nextSteps": ["..."]
}
```

`integrity` is one of `not_checked`, `folder`, `intact`, `not_a_database`,
`damaged`, `unreadable`, or `problem_reported`. `rows` carries only the tables
whose count could be answered cheaply; a missing table means the count is unknown,
and `approximate` says whether the number came from SQLite's statistics table.

## The `result` object

Each command that reaches an outcome emits one result object with typed numbers, so
a script never parses a sentence:

```json
{"event":"result","command":"vacuum","fields":{"previewOnly":false,"refused":false,"compacted":true,"sizeBeforeBytes":10900000000,"sizeAfterBytes":2580000000,"bytesReclaimed":8320000000,"keptSafetyCopyBytes":0,"netBytesFreed":8320000000}}
{"event":"result","command":"compact","fields":{"previewOnly":false,"refused":false,"rolledBack":false,"rowsRemoved":415856,"protectedSessions":239,"verifiedSessions":10,"safetyCopyRemoved":true,"safetyCopyBytes":10900000000,"reclaimableBytesNow":6200000000}}
```

Fields are numbers, booleans, or strings, and the type is part of the contract:
`rowsRemoved` is never the string `"415856"`.

## Recipes

The total reclaimable space across every database:

```bash
agent-db-doctor diagnose --machine-readable --quiet \
  | jq -s 'map(select(.event=="database") | .reclaimableBytes) | add'
```

Watch a long compaction:

```bash
agent-db-doctor compact -d "$DB" --yes --machine-readable \
  | jq -c 'select(.event=="progress") | {task, done, total}'
```

A monitoring script that fails when anything needs attention:

```bash
agent-db-doctor look --machine-readable --quiet > /var/log/agent-db-doctor.jsonl
status=$?
if [ "$status" -ne 0 ]; then echo "a database needs attention"; fi
```

## Stability

The event names and the field names above are the contract. New events and new
fields may be added; an existing field will not be renamed or removed without a
note in the changelog, and its type will not change.
