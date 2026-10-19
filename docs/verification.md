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
| Linux (clang) | Ubuntu clang version 21.1.8 (6ubuntu1) | success | 2026-10-19 13:50:37 | 2026-10-19 13:56:06 | 5m 29s |
| Windows (MSVC) | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 | success | 2026-10-19 13:50:37 | 2026-10-19 14:02:41 | 12m 04s |
| Linux (gcc) | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | success | 2026-10-19 13:50:38 | 2026-10-19 13:55:37 | 4m 59s |
| macOS (AppleClang) | Apple clang version 21.0.0 (clang-2100.1.1.101) | success | 2026-10-19 13:50:39 | 2026-10-19 13:54:02 | 3m 23s |

Run: [https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39263608385](https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39263608385), attempt 1.

Commit: `c242e19095aa14f8de34ae0dfc9f7c740a0e2613` on `main`.

Generated at 2026-10-19 14:02:51 UTC.
