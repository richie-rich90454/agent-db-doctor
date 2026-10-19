# Verification

This page is written by the continuous integration workflow itself, not by hand.
After the platform jobs finish on a push to the main branch, the verification job
asks the GitHub API what ran, reads the compiler banner each job uploaded, and
commits the result here. A run whose jobs failed writes the page too, because that
is when the record matters most.

A dash in the toolchain column means the job did not get as far as reporting its
compiler banner.

| Job | Toolchain | Result | Started (UTC) | Finished (UTC) | Duration |
|---|---|---|---|---|---|
| Windows (MSVC) | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 | success | 2026-10-19 13:38:45 | 2026-10-19 13:46:22 | 7m 37s |
| Linux (clang) | Ubuntu clang version 21.1.8 (6ubuntu1) | success | 2026-10-19 13:38:45 | 2026-10-19 13:42:41 | 3m 56s |
| Linux (gcc) | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | success | 2026-10-19 13:38:45 | 2026-10-19 13:43:29 | 4m 44s |
| macOS (AppleClang) | Apple clang version 21.0.0 (clang-2100.1.1.101) | success | 2026-10-19 13:39:00 | 2026-10-19 13:42:26 | 3m 26s |

Run: [https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39261905182](https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39261905182), attempt 1.

Commit: `7030afeeefd4831878b702cca808974570c82f5f` on `main`.

Generated at 2026-10-19 13:46:30 UTC.
