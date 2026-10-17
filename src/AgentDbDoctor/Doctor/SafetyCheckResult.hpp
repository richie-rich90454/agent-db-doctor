// The SafetyCheckResult value type, which is the answer to one safety question.
//
// The specification requires that every destructive operation be gated by checks
// that either pass or fail, and that a failure stops the operation before
// anything is changed. This is the type those checks return.
#ifndef AGENT_DB_DOCTOR_DOCTOR_SAFETYCHECKRESULT_HPP
#define AGENT_DB_DOCTOR_DOCTOR_SAFETYCHECKRESULT_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Doctor {
// The SafetyCheckResult value type holds the outcome of one check.
//
// The fields are private with reading methods, matching every other value type in
// the codebase. There is no class above it, because the specification forbids
// inheritance.
class SafetyCheckResult final {
public:
    // Builds a passing result with the sentence that says what was confirmed.
    explicit SafetyCheckResult(string explanationOfPass)
        : passedFlag(true), explanationText(std::move(explanationOfPass)) {
    }
    // Builds a failing result. The sentence must say what is wrong and what the
    // user can do about it, because the specification requires every error to
    // suggest a next step.
    static SafetyCheckResult failed(string explanationOfFailure) {
        return SafetyCheckResult(false, std::move(explanationOfFailure));
    }
    // True when the check passed and the operation may continue.
    bool passed() const {
        return passedFlag;
    }
    // The sentence explaining the outcome, whether the check passed or failed.
    const string& explanation() const {
        return explanationText;
    }
    // A complete sentence beginning with either "It is safe" or "It is not safe",
    // built from the stored explanation so the report layer does not have to
    // invent phrasing.
    string describe() const {
        // The conditional operator picks one of two string literals and then
        // appends the stored explanation. Both branches are the same type, which
        // is what makes the expression well formed.
        return string(passedFlag ? "It is safe to continue. " : "It is not safe to continue. ")
            + explanationText;
    }
private:
    SafetyCheckResult(bool passed, string explanation)
        : passedFlag(passed), explanationText(std::move(explanation)) {
    }
    bool passedFlag=false;
    string explanationText;
};
}
#endif
