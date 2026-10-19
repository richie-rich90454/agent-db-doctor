// The HealthScore class, which turns a whole diagnosis into one number.
//
// One number is worth having because it is the only form of the diagnostic
// result that can be compared: between two databases, and between the same
// database last month and now. A number nobody can explain is a number nobody
// should trust, so the whole formula is stated here. The score starts at 100 and
// subtracts, and every subtraction maps to something the report already says in
// words:
//
// - A database whose integrity status is damaged, unreadable, not a database at
//   all, or open with a problem reported loses 60 points. Those are the states
//   in which nothing else in the report can be relied on, so the score has to
//   say so loudly. A check that was not run costs nothing: a check that was
//   skipped is not evidence of damage, and the report says in words that it was
//   skipped.
// - Each warning costs 5 points, and all warnings together cost at most 20.
//   Warnings are the things worth knowing that do not stop the tool, so they
//   must not be able to push a sound database to zero on their own.
// - A file with more than a quarter of its pages free loses up to 10 points, in
//   proportion: one quarter free costs nothing, every page free costs the full
//   ten. Free pages are space a vacuum would hand back, so a file that is a
//   quarter empty has a quarter of a problem.
// - Positive growth loses up to 15 points, in proportion to the recorded rate
//   against the reference rate of 1 GB per day. One gigabyte a day is what a
//   heavily used database produces, so it is the rate at which the loss reaches
//   its maximum. Growth at or past the reference costs the full fifteen;
//   shrinking costs nothing.
//
// The result is clamped to 0..100. It is -1, meaning "unknown", when there is
// nothing to score: a path that does not exist and a folder of text files both
// have no health, and describing either as a bad database would be wrong.
#ifndef AGENT_DB_DOCTOR_DOCTOR_HEALTHSCORE_HPP
#define AGENT_DB_DOCTOR_DOCTOR_HEALTHSCORE_HPP
// Provides the DiagnosisReport value type, which is what the score is computed
// from. The dependency points one way only: this class knows the report, and the
// report only carries the finished number.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
namespace AgentDbDoctor::Doctor {
// The HealthScore class computes the 0..100 score described above.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class HealthScore final {
public:
    HealthScore()=delete;
    // The score for one report. `bytesPerDay` is the growth rate measured from
    // the recorded sizes, positive for growth and zero when there is no trend to
    // measure. Every constant of the formula is public so that a reader, a test,
    // and the documentation can all name the same numbers.
    static int scoreFor(const DiagnosisReport& report, double bytesPerDay);
    // The value returned when there is nothing to score.
    static constexpr int UNKNOWN_SCORE=-1;
    // What a damaged, unreadable, or unrecognisable database costs.
    static constexpr int DAMAGE_LOSS=60;
    // What one warning costs.
    static constexpr int WARNING_LOSS=5;
    // The most the warnings together can cost.
    static constexpr int MAXIMUM_WARNING_LOSS=20;
    // The share of the pages that may be free without any loss.
    static constexpr double FREE_PAGE_SHARE_ALLOWED=0.25;
    // The most the free pages can cost.
    static constexpr int MAXIMUM_FREE_PAGE_LOSS=10;
    // The growth rate at which the growth loss reaches its maximum: one gigabyte
    // per day, which is what a heavily used database produces.
    static constexpr double REFERENCE_GROWTH_BYTES_PER_DAY=1073741824.0;
    // The most positive growth can cost.
    static constexpr int MAXIMUM_GROWTH_LOSS=15;
};
}
#endif
