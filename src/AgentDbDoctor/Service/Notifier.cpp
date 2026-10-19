// The implementation of the Notifier class.
//
// Each platform has its own one line recipe: a PowerShell balloon on Windows,
// `notify-send` on Linux, and `osascript` on macOS. All three are invoked through
// `system`, which is the only launching facility the standard library offers and
// the only one this file needs. The text is quoted for the shell it will pass
// through, and the exit status is deliberately not acted on: a notification that
// fails has no consequence except that a person sees the log line instead. It is
// still captured in a named constant rather than discarded, because GCC marks
// `system` as `warn_unused_result` and this file is compiled with `-Werror`.
// Provides `system`, which runs the platform's notification program.
#include <cstdlib>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the Notifier class declaration.
#include "AgentDbDoctor/Service/Notifier.hpp"
using namespace std;
namespace AgentDbDoctor::Service {
namespace {
// Collapses the characters that would break a quoted command line into spaces.
// A message is built from paths and sizes, so it has no need for a line break,
// and flattening it lets each platform's quoting rule stay simple enough to
// check by eye.
string flattenIntoOneLine(const string& text) {
    string flattenedText;
    flattenedText.reserve(text.size());
    for (const char currentCharacter : text) {
        if (currentCharacter=='\n' || currentCharacter=='\r' || currentCharacter=='\t') {
            flattenedText.push_back(' ');
        } else {
            flattenedText.push_back(currentCharacter);
        }
    }
    return flattenedText;
}
#if defined(_WIN32)
// Builds a single quoted PowerShell string. A single quote inside the text is
// doubled, which is the escape PowerShell uses; a double quote would end the
// argument this script is passed as, so it is written as a single quote instead.
string powershellQuoted(const string& text) {
    string quotedText="'";
    for (const char currentCharacter : text) {
        if (currentCharacter=='\'') {
            quotedText+="''";
        } else if (currentCharacter=='"') {
            quotedText.push_back('\'');
        } else {
            quotedText.push_back(currentCharacter);
        }
    }
    quotedText.push_back('\'');
    return quotedText;
}
#else
// Builds a single quoted shell word for `sh`, which is what `system` uses on
// Linux and macOS. A single quote inside the word ends the quoting, so it is
// closed, escaped, and opened again: the standard `'\''` device.
string shellQuoted(const string& text) {
    string quotedText="'";
    for (const char currentCharacter : text) {
        if (currentCharacter=='\'') {
            quotedText+="'\\''";
        } else {
            quotedText.push_back(currentCharacter);
        }
    }
    quotedText.push_back('\'');
    return quotedText;
}
#endif
#if defined(__APPLE__)
// Builds the AppleScript source that shows a notification. Inside an AppleScript
// string literal a backslash escapes the next character, so both it and the
// quote that ends the literal are escaped.
string appleScriptQuoted(const string& text) {
    string quotedText="\"";
    for (const char currentCharacter : text) {
        if (currentCharacter=='\\' || currentCharacter=='"') {
            quotedText.push_back('\\');
        }
        quotedText.push_back(currentCharacter);
    }
    quotedText.push_back('"');
    return quotedText;
}
#endif
}
void Notifier::notify(const string& title, const string& message) {
    const string flatTitle=flattenIntoOneLine(title);
    const string flatMessage=flattenIntoOneLine(message);
#if defined(_WIN32)
    // The balloon needs its assembly loaded by name, and the icon needs the
    // drawing assembly beside it. The sleep keeps the process alive while the
    // balloon is on screen; without it the icon would be disposed before anyone
    // could see the message. The whole script is one argument, wrapped in double
    // quotes for the command interpreter that runs `powershell`.
    const string script=
        "[void][System.Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms');"
        "[void][System.Reflection.Assembly]::LoadWithPartialName('System.Drawing');"
        "$icon=New-Object System.Windows.Forms.NotifyIcon;"
        "$icon.Icon=[System.Drawing.SystemIcons]::Information;"
        "$icon.BalloonTipTitle=" + powershellQuoted(flatTitle) + ";"
        "$icon.BalloonTipText=" + powershellQuoted(flatMessage) + ";"
        "$icon.Visible=$true;"
        "$icon.ShowBalloonTip(5000);"
        "Start-Sleep -Seconds 6;"
        "$icon.Dispose()";
    const int launchStatus=system(
        ("powershell -NoProfile -WindowStyle Hidden -Command \"" + script + "\"").c_str()
    );
    static_cast<void>(launchStatus);
#elif defined(__APPLE__)
    const int launchStatus=system(
        ("osascript -e " + shellQuoted(
            "display notification " + appleScriptQuoted(flatMessage)
            + " with title " + appleScriptQuoted(flatTitle)
        )).c_str()
    );
    static_cast<void>(launchStatus);
#elif defined(__linux__)
    const int launchStatus=system(
        ("notify-send " + shellQuoted(flatTitle) + " " + shellQuoted(flatMessage)).c_str()
    );
    static_cast<void>(launchStatus);
#else
    // No notification program is known on this platform, so the log line the
    // caller always writes is the whole of the announcement.
    static_cast<void>(flatTitle);
    static_cast<void>(flatMessage);
#endif
}
}
