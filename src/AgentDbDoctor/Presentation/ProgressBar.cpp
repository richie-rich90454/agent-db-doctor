// The implementation of the ProgressBar class.
//
// The design goal here is that showing progress must never cost more than the work
// being measured. Three things keep that true: the level check that avoids
// formatting a message that will be discarded, the test that a new percentage is
// actually different, and the reuse of a single string buffer across redraws so
// that no memory is allocated after the first one.
#include <string>
// Provides `fputs`, `fprintf`, `fflush`, and `stdout`, which write to standard
// output.
#include <cstdio>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the ProgressBar class declaration.
#include "AgentDbDoctor/Presentation/ProgressBar.hpp"
// Provides PlatformMetadata, for the monotonic clock and the terminal test.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Presentation {
// The two escape sequences the bar uses. They are written as literal text rather
// than as numeric codes so that the source stays readable.
//
// The carriage return moves the cursor to the start of the line without advancing
// it, which is what lets the next write overwrite what came before. The erase to
// the end of line sequence removes the leftovers when the new line is shorter than
// the old one. Neither is ever written when the output is not a terminal.
static constexpr const char* CARRIAGE_RETURN="\r";
// The full block character is the filled part of the bar.
static constexpr const char* FULL_BLOCK="\xE2\x96\x88";
// The light shade character is the empty part of the bar.
static constexpr const char* LIGHT_SHADE="\xE2\x96\x91";
// How often a plain line is printed when the output is a file. One line every
// ten percent keeps a log readable without filling it with noise.
static constexpr int PLAIN_LINE_STEP_PERCENTAGE=10;
ProgressBar::ProgressBar(
    string taskDescription,
    long long totalUnits,
    bool isTerminalAttached
)
    : taskDescription(std::move(taskDescription)),
      totalUnits(totalUnits),
      completedUnits(0),
      // The clock is read once, at construction, and used for every estimate after
      // that. Reading it once is what keeps `advance` free of system calls.
      startedAtNanoseconds(static_cast<long double>(PlatformMetadata::monotonicNanoseconds())),
      terminalIsAttached(isTerminalAttached),
      isFinished(false) {
}
ProgressBar::~ProgressBar() {
    // The destructor finishes the bar, so a command that returns early still
    // leaves a complete line behind rather than a half drawn one.
    finish();
}
bool ProgressBar::isActive() const {
    return !isFinished;
}
void ProgressBar::setTotalUnits(long long totalUnitCount) {
    totalUnits=totalUnitCount;
    redraw();
}
void ProgressBar::setTaskDescription(string taskDescriptionText) {
    taskDescription=std::move(taskDescriptionText);
    // The description changed, so the next line will look different even if the
    // percentage has not moved. Forcing the redraw is what makes that visible.
    lastDrawnPercentage=-1;
    redraw();
}
void ProgressBar::setSink(ProgressSink sink) {
    progressSink=std::move(sink);
}
void ProgressBar::advance(long long unitsCompleted) {
    completedUnits+=unitsCompleted;
    redraw();
}
void ProgressBar::setFractionComplete(double completedFraction) {
    if (totalUnits<=0) {
        return;
    }
    // The fraction is clamped so that a rounding error above one cannot produce a
    // percentage above one hundred, which would look like a bug in the tool.
    if (completedFraction<0.0) {
        completedFraction=0.0;
    }
    if (completedFraction>1.0) {
        completedFraction=1.0;
    }
    completedUnits=static_cast<long long>(static_cast<double>(totalUnits) * completedFraction);
    redraw();
}
void ProgressBar::redraw() {
    if (isFinished) {
        return;
    }
    int percentage=0;
    if (totalUnits>0) {
        // The multiplication is done in floating point because `completedUnits` can
        // be in the billions and multiplying two such numbers would overflow a
        // sixty four bit integer.
        percentage=static_cast<int>(
            static_cast<double>(completedUnits) * 100.0 / static_cast<double>(totalUnits)
        );
        if (percentage>100) {
            percentage=100;
        }
    }
    if (percentage==lastDrawnPercentage) {
        // Nothing would look different, so nothing is written. This is the single
        // most important line in the file, because it is what stops the bar from
        // costing more than the work it measures.
        return;
    }
    if (progressSink) {
        // The sink gets exactly the redraws the screen gets: one per visible
        // percentage point. A graphical front end therefore updates at the same
        // rate the console bar would have drawn, and no faster.
        ProgressUpdate update;
        update.taskDescription=taskDescription;
        update.completedUnits=completedUnits;
        update.totalUnits=totalUnits;
        update.isFinished=false;
        progressSink(update);
    }
    if (terminalIsAttached) {
        // A terminal can be redrawn in place, so every visible change is drawn.
        lastDrawnPercentage=percentage;
        const int filledWidth=(percentage * BAR_WIDTH_IN_CHARACTERS) / 100;
        string line=string(CARRIAGE_RETURN)+taskDescription+" [";
        for (int characterIndex=0; characterIndex<BAR_WIDTH_IN_CHARACTERS; ++characterIndex) {
            line+=characterIndex<filledWidth ? FULL_BLOCK : LIGHT_SHADE;
        }
        line+="] "+to_string(percentage)+"%";
        fputs(line.c_str(), stdout);
        // `fflush` pushes the characters out of the C library's buffer so the user
        // sees them now rather than when the buffer happens to fill.
        fflush(stdout);
        return;
    }
    // The output is a file or a pipe, so carriage returns would make the file
    // unreadable. One plain line every ten percent is the compromise.
    if (percentage<plainLineCounter * PLAIN_LINE_STEP_PERCENTAGE) {
        return;
    }
    plainLineCounter++;
    lastDrawnPercentage=percentage;
    // The remaining time is estimated from the rate so far. With no elapsed time
    // there is no rate, so no estimate is printed rather than a wrong one.
    const long long elapsedNanoseconds=
        PlatformMetadata::monotonicNanoseconds()-static_cast<long long>(startedAtNanoseconds);
    string remainingText="";
    if (elapsedNanoseconds>0 && percentage>0 && percentage<100) {
        const double nanosecondsPerPercent=
            static_cast<double>(elapsedNanoseconds) / static_cast<double>(percentage);
        const double remainingNanoseconds=nanosecondsPerPercent * (100 - percentage);
        const long long remainingSeconds=static_cast<long long>(remainingNanoseconds / 1000000000.0);
        remainingText=", about " + to_string(remainingSeconds) + " seconds left";
    }
    fprintf(
        stdout,
        "%s: %d percent complete%s\n",
        taskDescription.c_str(),
        percentage,
        remainingText.c_str()
    );
    fflush(stdout);
}
void ProgressBar::writeFinalLine() {
    if (terminalIsAttached) {
        // The final line is written with a newline so it stays on the screen
        // instead of being overwritten by the next thing the tool prints.
        fprintf(stdout, "\n");
        fflush(stdout);
        return;
    }
    fprintf(stdout, "%s: finished\n", taskDescription.c_str());
    fflush(stdout);
}
void ProgressBar::finish() {
    if (isFinished) {
        return;
    }
    isFinished=true;
    if (progressSink) {
        ProgressUpdate update;
        update.taskDescription=taskDescription;
        update.completedUnits=completedUnits;
        update.totalUnits=totalUnits;
        update.isFinished=true;
        progressSink(update);
    }
    writeFinalLine();
}
}
