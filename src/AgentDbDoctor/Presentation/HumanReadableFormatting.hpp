// The HumanReadableFormatting class, which turns numbers into text a person reads
// without effort.
//
// The specification requires every user facing string to be routed through a
// translation table, and requires sizes, durations, and error codes never to
// appear as bare numbers. This class is that table for numbers. It also holds the
// message catalogue, so that the whole tool speaks through one place.
#ifndef AGENT_DB_DOCTOR_PRESENTATION_HUMANREADABLEFORMATTING_HPP
#define AGENT_DB_DOCTOR_PRESENTATION_HUMANREADABLEFORMATTING_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// The outer namespace is declared before the nested spelling below, because
// `namespace Outer::Inner` only compiles when `Outer` already exists. Without
// this line a header that is a translation unit's first include would fail.
namespace AgentDbDoctor {
}

using namespace std;
namespace AgentDbDoctor::Presentation {
// The HumanReadableFormatting class converts values into sentences.
//
// The class is `final` because the specification forbids inheritance. It holds no
// state, so every method is `static`.
class HumanReadableFormatting final {
public:
    HumanReadableFormatting()=delete;
    // Formats a byte count the way a person would say it, for example
    // "12.4 GB". Binary units are used, because that is what a file system reports
    // and what a user will recognise from a file browser. The unit is spelled out
    // rather than abbreviated, so the text is readable when spoken aloud.
    static string formatBytes(long long byteCount);
    // Formats a byte count with two decimal places, for the places where precision
    // matters, such as a progress figure.
    static string formatBytesPrecisely(long long byteCount);
    // Formats a duration in nanoseconds, for example "1.4 seconds" or "380
    // milliseconds". The unit is chosen so the number is always between one and a
    // thousand, which is what makes it easy to read at a glance.
    static string formatDuration(long long nanoseconds);
    // Formats a count of rows with a thousands separator, so a large number is
    // readable rather than being an unbroken run of digits.
    static string formatCount(long long count);
    // Formats a proportion between zero and one as a percentage.
    static string formatPercentage(double fraction, int decimalPlaces);
    // Formats a timestamp given as milliseconds since 1970, in the local time
    // zone, for example "2026-09-29 14:05". The conversion uses the C library's
    // time functions rather than any date library, because the specification
    // permits no others.
    static string formatTimestamp(long long epochMilliseconds);
    // Formats a timestamp as how long ago it was, for example "3 weeks ago". The
    // largest sensible unit is chosen automatically.
    static string formatRelativeTime(long long epochMilliseconds, long long nowEpochMilliseconds);
    // Inserts a thousands separator into a digit string.
    static string withThousandsSeparators(const string& digits);
    // A sentence saying what a percentage of a whole means in plain terms.
    static string describeShareOfWhole(long long partBytes, long long wholeBytes);
private:
};
}
#endif
