// The implementation of the HtmlReportWriter class.
//
// The page is built as one string and written once. That keeps the failure mode
// simple: either the whole file is there or nothing is; a half written report would
// be worse than none. The numbers are formatted with the same helpers the console
// output uses, so the page and the terminal never disagree about a size.
// Provides `ofstream`, which writes the page.
#include <fstream>
// Provides `time` and `strftime`, for the generation timestamp.
#include <ctime>
// Provides the HtmlReportWriter class declaration.
#include "AgentDbDoctor/Presentation/HtmlReportWriter.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor::Doctor;
namespace AgentDbDoctor::Presentation {
namespace {
// Escapes the characters HTML cannot carry. Every value from outside this program
// passes through here.
string escapeForHtml(const string& text) {
    string escapedText;
    escapedText.reserve(text.size() + 16);
    for (const char currentCharacter : text) {
        switch (currentCharacter) {
            case '&': escapedText+="&amp;"; break;
            case '<': escapedText+="&lt;"; break;
            case '>': escapedText+="&gt;"; break;
            case '"': escapedText+="&quot;"; break;
            case '\'': escapedText+="&#39;"; break;
            default: escapedText.push_back(currentCharacter); break;
        }
    }
    return escapedText;
}
// The integrity status as the words the console uses, so the page reads the same.
string integrityWords(const DiagnosisReport& report) {
    switch (report.getIntegrityStatus()) {
        case IntegrityStatus::NOT_CHECKED: return string("not checked");
        case IntegrityStatus::IS_A_FOLDER: return string("a folder, not a database");
        case IntegrityStatus::INTACT: return string("every page checked out");
        case IntegrityStatus::NOT_A_DATABASE: return string("not a SQLite database");
        case IntegrityStatus::STRUCTURALLY_DAMAGED: return string("DAMAGED");
        case IntegrityStatus::UNREADABLE: return string("UNREADABLE");
        case IntegrityStatus::PROBLEM_REPORTED: return string("a problem was reported");
    }
    return string("unknown");
}
// The generation timestamp as a readable local date.
string generationTimestamp() {
    const std::time_t currentTime=std::time(nullptr);
    struct tm brokenDownTime;
#if defined(_WIN32)
    localtime_s(&brokenDownTime, &currentTime);
#else
    localtime_r(&currentTime, &brokenDownTime);
#endif
    char timestampBuffer[64]={0};
    strftime(timestampBuffer, sizeof(timestampBuffer), "%Y-%m-%d %H:%M", &brokenDownTime);
    return string(timestampBuffer);
}
}
bool HtmlReportWriter::write(
    const string& reportFilePath,
    const vector<DiagnosisReport>& reports,
    const string& toolVersion
) {
    string page="<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    page+="<title>agent-db-doctor report</title>\n";
    page+="<style>"
        "body{font-family:system-ui,sans-serif;max-width:64rem;margin:2rem auto;padding:0 1rem;"
        "line-height:1.5;color:#1a1a1a}"
        "table{border-collapse:collapse;width:100%;margin:1rem 0}"
        "th,td{text-align:left;padding:0.4rem 0.6rem;border-bottom:1px solid #ddd;font-size:0.95rem}"
        "th{background:#f4f4f5}"
        "code{background:#f4f4f5;padding:0.1rem 0.3rem;border-radius:4px}"
        ".warn{background:#fff8e6;padding:0.5rem 0.75rem;border-radius:6px;margin:0.4rem 0}"
        "h2{margin-top:2rem}"
        "</style>\n</head>\n<body>\n";
    page+="<h1>agent-db-doctor report</h1>\n";
    page+="<p>Generated " + escapeForHtml(generationTimestamp()) + " by agent-db-doctor "
        + escapeForHtml(toolVersion) + ". Nothing in this report was changed to produce it.</p>\n";
    // The summary table first: one row per database, which is what a reader skims.
    page+="<table>\n<tr><th>Agent</th><th>Database</th><th>Size</th><th>Reclaimable</th>"
        "<th>Integrity</th></tr>\n";
    for (const DiagnosisReport& report : reports) {
        page+="<tr><td>" + escapeForHtml(report.getAgentName().empty() ? string("-") : report.getAgentName())
            + "</td><td><code>" + escapeForHtml(report.getDatabaseFilePath()) + "</code></td><td>"
            + escapeForHtml(HumanReadableFormatting::formatBytes(report.getLogicalSizeBytes()))
            + "</td><td>" + escapeForHtml(HumanReadableFormatting::formatBytes(report.getReclaimableBytes()))
            + "</td><td>" + escapeForHtml(integrityWords(report)) + "</td></tr>\n";
    }
    page+="</table>\n";
    // Then the details, one section per database.
    for (const DiagnosisReport& report : reports) {
        page+="<h2>" + escapeForHtml(
            (report.getAgentName().empty() ? string("Database") : report.getAgentName() + " database")
        ) + "</h2>\n";
        page+="<p><code>" + escapeForHtml(report.getDatabaseFilePath()) + "</code></p>\n";
        page+="<ul>\n";
        page+="<li>Size: " + escapeForHtml(HumanReadableFormatting::formatBytes(report.getLogicalSizeBytes())) + "</li>\n";
        if (report.getHasWriteAheadLog()) {
            page+="<li>Write ahead log: "
                + escapeForHtml(HumanReadableFormatting::formatBytes(report.getWriteAheadLogSizeBytes()))
                + "</li>\n";
        }
        if (report.getPageCount()>0) {
            page+="<li>Pages: " + escapeForHtml(HumanReadableFormatting::formatCount(report.getPageCount()))
                + " of " + escapeForHtml(HumanReadableFormatting::formatCount(report.getPageSizeBytes()))
                + " bytes, " + escapeForHtml(HumanReadableFormatting::formatCount(report.getFreePageCount()))
                + " unused</li>\n";
        }
        if (!report.getJournalMode().empty()) {
            page+="<li>Journal: <code>" + escapeForHtml(report.getJournalMode()) + "</code></li>\n";
        }
        if (!report.getSchemaGenerationDescription().empty()) {
            page+="<li>Schema: " + escapeForHtml(report.getSchemaGenerationDescription()) + "</li>\n";
        }
        for (const string& tableName : report.countedTableNames()) {
            page+="<li>Rows in " + escapeForHtml(tableName) + ": "
                + escapeForHtml(HumanReadableFormatting::formatCount(report.rowCount(tableName)))
                + (report.isRowCountApproximate(tableName) ? " (approximate)" : "") + "</li>\n";
        }
        if (!report.getRedundantEventTypes().empty()) {
            page+="<li>Older copies in the event log: "
                + escapeForHtml(HumanReadableFormatting::formatCount(report.getRedundantEventRowCount()))
                + "</li>\n";
        }
        page+="<li>Integrity: " + escapeForHtml(integrityWords(report));
        if (!report.getIntegrityMessage().empty() && report.getIntegrityStatus()!=IntegrityStatus::INTACT) {
            page+=" - " + escapeForHtml(report.getIntegrityMessage());
        }
        page+="</li>\n</ul>\n";
        for (const string& warning : report.getWarnings()) {
            page+="<p class=\"warn\">" + escapeForHtml(warning) + "</p>\n";
        }
        if (!report.getSuggestedNextSteps().empty()) {
            page+="<ul>\n";
            for (const string& nextStep : report.getSuggestedNextSteps()) {
                page+="<li>" + escapeForHtml(nextStep) + "</li>\n";
            }
            page+="</ul>\n";
        }
    }
    page+="</body>\n</html>\n";
    std::ofstream reportFile(reportFilePath, std::ios::trunc | std::ios::binary);
    if (!reportFile.is_open()) {
        return false;
    }
    reportFile<<page;
    return reportFile.good();
}
}
