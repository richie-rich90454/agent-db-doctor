// The action screens: what the buttons do and what the dialogues say.
//
// Destructive work is always two steps: a preview that changes nothing, then
// a typed confirmation, then the run. The confirmation is the word `yes` in
// a text box, the same bar as the command line demands, because clicking a
// button is too easy to do by accident. Every run happens on the worker; this
// file only starts requests, polls the numbers, and renders dialogues.
#ifndef AGENT_DB_DOCTOR_GUI_ACTIONSCREENS_HPP
#define AGENT_DB_DOCTOR_GUI_ACTIONSCREENS_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the action runner and its result value.
#include "ActionState.hpp"
// Provides the scanned database the action buttons act on.
#include "GuiState.hpp"
namespace AgentDbDoctor::Gui {
// The dialogue flow for one pending confirmation and one finished result.
// Kept beside the loop rather than in the runner, because it is interface
// state: what is open, what was typed, and what was last shown.
struct ActionUiState final {
    // True while the confirmation dialogue is open.
    bool confirmOpen=false;
    // True while the result dialogue is open.
    bool resultOpen=false;
    // True while the recovery plan dialogue is open.
    bool planOpen=false;
    // What the user typed into the confirmation box.
    std::string confirmText;
    // The request the confirmation would start.
    ActionRequest confirmedRequest;
    // A sentence saying what the confirmation would do.
    std::string confirmSentence;
    // The preview or plan request the last numbers came from, so the run it
    // leads to acts on the same database with the same settings.
    ActionRequest lastRequest;
    // True when `lastRequest` describes something shown on screen.
    bool hasLastRequest=false;
    // The last finished result, for the result dialogue.
    ActionResult lastResult;
    // The prune keep count and fraction inputs.
    int keepSessionsInput=0;
    double maximumFractionInput=0.5;
};
// Draws the activity bar: the current task, a live progress bar, and the
// counts. Always visible, so progress is never somewhere the user has to go
// looking for. When nothing runs it says so in one quiet line.
void renderActivityBar(ActionRunner& runner);
// Draws the action buttons for the selected database: preview, confirm, run.
// Only shown for databases the tool can act on; anything else gets the one
// honest sentence about why there is nothing to click.
void renderDatabaseActions(
    const ScannedDatabase& scanned,
    ActionRunner& runner,
    ActionUiState& ui);
// Draws the service view: one button per action and the last result.
void renderServiceView(ActionRunner& runner, ActionUiState& ui);
// Polls the finished result and opens the right dialogue. Call once per
// frame, before rendering the dialogues below.
void pollActionResult(ActionRunner& runner, ActionUiState& ui);
// Draws the confirmation, result, and plan dialogues. Call once per frame.
void renderActionDialogues(ActionRunner& runner, ActionUiState& ui);
}
#endif
