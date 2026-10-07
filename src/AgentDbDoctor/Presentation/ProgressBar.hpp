// The ProgressBar class, which shows how far a long operation has got.
//
// The specification requires that any operation taking more than two hundred
// milliseconds shows a progress bar with an estimate of the time remaining, and
// that no escape sequences are written when standard output is not a terminal. The
// second rule is what makes the tool safe to pipe into a file or a log.
#ifndef AGENT_DB_DOCTOR_PRESENTATION_PROGRESSBAR_HPP
#define AGENT_DB_DOCTOR_PRESENTATION_PROGRESSBAR_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the ProgressSink vocabulary, so a bar can feed a graphical front end
// the same numbers it draws.
#include "AgentDbDoctor/Presentation/ProgressSink.hpp"
using namespace std;
namespace AgentDbDoctor::Presentation {
// The ProgressBar class draws a single line of progress and then erases it.
//
// The class is `final` because the specification forbids inheritance. It is held
// by value by whichever command is running, so the line disappears automatically
// when that command finishes, even if the command returns early.
class ProgressBar final {
public:
    // Builds a bar for a task with a known total.
    //
    // `taskDescription` is a short phrase such as "Removing duplicate rows".
    // `totalUnits` is how many units of work there are, which is a row count, a
    // byte count, or a page count depending on the operation.
    //
    // `isTerminalAttached` should be the result of asking the platform whether
    // standard output is a terminal. When it is false the class prints one plain
    // line every so often instead of a bar, because a bar made of carriage returns
    // is unreadable in a log file.
    ProgressBar(string taskDescription, long long totalUnits, bool isTerminalAttached);
    // Stops the bar and leaves the final state on the screen.
    ~ProgressBar();
    ProgressBar(const ProgressBar&)=delete;
    ProgressBar& operator=(const ProgressBar&)=delete;
    // Records that some units of work are done and redraws the line.
    //
    // The method is cheap: it does arithmetic and writes a short string, and it
    // deliberately does nothing at all when the change would be invisible. Redrawing
    // on every single unit would make the operation slower than the work it
    // describes.
    void advance(long long unitsCompleted);
    // Records a new total, for a task whose size was not known at the start. This
    // happens when the size of a file is learned part way through a diagnostic.
    void setTotalUnits(long long totalUnits);
    // Replaces the phrase shown before the bar, for example when the command
    // changes from measuring to acting.
    void setTaskDescription(string taskDescription);
    // Draws the line at a percentage rather than a unit count, for a task whose
    // total is not a meaningful count of things.
    void setFractionComplete(double completedFraction);
    // Records a receiver for the same numbers the bar draws. Empty by default,
    // in which case the bar behaves exactly as it always has. A graphical front
    // end sets one and reads the numbers into its own widgets instead.
    void setSink(ProgressSink sink);
    // Stops the bar, leaving the finished line in place. The destructor calls this,
    // so calling it explicitly is only needed to finish earlier than the object
    // does.
    void finish();
    // True when the bar is currently drawing itself. Used by the tests.
    bool isActive() const;
    // The width of the drawn bar, in characters. It is a constant rather than a
    // constructor argument so that a bar never depends on terminal geometry it
    // cannot verify.
    static constexpr int BAR_WIDTH_IN_CHARACTERS=28;
private:
    // Draws the line for the current state. Does nothing when the change would be
    // invisible or when the output is not a terminal.
    void redraw();
    // Writes the final line, which is the same shape as the bar but with a full
    // block and no trailing whitespace.
    void writeFinalLine();
    string taskDescription;
    long long totalUnits=0;
    long long completedUnits=0;
    long double startedAtNanoseconds=0;
    // The last percentage that was drawn. Redrawing the same percentage produces
    // no visible change, so it is skipped.
    int lastDrawnPercentage=-1;
    // The line number to print, used to decide when a plain line is worth writing
    // when the output is a file rather than a terminal.
    int plainLineCounter=0;
    bool terminalIsAttached=false;
    bool isFinished=false;
    // The receiver of the same numbers, or empty when nobody is listening.
    ProgressSink progressSink;
};
}
#endif
