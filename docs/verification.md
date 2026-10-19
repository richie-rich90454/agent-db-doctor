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
| Windows (MSVC) | - | success | 2026-10-19 07:52:12 | 2026-10-19 07:59:21 | 7m 09s |
| Linux (gcc) | - | success | 2026-10-19 07:52:13 | 2026-10-19 07:57:12 | 4m 59s |
| Linux (clang) | - | failure | 2026-10-19 07:52:13 | 2026-10-19 07:54:52 | 2m 39s |
| macOS (AppleClang) | - | failure | 2026-10-19 07:52:18 | 2026-10-19 07:55:37 | 3m 19s |
| Sign the release packages | - | skipped | 2026-10-19 07:59:22 | 2026-10-19 07:59:22 | 0s |

Run: [https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39218777752](https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39218777752), attempt 1.

Commit: `381e2a7644de048351067557e9352beca6b8862f` on `main`.

Generated at 2026-10-19 07:59:31 UTC.
