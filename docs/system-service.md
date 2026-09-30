# The system service

The service is what turns this from a tool you remember to run into one that
notices on its own. It exists because the real failure this tool was written for
was invisible: nobody knew the database had reached 124 GB until the disk was 96%
full.

## What it costs

| Resource | Limit | Why |
|---|---|---|
| Resident memory, idle | under 12 MB | It runs for months |
| Threads | 1 | The design is lock free and single threaded by choice |
| Metadata calls per tick | 1 per database | The specification's budget |
| Bytes read per tick | 0 | A size comes from metadata, never from the file |
| CPU, idle | under 0.01% of one processor | It wakes once an hour |
| Log lines | at most 1 per hour | It has nothing to say until something changes |

The watchdog loop is deliberately the dullest code in the project. Each tick
performs one metadata call per database, compares the result against the previous
tick's number, and decides in memory whether there is anything worth saying. It
does not open a file, does not read a byte, and does not allocate once the sample
vectors have grown to their steady size.

`WatchdogBenchmark` asserts all of this: one call per database, no log output
while a database stays below its threshold, and under a megabyte of memory growth
across a thousand ticks.

## Installing

```bash
agent-db-doctor service install
```

You will be asked to confirm, and the tool will first print what it is about to do
and where the files will go. **Installing needs administrator rights**, because the
operating system keeps service definitions in a folder only an administrator can
write to. The tool says so before it tries, rather than after it fails.

| Platform | Service definition | Configuration | Log |
|---|---|---|---|
| Windows | Registry key under `HKLM\SYSTEM\CurrentControlSet\Services\agent-db-doctor` | `%PROGRAMDATA%\agent-db-doctor\config.json` | `%PROGRAMDATA%\agent-db-doctor\logs\agent-db-doctor.log` |
| Linux | `/etc/systemd/system/agent-db-doctor.service` | `/etc/agent-db-doctor/config.json` | journald, and the configured log file |
| macOS | `/Library/LaunchDaemons/com.agentdbdoctor.plist` | `/Library/Application Support/agent-db-doctor/config.json` | the configured log file |

## Other actions

```bash
agent-db-doctor service status     # is it installed, is it running, what did it last say
agent-db-doctor service start      # start it now
agent-db-doctor service stop       # stop it now
agent-db-doctor service uninstall  # remove it; your configuration file is kept
```

`uninstall` deliberately leaves the configuration file alone. A person
uninstalling a tool because they are tidying up did not ask to have their
thresholds erased, and reinstalling should find their settings still there.

## Configuring it

The configuration is plain JSON with no comments, and the tool writes it on first
install. Every field is optional; a file with one field in it is valid and the rest
keep their defaults.

```json
{
  "watchedDatabases": [],
  "warningThresholdBytes": 53687091200,
  "criticalThresholdBytes": 107374182400,
  "checkIntervalSeconds": 3600,
  "allowAutomaticPruning": false
}
```

| Field | Default | What it does |
|---|---|---|
| `watchedDatabases` | `[]` | Full paths to watch. Empty means every supported agent. |
| `warningThresholdBytes` | `53687091200` (50 GB) | A line is logged above this size. |
| `criticalThresholdBytes` | `107374182400` (100 GB) | The line says it is urgent above this. |
| `checkIntervalSeconds` | `3600` | How long to wait between ticks. |
| `allowAutomaticPruning` | `false` | Whether the service may delete rows on its own. |

**`allowAutomaticPruning` is off by default and should stay off.** A tool that
changes a database while nobody is watching is a tool nobody trusts, and the
service is the last place safety can be taken on faith. Leaving it off means the
service tells you and you decide.

The tool refuses a configuration that is internally inconsistent rather than
quietly accepting it: a critical threshold below the warning one would mean the
warning never fires, and a check interval under ten seconds would spin rather than
watch. Both are refused with a sentence explaining which, and the same applies to
`--interval` on the command line:

```bash
$ agent-db-doctor service run --interval 1
! The configuration is not usable. The check interval must be at least ten seconds.
  Anything more often wastes power and fills the log without telling you anything new.

Nothing was changed.
```

Ten seconds is also the shortest interval the twenty four hour soak uses, which would
be 8,640 ticks over that period. That run was stopped after 276 ticks, so the memory
figure quoted in this project comes from the in-process benchmark instead. See
`benchmarks/watchdog-24h.md` for what was and was not measured.

## What it logs

One line, and only when there is something to say:

```
agent-db-doctor: /home/ann/.local/share/opencode/opencode.db is 61.2 GB, which is
above the warning threshold of 50.0 GB. Run: agent-db-doctor diagnose --database
"/home/ann/.local/share/opencode/opencode.db"
```

Three cases produce a line: a database above the warning threshold, one above the
critical threshold, and one that has grown since the previous tick. Everything
else produces nothing, which is what keeps the log small enough to read and the
process quiet enough to forget.

The line names the file and gives the exact command to run, because a person
reading a log at a glance needs to know what to type, not what went wrong.

## Running it without installing

On Windows the executable exports a `ServiceMain` entry point, which is what the
Service Control Manager looks up by name. `agent-db-doctor service run` enters the
same loop, which is useful for testing a configuration before installing it:

```bash
agent-db-doctor service run          # enters the loop; Ctrl-C to stop
agent-db-doctor service status       # in another window, to see it working
```

## How the loop is written

The loop sleeps in two hundred millisecond slices rather than in one long sleep,
and checks a stop flag between them. That is why a stop takes effect within a
fraction of a second instead of at the end of the hour.

```cpp
while (!stopRequested) {
    const WatchdogRunSummary summary = runOneWatchdogPass();
    for (const string& lineToLog : summary.messagesToLog) {
        logger.warn(lineToLog);
    }
    for (int slice = 0; slice < totalSlices && !stopRequested; ++slice) {
        PlatformMetadata::sleepMilliseconds(200);
    }
}
```

The log lines are written *after* the pass rather than during it, so the pass
itself contains no file writes and the system call count per tick stays equal to
the number of databases. The log file is opened once and held open for the life of
the process, which is what lets each line cost a single write.

The service runs as the local system account on Windows, because every supported
agent keeps its database in a per-user folder and no single user account can read
all of them. On Linux, systemd sets the home directory the tool looks in; on macOS,
the launch daemon records it in the property list at install time.
