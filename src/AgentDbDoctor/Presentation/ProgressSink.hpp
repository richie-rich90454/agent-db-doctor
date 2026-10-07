// The ProgressSink vocabulary, shared by the console bar and the future GUI.
//
// A long operation reports `(done, total, description)` through a sink instead
// of writing to the screen itself. The console `ProgressBar` stays the only
// thing that writes text, and a graphical front end sets a sink that records
// the same numbers for its own widgets. There is no base class and no virtual
// dispatch: a sink is a plain function value, which is composition without
// inheritance.
#ifndef AGENT_DB_DOCTOR_PRESENTATION_PROGRESSSINK_HPP
#define AGENT_DB_DOCTOR_PRESENTATION_PROGRESSSINK_HPP
// Provides `function`, which holds any callable with a matching signature.
#include <functional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
using namespace std;
namespace AgentDbDoctor::Presentation {
// One progress report: how far a named task has got.
//
// `isFinished` is true only on the final report, so a receiver can tell a
// complete bar from an abandoned one without a separate call.
struct ProgressUpdate final {
    // What the work is, for example "Removing older copies".
    string taskDescription;
    // How many units are done. Units are rows, bytes, or pages depending on
    // the operation, exactly as the console bar counts them.
    long long completedUnits=0;
    // How many units the task has in total. Zero means the total is not known
    // yet, in which case only the description is meaningful.
    long long totalUnits=0;
    // True when the task is done. No further updates follow.
    bool isFinished=false;
};
// A receiver of progress reports. Empty by default, in which case reports go
// nowhere and cost nothing.
using ProgressSink=function<void(const ProgressUpdate&)>;
}
#endif
