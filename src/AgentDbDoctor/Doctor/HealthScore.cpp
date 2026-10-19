// The implementation of the HealthScore class.
//
// The arithmetic is one straight line of subtractions, kept in the same order as
// the rules listed in the header so that a reader can check one against the
// other. Nothing here reads a file or asks the operating system anything: the
// score is a pure function of the report and the growth rate, which is what makes
// it cheap enough for the watchdog's diagnostic path and exact enough to test.
// Provides `min` and `clamp`, used to bound each loss at its stated maximum.
#include <algorithm>
// Provides the HealthScore class declaration and the constants of the formula.
#include "AgentDbDoctor/Doctor/HealthScore.hpp"
using namespace std;
namespace AgentDbDoctor::Doctor {
int HealthScore::scoreFor(const DiagnosisReport& report, double bytesPerDay) {
    if (!report.getFileExists() || report.getIsDirectory()) {
        // A file that is not there and a folder of text files have no health to
        // measure. The report layer words the difference; the score only says
        // that there is no number.
        return UNKNOWN_SCORE;
    }
    double score=100.0;
    switch (report.getIntegrityStatus()) {
        case IntegrityStatus::STRUCTURALLY_DAMAGED:
        case IntegrityStatus::UNREADABLE:
        case IntegrityStatus::NOT_A_DATABASE:
        case IntegrityStatus::PROBLEM_REPORTED:
            score-=static_cast<double>(DAMAGE_LOSS);
            break;
        case IntegrityStatus::NOT_CHECKED:
        case IntegrityStatus::IS_A_FOLDER:
        case IntegrityStatus::INTACT:
            // A check that was skipped, and a database with nothing wrong to
            // report, both cost nothing here.
            break;
    }
    // The warnings are capped, so a talkative database cannot be pushed to zero
    // by messages alone: the cap leaves the damage above and the growth rate
    // below as the parts that dominate when they are present.
    const double warningCount=static_cast<double>(report.getWarnings().size());
    const double maximumCountedWarnings=static_cast<double>(MAXIMUM_WARNING_LOSS / WARNING_LOSS);
    score-=static_cast<double>(WARNING_LOSS) * min(warningCount, maximumCountedWarnings);
    const long long pageCount=report.getPageCount();
    if (pageCount>0) {
        const double freePageShare=static_cast<double>(report.getFreePageCount())
            / static_cast<double>(pageCount);
        if (freePageShare>FREE_PAGE_SHARE_ALLOWED) {
            // The share beyond the allowed quarter is measured against the span
            // that is left between a quarter and a completely empty file, so the
            // loss reaches its maximum exactly when every page is free.
            const double shareBeyondTheAllowedQuarter=
                (freePageShare - FREE_PAGE_SHARE_ALLOWED) / (1.0 - FREE_PAGE_SHARE_ALLOWED);
            score-=static_cast<double>(MAXIMUM_FREE_PAGE_LOSS) * min(shareBeyondTheAllowedQuarter, 1.0);
        }
    }
    if (bytesPerDay>0.0) {
        score-=static_cast<double>(MAXIMUM_GROWTH_LOSS)
            * min(bytesPerDay / REFERENCE_GROWTH_BYTES_PER_DAY, 1.0);
    }
    score=clamp(score, 0.0, 100.0);
    // Rounding to the nearest whole point keeps the number short in the report.
    // A half is added before the conversion because converting to an integer
    // truncates, and a score of 96.7 should read as 97 rather than 96.
    return static_cast<int>(score + 0.5);
}
}
