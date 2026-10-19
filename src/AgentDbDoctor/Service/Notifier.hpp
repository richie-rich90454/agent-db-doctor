// The Notifier class, which shows a desktop notification on the machine the
// service watches.
//
// It exists because the service's log is a file nobody reads until something has
// already gone wrong. A notification is the one channel that reaches a person who
// is not looking at the tool, so it is worth the platform specific code below.
//
// Two things must be understood about it. First, it is best effort: it spawns the
// platform's own notification program, and if that program is missing, disabled,
// or refuses, nothing is reported back and nothing else changes. Second, and more
// importantly, a notification sent by a system service may not be visible at all:
// on Windows the service runs in its own session and its tray balloon is drawn in
// that session, on Linux the daemon may have no display to talk to, and on macOS a
// launch daemon does not own the user's session. That is exactly why every
// notification is paired with a log line, which is written whether or not the
// notification can be seen, and why the documentation tells a user to look at the
// log if they expected a notification and saw none.
#ifndef AGENT_DB_DOCTOR_SERVICE_NOTIFIER_HPP
#define AGENT_DB_DOCTOR_SERVICE_NOTIFIER_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
using namespace std;
namespace AgentDbDoctor::Service {
// The Notifier class raises one desktop notification.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class Notifier final {
public:
    Notifier()=delete;
    // Shows a notification with the given title and message. Returns nothing and
    // throws nothing: a notification that cannot be shown is not a failure of the
    // thing that raised it.
    static void notify(const string& title, const string& message);
};
}
#endif
