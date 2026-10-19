# Integrations

Where the tool meets other software: continuous integration, containers,
Prometheus, and the two package managers the releases are prepared for.

## The GitHub Action

`action.yml` at the repository root is a composite action that builds the command
line program from the checked out source and runs it. It expects cmake and a C++
compiler on the runner; a GitHub hosted `ubuntu-latest` runner has both, and the
build leaves the graphical interface out, so SDL3 is never downloaded.

Use it where the runner can see the databases: a self-hosted runner on the
machine that keeps them, or a job that restores a database first. The workspace
must be the agent-db-doctor source, which is why the checkout lists the
repository explicitly.

```yaml
name: Check the agent databases
on:
  schedule:
    - cron: "17 6 * * *"
  workflow_dispatch:
jobs:
  doctor:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
        with:
          repository: richie-rich90454/agent-db-doctor
      - uses: richie-rich90454/agent-db-doctor@v1.0.0
        with:
          arguments: "--machine-readable --quiet"
```

| Input | Default | Meaning |
|---|---|---|
| `database` | empty | One database to work on. Empty checks every database the runner can find. |
| `arguments` | `--machine-readable` | Extra arguments, split on spaces. Setting it replaces the default. |
| `target` | empty | When set, the action runs `reclaim --target <size>` instead of `diagnose`. |
| `build-type` | `Release` | The CMake build type. |

The action fails the run when the program exits non-zero, and that is its point
as a pipeline guard:

| Exit code | Run result | Meaning |
|---|---|---|
| 0 | pass | Every database the program looked at is healthy. |
| 1 | fail | A database needs attention, or a command refused to run. |
| 2 | fail | The command line itself was wrong. |

A reclaim asks once before it touches anything, so a headless run passes `--yes`
in `arguments` and, if a safety copy should survive the run, `--keep-backup`:

```yaml
      - uses: richie-rich90454/agent-db-doctor@v1.0.0
        with:
          database: /home/runner/.local/share/opencode/opencode.db
          target: 20GB
          arguments: "--yes --keep-backup"
```

## Prometheus

`diagnose --prometheus` writes node_exporter textfile metrics for every database
the run looked at: the size in bytes, the bytes a vacuum would hand back, a
healthy flag, and the health score, each labelled with the agent and the
database's full path. The metrics are appended to the command's normal output, so
the pipeline keeps the metric lines and drops everything else before the
collector sees the file:

```bash
agent-db-doctor diagnose --prometheus --quiet \
  | grep -E '^(# (HELP|TYPE) agent_db_doctor_|agent_db_doctor_)' \
  > /var/lib/node_exporter/agent_db_doctor.prom.tmp
mv /var/lib/node_exporter/agent_db_doctor.prom.tmp /var/lib/node_exporter/agent_db_doctor.prom
```

The rename is deliberate: node_exporter's textfile collector only reads files
whose names end in `.prom`, so a half-written file is never scraped. The text the
filter keeps looks like this:

```
# HELP agent_db_doctor_size_bytes The size of each database file in bytes.
# TYPE agent_db_doctor_size_bytes gauge
agent_db_doctor_size_bytes{agent="OpenCode",path="/home/ann/.local/share/opencode/opencode.db"} 3045969920
# HELP agent_db_doctor_reclaimable_bytes The bytes a vacuum would hand back.
# TYPE agent_db_doctor_reclaimable_bytes gauge
agent_db_doctor_reclaimable_bytes{agent="OpenCode",path="/home/ann/.local/share/opencode/opencode.db"} 1486577664
# HELP agent_db_doctor_healthy 1 when the database is sound, 0 otherwise.
# TYPE agent_db_doctor_healthy gauge
agent_db_doctor_healthy{agent="OpenCode",path="/home/ann/.local/share/opencode/opencode.db"} 1
# HELP agent_db_doctor_health_score The health score from 0 to 100, or -1 when unknown.
# TYPE agent_db_doctor_health_score gauge
agent_db_doctor_health_score{agent="OpenCode",path="/home/ann/.local/share/opencode/opencode.db"} 78
```

Node exporter is pointed at the directory once:

```bash
node_exporter --collector.textfile.directory=/var/lib/node_exporter
```

A cron entry keeps the file fresh, and an alert rule watches it:

```
*/15 * * * * agent-db-doctor diagnose --prometheus --quiet | grep -E '^(# (HELP|TYPE) agent_db_doctor_|agent_db_doctor_)' > /var/lib/node_exporter/agent_db_doctor.prom.tmp; mv /var/lib/node_exporter/agent_db_doctor.prom.tmp /var/lib/node_exporter/agent_db_doctor.prom
```

```yaml
- alert: AgentDatabaseNeedsAttention
  expr: agent_db_doctor_healthy == 0
  for: 1h
  labels:
    severity: warning
  annotations:
    summary: "{{ $labels.agent }} database {{ $labels.path }} needs attention"
    description: "Run agent-db-doctor diagnose --database {{ $labels.path }} on the machine that holds it."
```

## winget and Homebrew

The manifests for both package managers are prepared under `packaging/`, and both
wait for a release: the download URLs and the installer hashes only exist once
the release artifacts exist, so the manifests ship as templates and are filled in
by the scripts in that folder. The full routine, including the pull request to
`microsoft/winget-pkgs`, is in `packaging/README.md`.

| Manager | What is prepared | What the user runs after publishing |
|---|---|---|
| winget | The three Community Repository manifests, generated from the release zip | `winget install richie-rich90454.agent-db-doctor` |
| Homebrew | A formula for a personal tap, generated from the two release tarballs | `brew tap richie-rich90454/tap` then `brew install agent-db-doctor` |

Both cost nothing but a GitHub account: the Community Repository accepts the
manifest pull request from the account that will maintain it, and a Homebrew tap
is an ordinary repository named `homebrew-tap`.

Every release package is signed by the release workflow, which builds the packages
when a release is published and attaches them, and the signature and certificate
travel with it as `<package>.sig` and `<package>.pem`. Verification is keyless, so
the identity checked is the workflow, not a key:

```bash
cosign verify-blob \
  --certificate agent-db-doctor-1.0.0-windows-x86_64.zip.pem \
  --signature agent-db-doctor-1.0.0-windows-x86_64.zip.sig \
  --certificate-identity-regexp '^https://github\.com/richie-rich90454/agent-db-doctor/\.github/workflows/release\.yml@refs/tags/v' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com \
  agent-db-doctor-1.0.0-windows-x86_64.zip
```

## The fuzz target

The service configuration reader is the project's own hand written parser for a
subset of JSON, and `tests/fuzz/ConfigurationFuzz.cpp` feeds arbitrary bytes to
it through `ConfigurationFile::fromText`. The build needs Clang, because
`-fsanitize=fuzzer` is a Clang feature, and the target only exists behind its own
option:

```bash
cmake -S . -B build-fuzz \
  -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang \
  -DAGENT_DB_DOCTOR_ENABLE_FUZZING=ON
cmake --build build-fuzz
./build-fuzz/bin/fuzz_configuration -max_total_time=60
```

The binary lands in `build-fuzz/bin` because the project sends every executable
to a `bin` folder below the build directory. Run it for as long as a machine can
be spared; a crash is written to disk as `crash-<hash>` and reproduced by passing
that file as the only argument. A directory of seed inputs can be passed ahead of
the flags, and the empty corpus the run starts from is enough to find shallow
mistakes in minutes.
