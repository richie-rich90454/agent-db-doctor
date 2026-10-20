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
| Windows (MSVC) | Microsoft (R) C/C++ Optimizing Compiler Version 19.51.36260 for x64 | success | 2026-10-20 12:35:05 | 2026-10-20 12:42:32 | 7m 27s |
| Linux (gcc) | g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0 | success | 2026-10-20 12:35:05 | 2026-10-20 12:39:44 | 4m 39s |
| Linux (clang) | Ubuntu clang version 21.1.8 (6ubuntu1) | success | 2026-10-20 12:35:06 | 2026-10-20 12:40:59 | 5m 53s |
| macOS (AppleClang) | Apple clang version 21.0.0 (clang-2100.1.1.101) | success | 2026-10-20 12:35:11 | 2026-10-20 12:38:43 | 3m 32s |

Run: [https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39423383901](https://github.com/richie-rich90454/agent-db-doctor/actions/runs/39423383901), attempt 1.

Commit: `21da118f9904113086d147a8f55a5b0d9fcf103b` on `main`.

Generated at 2026-10-20 12:42:38 UTC.
