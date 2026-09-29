// The RecoveryRecommendation value type, which is one step of a recovery plan.
//
// The specification is emphatic that a person using this tool may be panicking.
// Every recommendation is therefore a complete object: a title, an explanation in
// plain English, the exact command to run, and an honest statement of whether it
// is safe.
#ifndef AGENT_DB_DOCTOR_RECOVERY_RECOVERYRECOMMENDATION_HPP
#define AGENT_DB_DOCTOR_RECOVERY_RECOVERYRECOMMENDATION_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Recovery {
// The SeverityLevel enumeration says how urgent a recommendation is.
enum class SeverityLevel {
    // Nothing is wrong. The recommendation is an optional improvement.
    INFORMATION,
    // Something will become a problem if it is ignored.
    ATTENTION,
    // Something is already wrong and the user should act soon.
    URGENT,
    // The situation cannot be made worse, but it also cannot be fixed by this
    // tool. The recommendation exists so the user knows the limit and can stop
    // hoping for a one command fix.
    BEYOND_RECOVERY
};
// The RecoveryRecommendation class holds one step of a recovery plan.
//
// The class is `final` because the specification forbids inheritance. Every field
// is private with a reading method, so a plan cannot be half built.
class RecoveryRecommendation final {
public:
    // Builds a recommendation.
    //
    // `titleText` is a short headline, `explanationText` is a paragraph in plain
    // English, `commandText` is the exact command to run or the empty string when
    // there is nothing for the user to type, and `isDestructive` says whether
    // running it could lose data.
    RecoveryRecommendation(
        SeverityLevel severity,
        string titleText,
        string explanationText,
        string commandText,
        bool isDestructive
    );
    // How urgent this step is.
    SeverityLevel severity() const;
    // The short headline.
    const string& title() const;
    // The paragraph explaining what is happening and why.
    const string& explanation() const;
    // The exact command to run, or the empty string when there is nothing to type.
    const string& command() const;
    // True when running this command could lose data, in which case the tool
    // refuses to run it without an explicit confirmation and a safety copy.
    bool isDestructive() const;
    // True when this step is the last one the tool can offer, which is how the
    // report says out loud that the tool has reached the end of what it can do.
    bool isFinalWord() const;
    // A sentence naming the severity, for example "This is urgent." The word is
    // chosen so it can be printed before the title without sounding like an error
    // code.
    string describeSeverity() const;
private:
    SeverityLevel severityLevel=SeverityLevel::INFORMATION;
    string titleText;
    string explanationText;
    string commandText;
    bool destructiveFlag=false;
};
}
#endif
