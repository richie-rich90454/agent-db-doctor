// The implementation of the RecoveryRecommendation class.
//
// The class is small enough that its methods are defined here rather than in a
// separate file, which keeps the value type and its behaviour together.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the RecoveryRecommendation class and the SeverityLevel enumeration.
#include "AgentDbDoctor/Recovery/RecoveryRecommendation.hpp"
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::Recovery {
RecoveryRecommendation::RecoveryRecommendation(
    SeverityLevel severity,
    string titleText,
    string explanationText,
    string commandText,
    bool isDestructive
)
    : severityLevel(severity),
      titleText(move(titleText)),
      explanationText(move(explanationText)),
      commandText(move(commandText)),
      destructiveFlag(isDestructive) {
}
SeverityLevel RecoveryRecommendation::severity() const {
    return severityLevel;
}
const string& RecoveryRecommendation::title() const {
    return titleText;
}
const string& RecoveryRecommendation::explanation() const {
    return explanationText;
}
const string& RecoveryRecommendation::command() const {
    return commandText;
}
bool RecoveryRecommendation::isDestructive() const {
    return destructiveFlag;
}
bool RecoveryRecommendation::isFinalWord() const {
    // The most severe level is the one that means there is nothing more to try, so
    // comparing against it is how a caller recognises the end of a plan.
    return severityLevel==SeverityLevel::BEYOND_RECOVERY;
}
string RecoveryRecommendation::describeSeverity() const {
    switch (severityLevel) {
        case SeverityLevel::INFORMATION: return string("This is worth knowing about.");
        case SeverityLevel::ATTENTION: return string("This needs attention soon.");
        case SeverityLevel::URGENT: return string("This needs your attention now.");
        case SeverityLevel::BEYOND_RECOVERY: return string("This tool cannot fix this one.");
    }
    return string("Here is what was found.");
}
}
