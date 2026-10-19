<!--
Thank you for the change. This asks the questions the review will ask, so that the
review can be about the change rather than about missing information. Delete a
section only when it truly does not apply.
-->

## What this changes

<!-- One paragraph: what was wrong or missing, and what the change does about it. -->

## Why

<!-- The reason, not the mechanism. What does a user get that they did not have before? -->

## How it was verified

<!--
The commands you actually ran, on which compiler and which platform. "It builds" is
not a verification in this project; the boxes below are what a reviewer looks for.
-->

- [ ] `cmake --build build` with the project's warning set (`-Werror` on GCC and Clang, `/WX` on MSVC)
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] The command line program and the window both started

```
The commands you ran, and what they printed.
```

## If this writes to a database

<!-- The five steps from CONTRIBUTING.md. A change that writes to a database without all five is not ready. -->

- [ ] `SafetyChecks` runs first, and can refuse
- [ ] A complete copy is made first, including the `-wal` and `-shm` side files
- [ ] The copy's path is printed
- [ ] The user is asked, unless `--yes` was passed
- [ ] The content is verified afterwards, and the copy is put back if the check fails
- [ ] A test proves that a failed verification restores the original

## If this adds code

- [ ] Every class is `final`; there is no inheritance anywhere in this codebase
- [ ] The formatter has been run: `cmake --build build --target format`
- [ ] New logic has a unit test; a new command has an integration test against a real SQLite file
- [ ] No new dependency (the five permitted ones are listed in `CMakeLists.txt`)
- [ ] Nothing walks a directory tree; discovery is still a fixed list of candidate paths

## Performance

<!-- The contract in docs/performance.md. Answer all three, even when the answer is "nothing changed". -->

- System calls added per diagnostic pass:
- Bytes read added:
- Complexity of the new work:

## Accessibility

<!-- What ACCESSIBILITY.md commits to. -->

- [ ] The command line's output stays free of colour, cursor movement, and escape sequences other than `watch`'s terminal clear
- [ ] No meaning is carried by colour alone; the window pairs every colour with a word
- [ ] Nothing new is reachable only by hover
- [ ] New images in the documentation carry `alt` text

## Documentation

- [ ] Behaviour, flags, and exit codes that changed are reflected in the docs
- [ ] `docs/json-api.md` is updated if a field was added to the machine readable output
- [ ] `CHANGELOG.md` has an entry under Unreleased

## Not verified

<!--
Say what you could not check, and why. "The macOS path compiles here and was not run"
is a useful sentence; silence about it is not.
-->
