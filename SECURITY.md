# Security policy

## Reporting a vulnerability

Open an issue at
https://github.com/richie-rich90454/agent-db-doctor/issues with the details, or
email the maintainer through the address on the GitHub profile for anything that
should not be public yet. Please include what you ran, what happened, and what you
expected.

The two things that matter most, and that will be treated as vulnerabilities rather
than as bugs:

1. **A path where the tool loses or corrupts user data** it promised to protect.
2. **A path where a command runs with more privilege than it needs**, especially
   around the service installation, which runs as an administrator or as root.

## What the tool does, in security terms

- It reads and writes SQLite database files in the user's own directories, and
  files under the shared configuration directory for the service.
- It runs no shell commands for its normal operation. The macOS service
  installation uses `launchctl`, `chown`, and `chmod` with fixed paths; the Linux
  one uses `systemctl`; the Windows one uses the service control manager and the
  registry directly.
- Its SQL is built from constants and bound parameters. The one place a path
  appears inside a statement, `VACUUM INTO`, is escaped as a SQL string literal.
- SQLite is vendored and compiled without extension loading, without shared cache,
  and without double-quoted string literals.
- Every dependency is pinned by commit hash or by a SHA-256 of the archive, so a
  moved tag or a replaced download cannot change what is built.

## What it will not do

- It never deletes a database file.
- It never writes to a database during `diagnose` or `agents`; both open read only.
- It never changes a database it was not asked about.
- It never asks for elevation by itself; installing the service is the one command
  that needs administrator rights, and it says so before it starts.

## Supported versions

The project is versioned with semantic versioning. Security fixes are made on the
latest release. The version is currently 1.0.0 and the changelog records every
change.
