// The implementation of the HumanReadableFormatting class.
//
// The number to text conversions are written by hand rather than with a formatting
// library, for two reasons. The specification permits `fmt` only on cold paths,
// and these functions are used in the report renderer, which is a cold path but
// one where a locale-aware formatter would produce a comma in some regions and a
// full stop in others. A file size written "1,024" is confusing to a reader who
// expects "1.024".
#include <cstdio>
// Provides `snprintf`, which writes formatted text into a fixed size buffer.
#include <cstdio>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `reverse`, the standard library function that reverses a sequence in
// place.
#include <algorithm>
// Provides `strftime`, `localtime_r`, and `struct tm`, which turn a count of
// seconds since 1970 into a readable date.
#include <ctime>
// Provides the HumanReadableFormatting class declaration.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::Presentation {
// The binary size units. A kilobyte in this tool is 1024 bytes, not 1000,
// because that is the unit a file system allocates in and the unit a file browser
// shows. The suffixes are spelled out because a report that is read aloud should
// not need the reader to know the abbreviations.
static constexpr long long KILOBYTE=1024LL;
static constexpr long long MEGABYTE=KILOBYTE * 1024LL;
static constexpr long long GIGABYTE=MEGABYTE * 1024LL;
static constexpr long long TERABYTE=GIGABYTE * 1024LL;
string HumanReadableFormatting::withThousandsSeparators(const string& digits) {
    // The string is built backwards and then reversed, because inserting a
    // separator while walking forwards requires knowing how many digits come
    // after the current one, which is not known until the end.
    string reversedResult;
    reversedResult.reserve(digits.size() + digits.size() / 3);
    size_t digitsAfterCurrent=0;
    for (size_t digitIndex=digits.size(); digitIndex>0; --digitIndex) {
        if (digitsAfterCurrent>0 && digitsAfterCurrent % 3 == 0) {
            reversedResult.push_back(',');
        }
        reversedResult.push_back(digits[digitIndex - 1]);
        digitsAfterCurrent++;
    }
    // The standard `reverse` works on any sequence with a random access iterator,
    // which a `string` is.
    reverse(reversedResult.begin(), reversedResult.end());
    return reversedResult;
}
string HumanReadableFormatting::formatBytes(long long byteCount) {
    // A negative byte count should never happen, but if one arrives, showing it as
    // zero is less confusing than showing a negative amount of storage.
    if (byteCount<0) {
        return string("0 bytes");
    }
    if (byteCount<TERABYTE) {
        // Below a terabyte the value is shown with one decimal place, which is
        // enough to tell a user that a file grew without being precise about
        // bytes they cannot act on anyway.
        const double value=static_cast<double>(byteCount) / static_cast<double>(GIGABYTE);
        if (byteCount>=GIGABYTE) {
            char buffer[64];
            // `snprintf` writes at most the size given, so it cannot overrun. It
            // returns the number of characters that would have been written, which
            // is why the result is checked before it is used.
            const int written=snprintf(buffer, sizeof(buffer), "%.1f GB", value);
            if (written>0 && static_cast<size_t>(written)<sizeof(buffer)) {
                return string(buffer, static_cast<size_t>(written));
            }
        }
        if (byteCount>=MEGABYTE) {
            const double megabyteValue=static_cast<double>(byteCount) / static_cast<double>(MEGABYTE);
            char buffer[64];
            const int written=snprintf(buffer, sizeof(buffer), "%.1f MB", megabyteValue);
            if (written>0 && static_cast<size_t>(written)<sizeof(buffer)) {
                return string(buffer, static_cast<size_t>(written));
            }
        }
        if (byteCount>=KILOBYTE) {
            const double kilobyteValue=static_cast<double>(byteCount) / static_cast<double>(KILOBYTE);
            char buffer[64];
            const int written=snprintf(buffer, sizeof(buffer), "%.1f KB", kilobyteValue);
            if (written>0 && static_cast<size_t>(written)<sizeof(buffer)) {
                return string(buffer, static_cast<size_t>(written));
            }
        }
    }
    return formatCount(byteCount) + " bytes";
}
string HumanReadableFormatting::formatBytesPrecisely(long long byteCount) {
    if (byteCount<TERABYTE) {
        const double value=static_cast<double>(byteCount) / static_cast<double>(GIGABYTE);
        char buffer[64];
        const int written=snprintf(buffer, sizeof(buffer), "%.2f GB", value);
        if (written>0 && static_cast<size_t>(written)<sizeof(buffer)) {
            return string(buffer, static_cast<size_t>(written));
        }
    }
    return formatCount(byteCount) + " bytes";
}
string HumanReadableFormatting::formatCount(long long count) {
    const string digits=to_string(count);
    const bool isNegative=!digits.empty() && digits.front()=='-';
    // The separator logic works on the digits alone, so the sign is removed first
    // and added back afterwards. Inserting a separator around a minus sign would
    // produce "-1,234" written as "-,1234", which is wrong.
    const string magnitudeDigits=isNegative ? digits.substr(1) : digits;
    const string separated=withThousandsSeparators(magnitudeDigits);
    return isNegative ? "-" + separated : separated;
}
string HumanReadableFormatting::formatDuration(long long nanoseconds) {
    if (nanoseconds<0) {
        return string("an unknown amount of time");
    }
    // The unit is chosen so the number lands between one and a thousand, which is
    // what makes the value easy to compare between two runs.
    constexpr long long NANOSECONDS_PER_MICROSECOND=1000LL;
    constexpr long long NANOSECONDS_PER_MILLISECOND=1000000LL;
    constexpr long long NANOSECONDS_PER_SECOND=1000000000LL;
    constexpr long long NANOSECONDS_PER_MINUTE=60LL * NANOSECONDS_PER_SECOND;
    if (nanoseconds < NANOSECONDS_PER_MICROSECOND) {
        return to_string(nanoseconds) + " nanoseconds";
    }
    if (nanoseconds < NANOSECONDS_PER_MILLISECOND) {
        return to_string(nanoseconds / NANOSECONDS_PER_MICROSECOND) + " microseconds";
    }
    if (nanoseconds < NANOSECONDS_PER_SECOND) {
        return to_string(nanoseconds / NANOSECONDS_PER_MILLISECOND) + " milliseconds";
    }
    if (nanoseconds < NANOSECONDS_PER_MINUTE) {
        char buffer[64];
        const int written=snprintf(
            buffer,
            sizeof(buffer),
            "%.1f seconds",
            static_cast<double>(nanoseconds) / static_cast<double>(NANOSECONDS_PER_SECOND)
        );
        if (written>0 && static_cast<size_t>(written)<sizeof(buffer)) {
            return string(buffer, static_cast<size_t>(written));
        }
    }
    return to_string(nanoseconds / NANOSECONDS_PER_MINUTE) + " minutes";
}
string HumanReadableFormatting::formatPercentage(double fraction, int decimalPlaces) {
    if (fraction<0.0) {
        fraction=0.0;
    }
    if (fraction>1.0) {
        fraction=1.0;
    }
    // The precision is clamped rather than trusted, because a caller asking for
    // twelve decimal places would produce a number nobody can read and a buffer
    // that might not be large enough.
    if (decimalPlaces<0) {
        decimalPlaces=0;
    }
    if (decimalPlaces>6) {
        decimalPlaces=6;
    }
    char buffer[64];
    // The dynamic precision is written as `.*`, which means "take the number of
    // decimal places from the next integer argument". That is the only way to
    // build a format string with a variable precision at run time.
    const int written=snprintf(buffer, sizeof(buffer), "%.*f%%", decimalPlaces, fraction * 100.0);
    if (written>0 && static_cast<size_t>(written)<sizeof(buffer)) {
        return string(buffer, static_cast<size_t>(written));
    }
    return string("0%");
}
string HumanReadableFormatting::formatTimestamp(long long epochMilliseconds) {
    // Seconds since the epoch, which is what the C library time functions expect.
    const long long epochSeconds=epochMilliseconds / 1000LL;
    // `time_t` is the C library's type for a point in time. The cast is explicit
    // because the two types are signed integers of different widths.
    const time_t calendarTime=static_cast<time_t>(epochSeconds);
    // `struct tm` is the broken down calendar form: year, month, day, hour, and so
    // on. `localtime_r` fills it in and, unlike `localtime`, is safe to call
    // because it writes into storage the caller owns rather than into a shared
    // static one.
    struct tm brokenDownTime;
#if defined(_WIN32)
    // Windows spells the thread safe conversion `localtime_s` and takes its
    // arguments in the opposite order to the Unix version. Both return zero on
    // success, which is the test used below.
    if (localtime_s(&brokenDownTime, &calendarTime)!=0) {
        return string("an unknown time");
    }
#else
    // `localtime_r` writes into storage the caller owns rather than into a shared
    // static one, and returns a null pointer on failure.
    if (localtime_r(&calendarTime, &brokenDownTime)==nullptr) {
        return string("an unknown time");
    }
#endif
    char buffer[64];
    // The format string uses the strftime conventions, where each `%` introduces a
    // field. The year, month, day, hour, minute, and second are the six a user
    // needs to place an event in time.
    const size_t written=strftime(
        buffer,
        sizeof(buffer),
        "%Y-%m-%d %H:%M:%S",
        &brokenDownTime
    );
    if (written==0) {
        return string("an unknown time");
    }
    return string(buffer, written);
}
string HumanReadableFormatting::formatRelativeTime(
    long long epochMilliseconds,
    long long nowEpochMilliseconds
) {
    const long long differenceMilliseconds=nowEpochMilliseconds-epochMilliseconds;
    if (differenceMilliseconds<0) {
        return string("in the future");
    }
    constexpr long long MILLISECONDS_PER_SECOND=1000LL;
    constexpr long long MILLISECONDS_PER_MINUTE=60LL * MILLISECONDS_PER_SECOND;
    constexpr long long MILLISECONDS_PER_HOUR=60LL * MILLISECONDS_PER_MINUTE;
    constexpr long long MILLISECONDS_PER_DAY=24LL * MILLISECONDS_PER_HOUR;
    constexpr long long MILLISECONDS_PER_WEEK=7LL * MILLISECONDS_PER_DAY;
    // Each unit is chosen only once the difference exceeds the next one up, so the
    // value is always between one and about sixty of the chosen unit.
    if (differenceMilliseconds < MILLISECONDS_PER_MINUTE) {
        return to_string(differenceMilliseconds / MILLISECONDS_PER_SECOND) + " seconds ago";
    }
    if (differenceMilliseconds < MILLISECONDS_PER_HOUR) {
        return to_string(differenceMilliseconds / MILLISECONDS_PER_MINUTE) + " minutes ago";
    }
    if (differenceMilliseconds < MILLISECONDS_PER_DAY) {
        return to_string(differenceMilliseconds / MILLISECONDS_PER_HOUR) + " hours ago";
    }
    if (differenceMilliseconds < MILLISECONDS_PER_WEEK) {
        return to_string(differenceMilliseconds / MILLISECONDS_PER_DAY) + " days ago";
    }
    if (differenceMilliseconds < 30LL * MILLISECONDS_PER_DAY) {
        return to_string(differenceMilliseconds / MILLISECONDS_PER_WEEK) + " weeks ago";
    }
    if (differenceMilliseconds < 365LL * MILLISECONDS_PER_DAY) {
        return to_string(differenceMilliseconds / (30LL * MILLISECONDS_PER_DAY)) + " months ago";
    }
    return to_string(differenceMilliseconds / (365LL * MILLISECONDS_PER_DAY)) + " years ago";
}
string HumanReadableFormatting::describeShareOfWhole(long long partBytes, long long wholeBytes) {
    if (wholeBytes<=0) {
        return string("the whole file");
    }
    if (partBytes<=0) {
        return string("none of it");
    }
    if (partBytes>=wholeBytes) {
        return string("all of it");
    }
    return formatPercentage(
        static_cast<double>(partBytes) / static_cast<double>(wholeBytes),
        1
    ) + " of it";
}
}
