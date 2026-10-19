# Verification

This page is written by the continuous integration workflow itself, not by hand.
After the platform jobs finish on a push to the main branch, the verification job
asks the GitHub API what ran, reads each job's compiler banner back from that
job's log, and commits the result here. A run whose jobs failed writes the page
too, because that is when the record matters most.

A dash in the toolchain column means the job did not get as far as printing its
compiler banner.

| Job | Toolchain | Result | Started (UTC) | Finished (UTC) | Duration |
|---|---|---|---|---|---|
| Linux (gcc) | - | success | 2026-10-19 13:01:08 | 2026-10-19 13:04:32 | 3m 24s |
| Windows (MSVC) | - | failure | 2026-10-19 13:01:08 | 2026-10-19 13:09:08 | 8m 00s |
| Linux (clang) | - | success | 2026-10-19 13:01:08 | 2026-10-19 13:06:36 | 5m 28s |
| macOS (AppleClang) | - | success | 2026-10-19 13:01:15 | 2026-10-19 13:05:16 | 4m 01s |

Run: [https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39256608836](https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39256608836), attempt 1.

Commit: `574b9653043dac7d3a1b1abb94655364a6e2ab2c` on `main`.

Generated at 2026-10-19 13:13:54 UTC.
