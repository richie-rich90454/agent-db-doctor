# Supported agents

Sixty two coding agents are supported. Seven of them keep SQLite databases and can
be diagnosed *and* vacuumed; the rest keep text, JSON, or configuration and are
diagnosed and left alone. Only OpenCode stores redundant snapshots, so only
OpenCode's event log is compacted.

`agent-db-doctor agents` prints this list with the paths found on your machine.
`agent-db-doctor agents --verify` also prints every candidate path that was looked
at and not found, which is where an agent keeps its data when it is installed.

## The seven that are vacuumed

| Agent | Database location |
|---|---|
| OpenCode | `~/.local/share/opencode/opencode.db` (compacted and vacuumed) |
| Cursor | platform-specific, one SQLite file per workspace |
| VS Code | platform-specific, one `state.vscdb` per workspace |
| Cline | `~/.cline/data/db/sessions.db` |
| Windsurf | platform-specific data dir |
| Zed | platform-specific user data dir |
| Void | platform-specific data dir |

## Every supported agent

| Agent | Database location | What the tool does |
|---|---|---|
| Codex CLI | `~/.codex/sessions/` (JSONL) | Diagnose only; no duplicate snapshots |
| Claude Code | `~/.claude/projects/` (JSONL) | Diagnose only; no duplicate snapshots |
| Continue | `~/.continue/` | Diagnose only; the index is rebuildable |
| Aider | `.aider.chat.history.md` | Diagnose only; a transcript is the only copy |
| Roo Code | `~/.roo/` plus VS Code extension storage | Diagnose only; task state, undocumented |
| Reasonix | `~/.reasonix/` | Diagnose only; skills and configuration |
| Gemini CLI | `~/.gemini/` | Diagnose only; session history, 30-day retention |
| GitHub Copilot | VS Code extension storage | Diagnose only; chat state, undocumented |
| Warp | platform-specific data dir | Diagnose only; terminal state, undocumented |
| Kilo Code | `~/.kilo/` plus VS Code extension storage | Diagnose only; task state, undocumented |
| Augment | VS Code extension storage | Diagnose only; assistant state, undocumented |
| Trae | platform-specific data dir | Diagnose only; workspace state, undocumented |
| PearAI | platform-specific data dir | Diagnose only; workspace state, undocumented |
| Amp | `~/.amp/` | Diagnose only; CLI session state, undocumented |
| CommandCode | `~/.commandcode/` | Diagnose only; session state, undocumented |
| Pi | `~/.pi/` | Diagnose only; CLI session state, undocumented |
| Tabnine | `~/.tabnine/` plus editor storage | Diagnose only; completion state, undocumented |
| Cody | `~/.cody/` plus editor storage | Diagnose only; chat state, undocumented |
| Codeium | `~/.codeium/` plus editor storage | Diagnose only; assistant state, undocumented |
| Supermaven | editor extension storage | Diagnose only; completion state, undocumented |
| Bito | editor extension storage | Diagnose only; assistant state, undocumented |
| CodeGPT | editor extension storage | Diagnose only; assistant state, undocumented |
| Blackbox AI | editor extension storage | Diagnose only; assistant state, undocumented |
| AskCodi | editor extension storage | Diagnose only; assistant state, undocumented |
| Phind | editor extension storage | Diagnose only; completion state, undocumented |
| IntelliCode | editor extension storage | Diagnose only; completion state, undocumented |
| Tabby | `~/.tabby/` plus editor storage | Diagnose only; assistant state, undocumented |
| Pieces | `~/.pieces/` | Diagnose only; snippets and state, undocumented |
| Amazon Q | `~/.amazonq/` | Diagnose only; CLI session state, undocumented |
| Junie | `~/.junie/` | Diagnose only; session state, undocumented |
| Qoder | platform-specific data dir | Diagnose only; workspace state, undocumented |
| Lingma | `~/.lingma/` | Diagnose only; session state, undocumented |
| Comate | `~/.comate/` | Diagnose only; session state, undocumented |
| Droid | `~/.factory/`, `~/.droid/` | Diagnose only; terminal state, undocumented |
| GitLab Duo | editor extension storage | Diagnose only; assistant state, undocumented |
| WorkBuddy | `~/.workbuddy/` | Diagnose only; task state, undocumented |
| Kimi | `~/.kimi/` (`$KIMI_SHARE_DIR` overrides) | Diagnose only; session history, undocumented |
| Kiro | `~/.kiro/` (`$KIRO_HOME` overrides) | Diagnose only; agents and sessions, undocumented |
| Qwen Code | `~/.qwen/` | Diagnose only; CLI session state, undocumented |
| CodeBuddy | `~/.codebuddy/` | Diagnose only; task state, undocumented |
| MarsCode | `~/.marscode/` | Diagnose only; session state, undocumented |
| Qodo | `~/.qodo/` | Diagnose only; review state, undocumented |
| Sourcery | editor extension storage | Diagnose only; review state, undocumented |
| Snyk | editor extension storage | Diagnose only; scan state, undocumented |
| SonarQube | editor extension storage | Diagnose only; analysis state, undocumented |
| AnythingLLM | `~/.anythingllm/` | Diagnose only; chat history, undocumented |
| GPT4All | platform-specific data dir | Diagnose only; chat history, undocumented |
| Jan | `~/.jan/` | Diagnose only; chat history, undocumented |
| LM Studio | `~/.lmstudio/` | Diagnose only; chat history, undocumented |
| Open WebUI | `~/.open-webui/` | Diagnose only; chat history, undocumented |
| AIChat | `~/.config/aichat/` | Diagnose only; terminal state, undocumented |
| gptme | `~/.config/gptme/` | Diagnose only; conversation logs, undocumented |
| LLM | `~/.llm/` | Diagnose only; logged prompts, undocumented |
| ShellGPT | `~/.config/shell_gpt/` | Diagnose only; terminal state, undocumented |
| Raycast | platform-specific data dir | Diagnose only; launcher state, undocumented |

Where the table says "unsupported" or "undocumented", it means the tool reports
what the path contains and changes nothing: the agent's own documentation does not
describe the tables, and inventing a deletion rule for undocumented tables would be
guessing with someone's history.

## Where the paths come from

A path is one of three things, and [agent-adapters.md](agent-adapters.md) says which
one applies to each agent:

- confirmed on a real machine where the agent is installed;
- taken from the vendor's own documentation;
- the vendor's published default, which nobody has confirmed.

## Adding an agent

Adding an adapter is one small class in `src/AgentDbDoctor/Agents/`, and
[agent-adapters.md](agent-adapters.md) walks through it: the candidate paths for
each platform, the display name, whether the storage is SQLite, and, for an agent
whose duplicate snapshots are documented, the event types that name an older copy.
Everything else — diagnose, vacuum, the interface, the service — follows from the
descriptor.
