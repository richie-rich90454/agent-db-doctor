# Agent adapters

How each supported coding agent stores its work, what this tool does about it, and
how to add another one.

The short version: **only OpenCode stores redundant snapshots**, so only OpenCode
is pruned. The other five are reported and left alone. That is a deliberate choice,
not an omission.

## Why only one agent is pruned

A duplicate snapshot is a row that records an *older version* of something the
agent has since changed, and whose content is therefore still present somewhere
else. Removing it loses nothing.

A plain text transcript has no such thing. Every line of a Codex CLI or Claude Code
session file is the only copy of that line. Deleting anything from one is the tool
deciding what a conversation should have said, and no tool gets to make that
decision for you.

| Agent | Storage | Duplicates? | What the tool does |
|---|---|---|---|
| OpenCode | One SQLite database | Yes | Diagnose, prune, compact |
| Cursor | One SQLite file per workspace | Unknown, undocumented | Diagnose only |
| Codex CLI | One JSONL file per session | No | Diagnose only |
| Claude Code | One JSONL file per project folder | No | Diagnose only |
| Continue | Config folder, optional index | No | Diagnose only |
| Aider | One Markdown transcript per project | No | Diagnose only |

## The schema the tool depends on

The prune command depends on four tables and five columns. It checks for all of
them before deleting anything, and refuses with a sentence naming whichever is
missing.

| Table | Columns the tool uses | Why |
|---|---|---|
| `session` | `id`, `time_created` | The boundary: which sessions are old enough to prune |
| `message` | `id`, `session_id` | Joining a session to its messages |
| `part` | `id`, `message_id`, `data` | **Where the conversation text actually lives** |
| `event` | `type`, `session_id` | The rows to remove |

The `part.data` column holds a JSON object. The safety check reads two fields from
it: `$.type`, which is `text` for a written message, and `$.text`, whose length
decides whether the part counts as readable. **This is the check that makes pruning
safe.** If the text lived only in the `event` rows, the check fails and nothing is
deleted.

## The second generation

Newer releases dual write a second generation alongside the first: the
`session.next.*` event family (`prompted`, `step.started`, `text.ended`,
`tool.called`, and others, with no version suffix on the stored string) plus the
`session_message`, `session_input`, and `session_context_epoch` tables. The
first generation tables keep their names and columns, so every query above stays
valid.

Pruning still removes only the first generation `*.updated.1` rows. The second
generation rows are each unique current state, not superseded snapshots, so
there is nothing safe to remove from them and the tool never touches those
tables. `SchemaVersionDetector` names the generation from the rows it finds
(`Version1`, `Version1And2`, `Version2Only`, or `Unknown`), the diagnose report
prints it, and prune refuses a database whose event content belongs to neither
generation rather than reporting "nothing to do" over rows it cannot read.

## The three event types

```
message.updated.1
message.part.updated.1
session.updated.1
```

The `.1` suffix is the agent's convention for "the first recorded version of this
object". When a message changes, the agent writes a new row for the new state and
leaves the old one in place. Every `updated.1` row is therefore a complete copy of a
state the conversation has already moved past.

They are declared as `constexpr` in `OpenCodeAgent.cpp` so the compiler puts them in
read-only memory, and they are compared by pointer rather than by content. That is
principle P-9 of the specification, and it matters at a hundred million rows.

## Paths on each platform

Every adapter lists candidates in priority order, and the tool costs one metadata
call per candidate. **No adapter walks a directory tree.**

### OpenCode

| Platform | Path |
|---|---|
| Linux, macOS | `~/.local/share/opencode/opencode.db` |
| Windows | `~/.local/share/opencode/opencode.db` (yes, on Windows too: this is where the live database sits) |
| Windows | `%LOCALAPPDATA%\opencode\opencode.db` |
| Windows | `%APPDATA%\opencode\opencode.db` |
| Windows | `~\AppData\Local\opencode\opencode.db` |
| Any | `$OPENCODE_DB` (absolute or `:memory:` names it directly, a bare name joins the data directory) |
| Any | `$OPENCODE_DATABASE` (the older name this tool used first, still honoured) |

The file is named after the release channel: `opencode.db` for the `latest`,
`beta`, and `prod` channels, and `opencode-{channel}.db` for anything else. The
channel names are not published anywhere a tool can read them, so each data
directory is listed once, one level deep, for files shaped like `opencode-*.db`,
unless `$OPENCODE_DISABLE_CHANNEL_DB` is `1` or `true`. A zero byte file loses
to a file with a size when several candidates exist, because some installs leave
empty placeholders behind.

### Cursor

Cursor stores one database per workspace, in a folder named after a hash of the
workspace path. No fixed candidate list can predict those names, which is the one
reason this tool opens a directory at all.

| Platform | Path |
|---|---|
| macOS | `~/Library/Application Support/Cursor/User/workspaceStorage` |
| Linux | `~/.config/Cursor/User/workspaceStorage` |
| Windows | `~\AppData\Roaming\Cursor\User\workspaceStorage` |

The listing is **one level deep and bounded at five hundred entries**, so a machine
with five thousand stored workspaces costs the same as one with five.

### Codex CLI

`~/.codex/sessions` on every platform, plus `$CODEX_HOME/sessions`.

### Claude Code

`~/.claude/projects` on every platform, plus `$CLAUDE_CONFIG_DIR/projects`.
One folder per working directory, and the session files are inside those. The
listing is bounded at two thousand entries.

### Continue

`~/.continue`, plus the two extension data folders. The listing is bounded at two
thousand entries, and only files above the reporting threshold are shown, because a
vector index can reach gigabytes and everything else is noise.

### Aider

`.aider.chat.history.md` in the project directory, which defaults to the process
working directory, plus the same file in the home directory.

## Adding an agent

Three files, and the changes are small.

**1. Declare the adapter**, in `src/AgentDbDoctor/Agents/YourAgent.hpp`:

```cpp
class YourAgent final {
public:
    YourAgent();
    string displayName() const;
    optional<string> findDatabasePath() const;
    vector<string> redundantEventTypes() const;
    vector<string> likelyProcessNames() const;
    AgentDescriptor describe() const;
private:
    static vector<string> buildCandidatePaths();
    AgentDatabaseLocator databaseLocator;
};
```

`redundantEventTypes()` returns an empty vector unless you have **verified** that
the agent writes snapshots of state it has elsewhere. Returning an empty vector
means the agent is reported and never pruned, which is the safe default and the
right answer for every agent except OpenCode.

**2. Implement it**, in `src/AgentDbDoctor/Agents/YourAgent.cpp`. Build the
candidate list in the constructor, once, and let the locator do the testing:

```cpp
YourAgent::YourAgent()
    : databaseLocator(buildCandidatePaths()) {
}
```

**3. Register it**, in `src/AgentDbDoctor/Agents/AgentRegistry.hpp` and
`.cpp`: a forward declaration, a `unique_ptr` field, and an entry in
`allAgents()`. The registry owns the agent; nothing derives from anything.

**4. Add the paths** to `agentRegistry.discoverAllDatabases()` if the agent stores
more than one file, as Claude Code and Cursor do.

**5. Add a test** to `tests/unit/TestAgents.cpp`. The two properties worth
asserting for every adapter are that its descriptor is complete, because a report
with a blank section is a bug, and that its candidate list costs one metadata call
per entry.

### What not to do

- **Do not reverse engineer an undocumented schema.** Cursor's tables are
  undocumented, so the tool reports their sizes and refuses to prune them. A guess
  that deletes the wrong rows loses editor state.
- **Do not walk a directory tree.** If a folder name is unpredictable, use one
  bounded level and a documented cap.
- **Do not add a dependency** to read the format. The permitted five are listed in
  `CMakeLists.txt`.
