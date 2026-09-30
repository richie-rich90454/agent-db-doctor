// The RecoveryPlaybook class, which turns a diagnosis into an ordered plan.
//
// The specification's humanist rule is that the tool must be patient and must
// never let a user face a cryptic code alone. This class is where that promise is
// kept: it produces a list of steps, each with a sentence explaining what it does
// and the exact command to run, ordered so that the safest and most likely to
// succeed come first.
#ifndef AGENT_DB_DOCTOR_RECOVERY_RECOVERYPLAYBOOK_HPP
#define AGENT_DB_DOCTOR_RECOVERY_RECOVERYPLAYBOOK_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiagnosisReport value type.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
// Provides the RecoveryRecommendation value type.
#include "AgentDbDoctor/Recovery/RecoveryRecommendation.hpp"
// The report value type lives in the doctor subsystem, so that namespace is
// brought into scope here. The directive sits after the includes because that is
// where the namespace has been declared.
using namespace AgentDbDoctor::Doctor;
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Recovery {
// The RecoveryPlaybook class produces recovery plans from diagnosis reports.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class RecoveryPlaybook final {
public:
    RecoveryPlaybook()=delete;
    // Builds the plan for a database that could not be opened.
    //
    // `sqliteResultCode` is the code from the failed open, and `openMessage` is the
    // sentence the exception produced. The plan is ordered so that the steps which
    // cannot make things worse come first, and the steps that write to the file come
    // last and are marked destructive.
    static vector<RecoveryRecommendation> buildPlanForUnreadableDatabase(
        const string& databaseFilePath,
        int sqliteResultCode,
        const string& openMessage
    );
    // Builds the plan for a database that is damaged but readable.
    static vector<RecoveryRecommendation> buildPlanForDamagedDatabase(
        const DiagnosisReport& report
    );
    // Builds the plan for a healthy database that is simply too large. The
    // suggestion is the prune and compact pair, in that order, because pruning
    // without compacting would leave the file the same size on disk.
    static vector<RecoveryRecommendation> buildPlanForOversizedDatabase(
        const DiagnosisReport& report
    );
    // Builds the plan for a database that is in no trouble at all. The single step
    // says so, because a tool that finds nothing wrong should say nothing wrong.
    static vector<RecoveryRecommendation> buildPlanForHealthyDatabase(
        const DiagnosisReport& report
    );
    // The sentence printed above every plan, chosen so it never sounds like a
    // verdict. It takes the agent name and the path so the user can see at a glance
    // which file is being talked about.
    static string openingSentence(const string& agentName, const string& databaseFilePath);
    // The sentence printed below every plan, which points at the service that
    // would have caught this earlier.
    static string closingSentence();
private:
};
}
#endif
