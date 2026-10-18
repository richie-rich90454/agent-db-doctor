// The implementation of the HistoryCommand class.
//
// The arithmetic is one straight line: the change across the recorded window
// divided by the days in it. That is stated in the output rather than dressed up,
// because a trend measured over three days is a much weaker claim than one measured
// over thirty, and the number of recorded points is printed beside the forecast so
// a reader can judge it.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides `table`, used to convert seconds to a date.
#include <ctime>
// Provides the HistoryCommand class declaration.
#include "AgentDbDoctor/Command/HistoryCommand.hpp"
// Provides the recorded sizes.
#include "AgentDbDoctor/Service/HistoryStore.hpp"
// Provides PlatformMetadata, for the disk space behind the forecast.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Service;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
namespace {
// The default window, in days, when the caller does not name one.
constexpr int DEFAULT_WINDOW_DAYS=30;
}
HistoryCommand::HistoryCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string HistoryCommand::describe() const {
    return string(
        "Report how the databases have grown, from the sizes the background watchdog records once "
        "an hour, with a straight line forecast of when the drive would fill."
    );
}
string HistoryCommand::commandName() const {
    return string("history");
}
bool HistoryCommand::reportOneDatabase(const string& databaseFilePath, int windowDays) {
    const vector<HistoryPoint> recordedPoints=HistoryStore::pointsFor(databaseFilePath, 0);
    if (recordedPoints.empty()) {
        if (reporter.isMachineReadable()) {
            reporter.writeMachineObject(
                string("{\"event\":\"history\",\"path\":\"")
                + ConsoleReport::escapeForJson(databaseFilePath)
                + "\",\"points\":0,\"windowDays\":" + to_string(windowDays) + "}"
            );
            return false;
        }
        reporter.writeLine(databaseFilePath);
        reporter.writeDetailLine(
            "Nothing has been recorded for this database. The background service records a size "
            "once an hour, so this fills in after it has run for a few hours."
        );
        return false;
    }
    // The window is applied to the recorded points, then the first and last of what
    // remains are the ends of the line.
    const long long nowSeconds=static_cast<long long>(std::time(nullptr));
    const long long oldestKeptSecond=nowSeconds - (static_cast<long long>(windowDays) * 24 * 60 * 60);
    vector<HistoryPoint> windowPoints;
    for (const HistoryPoint& point : recordedPoints) {
        if (point.epochSeconds>=oldestKeptSecond) {
            windowPoints.push_back(point);
        }
    }
    // A single point inside the window is not a trend, so the oldest recorded point
    // is used as the start instead. That is what makes the report useful within the
    // first day the service runs.
    if (windowPoints.size()<2) {
        windowPoints=recordedPoints;
    }
    const HistoryPoint firstPoint=windowPoints.front();
    const HistoryPoint lastPoint=windowPoints.back();
    const long long elapsedSeconds=lastPoint.epochSeconds-firstPoint.epochSeconds;
    const long long changeBytes=lastPoint.sizeBytes-firstPoint.sizeBytes;
    const double elapsedDays=elapsedSeconds>0
        ? static_cast<double>(elapsedSeconds) / 86400.0
        : 0.0;
    const double bytesPerDay=elapsedDays>0.0
        ? static_cast<double>(changeBytes) / elapsedDays
        : 0.0;
    const DiskSpace availableSpace=PlatformMetadata::readDiskSpace(databaseFilePath);
    // The forecast is only a number when the database is growing and the drive can
    // be read. Everything else is answered with the reason rather than with a
    // made up figure.
    long long daysUntilDriveFull=-1;
    if (bytesPerDay>0.0 && availableSpace.isValid) {
        daysUntilDriveFull=static_cast<long long>(
            static_cast<double>(availableSpace.availableBytes) / bytesPerDay
        );
    }
    if (reporter.isMachineReadable()) {
        string jsonText="{\"event\":\"history\"";
        jsonText+=",\"path\":\"" + ConsoleReport::escapeForJson(databaseFilePath) + "\"";
        jsonText+=",\"points\":" + to_string(recordedPoints.size());
        jsonText+=",\"windowDays\":" + to_string(windowDays);
        jsonText+=",\"firstEpochSeconds\":" + to_string(firstPoint.epochSeconds);
        jsonText+=",\"firstBytes\":" + to_string(firstPoint.sizeBytes);
        jsonText+=",\"lastEpochSeconds\":" + to_string(lastPoint.epochSeconds);
        jsonText+=",\"lastBytes\":" + to_string(lastPoint.sizeBytes);
        jsonText+=",\"changeBytes\":" + to_string(changeBytes);
        jsonText+=",\"bytesPerDay\":" + to_string(static_cast<long long>(bytesPerDay));
        jsonText+=",\"freeBytes\":" + to_string(availableSpace.availableBytes);
        jsonText+=",\"daysUntilDriveFull\":" + to_string(daysUntilDriveFull)+"}";
        reporter.writeMachineObject(jsonText);
        return true;
    }
    reporter.writeLine(databaseFilePath);
    reporter.writeDetailLine(
        to_string(recordedPoints.size()) + " sizes recorded over the last "
        + to_string(windowDays) + " days, unless there are fewer."
    );
    reporter.writeDetailLine(
        "Then: " + HumanReadableFormatting::formatBytes(firstPoint.sizeBytes)
        + " on " + HumanReadableFormatting::formatTimestamp(firstPoint.epochSeconds * 1000)
    );
    reporter.writeDetailLine(
        "Now:  " + HumanReadableFormatting::formatBytes(lastPoint.sizeBytes)
        + " on " + HumanReadableFormatting::formatTimestamp(lastPoint.epochSeconds * 1000)
    );
    if (changeBytes==0) {
        reporter.writeDetailLine("The size has not changed. That is the good answer.");
        return true;
    }
    const string direction=changeBytes>0 ? "Grew" : "Shrank";
    const long long absoluteChange=changeBytes>0 ? changeBytes : -changeBytes;
    reporter.writeDetailLine(
        direction + ": " + (changeBytes>0 ? "+" : "-")
        + HumanReadableFormatting::formatBytes(absoluteChange)
        + (elapsedSeconds>0
            ? " over about " + to_string(static_cast<long long>(elapsedDays + 0.5))
                + (elapsedDays<1.5 ? " day." : " days.")
            : string(" over an unknown span."))
    );
    if (bytesPerDay>0.0) {
        reporter.writeDetailLine(
            string("At this rate: about ")
            + HumanReadableFormatting::formatBytes(static_cast<long long>(bytesPerDay))
            + " per day."
        );
        if (daysUntilDriveFull>=0) {
            reporter.writeDetailLine(
                string("The drive has ") + HumanReadableFormatting::formatBytes(availableSpace.availableBytes)
                + " free, which is about " + to_string(daysUntilDriveFull)
                + (daysUntilDriveFull==1 ? " day" : " days") + " at this rate."
            );
        }
    } else if (bytesPerDay<0.0) {
        reporter.writeDetailLine("At this rate the database is shrinking, not growing.");
    }
    return true;
}
int HistoryCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    const int windowDays=options.historyDaysToShow>0 ? options.historyDaysToShow : DEFAULT_WINDOW_DAYS;
    vector<string> databasePaths;
    if (!options.databaseFilePath.empty()) {
        databasePaths.push_back(options.databaseFilePath);
    } else {
        databasePaths=HistoryStore::knownDatabasePaths();
    }
    if (databasePaths.empty()) {
        reporter.writeLine(
            "No sizes have been recorded yet. The background service records one size per database "
            "per hour; install it with: agent-db-doctor service install"
        );
        return 0;
    }
    bool reportedAnything=false;
    for (const string& databaseFilePath : databasePaths) {
        if (reportOneDatabase(databaseFilePath, windowDays)) {
            reportedAnything=true;
        }
        if (!reporter.isMachineReadable()) {
            reporter.writeLine("");
        }
    }
    if (!reportedAnything && !reporter.isMachineReadable()) {
        reporter.writeSuggestedNextSteps(vector<string>{
            string("Record the first sizes by running the background service: agent-db-doctor service install"),
            string("Or record one by hand, once, for a single database.")
        });
    }
    return 0;
}
}
