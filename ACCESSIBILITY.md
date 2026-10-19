# Accessibility

This is the accessibility statement for agent-db-doctor: what the project commits
to, where the tool is known to work, what is known not to work, and how to report a
barrier. It describes the tool as it is today, including the parts that are not good
enough yet, because a statement that listed only the good parts would be no use to
the person deciding whether they can use this.

## The commitment

The command line program is the accessible surface of this project, and it is meant
to stay that way.

- **Plain text, in order.** The reporting commands print one fact per line, with no
  colour, no cursor movement, and no redrawing. The one command that redraws is
  `watch`, which is a live view by design: it clears the screen between refreshes
  only when its output is a terminal, so a redirected run, `watch --once`, and
  `--machine-readable` all produce plain lines. That clear-screen sequence is the
  only escape sequence the program ever writes, and everything else is plain text,
  so a screen reader, a braille display, `grep`, or a file receives the same
  characters.
- **Nothing is carried by colour alone.** The command line uses no colour; the
  window pairs every colour with a word, so a row is not "the green one" but a row
  whose health column reads `Sound`.
- **Nothing important hides behind hover.** Every value in the window is drawn on
  screen or shown in a dialog; the tooltips repeat what is already visible.
- **No timers and nothing to click in time.** No dialog closes by itself and no
  message scrolls away.
- **Machine readable output.** `--machine-readable` prints one JSON object per line:
  the same information without the prose, for a script or a reader that prefers
  structure.
- **The window follows the display's scale factor**, so a scaled display gets larger
  text, and it offers a light and a dark theme.

## Supported environments

| Surface | Where it is built and tested |
|---|---|
| The command line | Windows x64, macOS on Apple silicon, and Linux x64. Needs no display and no window system. |
| The window | The same three platforms. Needs a display: X11 or Wayland on Linux, an ordinary desktop session on Windows and macOS. |
| The HTML report (`diagnose --report`) | Any browser. It is plain semantic HTML: a declared language, a heading structure, real tables with header cells, and one dark text colour on white. Warnings are sentences in a highlighted block, not colours standing in for meanings. |
| The JSON output | Anything that reads text. |

Partial support, and the ground that has not been tested at all:

- **Wayland** is compiled against but never exercised: continuous integration runs
  the window under X11, and the Wayland path is untested.
- **Intel Macs** and **Windows on ARM** are not built or tested.
- **32 bit systems** are not supported.

## Known limitations

These are real, and they are the reason this page exists.

- **The window is not exposed to screen readers.** It is drawn with Dear ImGui,
  which renders its own widgets and does not publish them through the platform
  accessibility APIs (UI Automation on Windows, NSAccessibility on macOS, AT-SPI on
  Linux), so a screen reader sees the window as a single surface. Everything the
  window can do, the command line program can also do — the window's own settings,
  its default target and backup folder, are the only things it keeps to itself — and
  the command line is the recommended path for anyone using a screen reader.
- **Keyboard navigation inside the window is limited.** The window answers to
  `Ctrl+R`, which looks at every database again, and `Ctrl+Q`, which quits. The menus
  and the controls are reached with the pointer; there is no tab order through them
  yet.
- **The window scales with the display, not with the operating system's separate
  text size preference.** A display set to 150% gets larger text; the "make text
  bigger" setting is not read.
- **A destructive run asks for a typed `yes`.** That is deliberate, and it is also a
  barrier for anyone who cannot type: the exact word is what makes an accident
  unlikely. `--yes` exists for scripts, and the window never passes it.
- **There is no audio and no video**, in the tool or in its documentation, so there
  is nothing to caption.
- **The documentation is text.** Every image in the README carries `alt` text.

## Reporting a barrier

Please [open an accessibility report](https://github.com/richie-rich90454/agent-db-doctor/issues/new?template=accessibility_barrier.yml).
It asks where the barrier is (the command line, the window, or the documentation),
what you were trying to do, which assistive technology and version you use, and how
much it blocks you. If the form itself is a barrier, an ordinary issue is fine.

Reports are read and answered as quickly as one maintainer can manage, and there is
no service level agreement. When something is fixed, or when something new is found,
this page is updated; it is owned by the maintainer and reviewed whenever the
window's interface changes. [docs/roadmap.md](docs/roadmap.md) says what is planned.

## For contributors

- Keep the command line's output free of colour, cursor movement, and escape
  sequences other than the clear that `watch` sends to a terminal.
- Never let a colour carry a meaning on its own; pair it with a word.
- Give every image in the documentation `alt` text that says what the image shows.
- Do not put information behind hover that is not also on screen.
- The pull request template asks about these four things, so a change that breaks
  one of them is visible during review rather than after a release.
