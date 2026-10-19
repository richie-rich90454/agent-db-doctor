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
| Windows (MSVC) | - | success | 2026-10-19 11:02:07 | 2026-10-19 11:07:11 | 5m 04s |
| Linux (clang) | - | success | 2026-10-19 11:02:07 | 2026-10-19 11:07:28 | 5m 21s |
| Linux (gcc) | - | success | 2026-10-19 11:02:07 | 2026-10-19 11:06:07 | 4m 00s |
| macOS (AppleClang) | - | failure | 2026-10-19 11:02:13 | 2026-10-19 11:05:24 | 3m 11s |

Run: [https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39241687574](https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39241687574), attempt 1.

Commit: `6532579146047f8418e617a7d175552228678fa6` on `main`.

Generated at 2026-10-19 11:07:41 UTC.
