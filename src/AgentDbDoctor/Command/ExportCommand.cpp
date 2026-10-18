// The implementation of the ExportCommand class.
//
// The parts are read with `json_extract` inside SQL so that the parsing stays in
// SQLite, where it is fast and needs no JSON library. The three output formats share
// one reading pass: a small value type holds the session, its messages, and their
// parts, and the three writers turn that into Markdown, JSON, or HTML. The column
// names are discovered rather than assumed, because the session table of an agent
// that never grew a title column is still a session table.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
#include <vector>
// Provides `ofstream`, which writes the exported document.
#include <fstream>
// Provides `ctime`, for the export timestamp.
#include <ctime>
// Provides the ExportCommand class declaration.
#include "AgentDbDoctor/Command/ExportCommand.hpp"
// Provides PlatformMetadata, for the file path of the output and the clock.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the Database wrapper and the Statement class.
#include "AgentDbDoctor/Persistence/Database.hpp"
// Provides StatisticsReader, for the column checks.
#include "AgentDbDoctor/Doctor/StatisticsReader.hpp"
// Provides the number to text conversions.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::FileSystem;
using namespace AgentDbDoctor::Persistence;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Command {
namespace {
// One piece of one message.
struct ExportedPart final {
    // The kind of part: text, tool, or something else the agent invented.
    string kind;
    // The text, for a text part.
    string text;
    // The tool name, for a tool part.
    string toolName;
};
// One turn of the conversation.
struct ExportedMessage final {
    string messageIdentifier;
    string role;
    long long epochSeconds=0;
    vector<ExportedPart> parts;
};
// The whole conversation.
struct ExportedSession final {
    string sessionIdentifier;
    string title;
    long long epochSeconds=0;
    vector<ExportedMessage> messages;
};
// Escapes the five characters HTML cannot carry, so a conversation cannot inject
// markup into the exported page.
string escapeForHtml(const string& text) {
    string escapedText;
    escapedText.reserve(text.size() + 16);
    for (const char currentCharacter : text) {
        switch (currentCharacter) {
            case '&': escapedText+="&amp;"; break;
            case '<': escapedText+="&lt;"; break;
            case '>': escapedText+="&gt;"; break;
            case '"': escapedText+="&quot;"; break;
            default: escapedText.push_back(currentCharacter); break;
        }
    }
    return escapedText;
}
// Reads one session out of the database. Returns false when the session is not
// there, which is a normal answer for a mistyped identifier.
bool readSession(Database& database, const string& sessionIdentifier, ExportedSession& sessionOut) {
    const bool hasTitleColumn=StatisticsReader::columnExists(database, "session", "title");
    const bool hasTimeColumn=StatisticsReader::columnExists(database, "session", "time_created");
    string sessionQuery="SELECT id";
    if (hasTitleColumn) {
        sessionQuery+=", title";
    }
    if (hasTimeColumn) {
        sessionQuery+=", time_created";
    }
    sessionQuery+=" FROM session WHERE id=?1";
    Statement sessionStatement(database.rawHandle(), sessionQuery, "the session being exported");
    if (!sessionStatement.isValid()) {
        return false;
    }
    sessionStatement.bindText(1, sessionIdentifier);
    if (!sessionStatement.step()) {
        return false;
    }
    sessionOut.sessionIdentifier=sessionStatement.columnText(0);
    if (hasTitleColumn) {
        sessionOut.title=sessionStatement.columnText(1);
    }
    if (hasTimeColumn) {
        sessionOut.epochSeconds=sessionStatement.columnInteger(hasTitleColumn ? 2 : 1);
    }
    if (sessionOut.title.empty()) {
        sessionOut.title=sessionOut.sessionIdentifier;
    }
    const bool messageHasTimeColumn=StatisticsReader::columnExists(database, "message", "time_created");
    const string messageQuery=messageHasTimeColumn
        ? string("SELECT id, role, time_created FROM message WHERE session_id=?1 ORDER BY time_created, rowid")
        : string("SELECT id, role FROM message WHERE session_id=?1 ORDER BY rowid");
    Statement messageStatement(database.rawHandle(), messageQuery, "the messages being exported");
    if (!messageStatement.isValid()) {
        return false;
    }
    messageStatement.bindText(1, sessionIdentifier);
    while (messageStatement.step()) {
        ExportedMessage message;
        message.messageIdentifier=messageStatement.columnText(0);
        message.role=messageStatement.columnText(1);
        if (messageHasTimeColumn) {
            message.epochSeconds=messageStatement.columnInteger(2);
        }
        Statement partStatement(
            database.rawHandle(),
            "SELECT json_extract(data, '$.type'), json_extract(data, '$.text'), "
            "json_extract(data, '$.tool') FROM part WHERE message_id=?1 ORDER BY rowid",
            "the parts being exported"
        );
        if (partStatement.isValid()) {
            partStatement.bindText(1, message.messageIdentifier);
            while (partStatement.step()) {
                ExportedPart part;
                part.kind=partStatement.columnText(0);
                part.text=partStatement.columnText(1);
                part.toolName=partStatement.columnText(2);
                message.parts.push_back(std::move(part));
            }
        }
        sessionOut.messages.push_back(std::move(message));
    }
    return true;
}
// Renders the session as Markdown.
string renderMarkdown(const ExportedSession& session, const string& agentName) {
    string text="# " + session.title + "\n\n";
    text+="Session " + session.sessionIdentifier + ".";
    if (!agentName.empty()) {
        text+=" Exported from " + agentName + ".";
    }
    text+="\n\n";
    for (const ExportedMessage& message : session.messages) {
        text+="## " + (message.role.empty() ? string("message") : message.role);
        if (message.epochSeconds>0) {
            text+=" - " + HumanReadableFormatting::formatTimestamp(message.epochSeconds * 1000);
        }
        text+="\n\n";
        for (const ExportedPart& part : message.parts) {
            if (part.kind=="text" && !part.text.empty()) {
                text+=part.text + "\n\n";
            } else if (part.kind=="tool" && !part.toolName.empty()) {
                text+="[tool: " + part.toolName + "]\n\n";
            }
        }
    }
    return text;
}
// Renders the session as one JSON document. The escaping rule is the same one the
// machine readable output uses.
string renderJson(const ExportedSession& session) {
    string text="{\"session\":{\"id\":\"" + ConsoleReport::escapeForJson(session.sessionIdentifier)
        + "\",\"title\":\"" + ConsoleReport::escapeForJson(session.title)
        + "\",\"timeCreated\":" + to_string(session.epochSeconds) + "},\"messages\":[";
    for (size_t messageIndex=0; messageIndex<session.messages.size(); ++messageIndex) {
        const ExportedMessage& message=session.messages[messageIndex];
        if (messageIndex>0) {
            text+=",";
        }
        text+="{\"id\":\"" + ConsoleReport::escapeForJson(message.messageIdentifier)
            + "\",\"role\":\"" + ConsoleReport::escapeForJson(message.role)
            + "\",\"timeCreated\":" + to_string(message.epochSeconds) + ",\"parts\":[";
        for (size_t partIndex=0; partIndex<message.parts.size(); ++partIndex) {
            const ExportedPart& part=message.parts[partIndex];
            if (partIndex>0) {
                text+=",";
            }
            text+="{\"kind\":\"" + ConsoleReport::escapeForJson(part.kind)
                + "\",\"text\":\"" + ConsoleReport::escapeForJson(part.text)
                + "\",\"tool\":\"" + ConsoleReport::escapeForJson(part.toolName) + "\"}";
        }
        text+="]}";
    }
    text+="]}\n";
    return text;
}
// Renders the session as a small standalone page.
string renderHtml(const ExportedSession& session, const string& agentName) {
    string text="<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    text+="<title>" + escapeForHtml(session.title) + "</title>\n";
    text+="<style>body{font-family:system-ui,sans-serif;max-width:52rem;margin:2rem auto;"
        "padding:0 1rem;line-height:1.5}pre{white-space:pre-wrap;background:#f6f6f6;"
        "padding:0.75rem;border-radius:6px}.role{font-weight:600;margin-top:1.5rem}"
        "</style>\n</head>\n<body>\n";
    text+="<h1>" + escapeForHtml(session.title) + "</h1>\n";
    text+="<p>Session " + escapeForHtml(session.sessionIdentifier);
    if (!agentName.empty()) {
        text+=", exported from " + escapeForHtml(agentName);
    }
    text+=".</p>\n";
    for (const ExportedMessage& message : session.messages) {
        text+="<p class=\"role\">" + escapeForHtml(message.role.empty() ? string("message") : message.role);
        if (message.epochSeconds>0) {
            text+=" - " + escapeForHtml(
                HumanReadableFormatting::formatTimestamp(message.epochSeconds * 1000)
            );
        }
        text+="</p>\n";
        for (const ExportedPart& part : message.parts) {
            if (part.kind=="text" && !part.text.empty()) {
                text+="<pre>" + escapeForHtml(part.text) + "</pre>\n";
            } else if (part.kind=="tool" && !part.toolName.empty()) {
                text+="<p><em>[tool: " + escapeForHtml(part.toolName) + "]</em></p>\n";
            }
        }
    }
    text+="</body>\n</html>\n";
    return text;
}
}
ExportCommand::ExportCommand(bool isTerminalAttached)
    : reporter(isTerminalAttached) {
}
string ExportCommand::describe() const {
    return string(
        "Write one session out as Markdown, JSON, or HTML, so a conversation can be kept anywhere "
        "before anything is deleted or rebuilt. Nothing is changed."
    );
}
string ExportCommand::commandName() const {
    return string("export");
}
int ExportCommand::execute(const CommandOptions& options) {
    if (!options.quiet) {
        reporter.writeBanner(commandName(), describe());
    }
    if (options.databaseFilePath.empty() || options.sessionIdentifier.empty()) {
        reporter.writeFailure(
            string("This command writes out one session, so it needs both a database and a session "
                "identifier:\n  agent-db-doctor export --database <path> --session <id>\n\n"
                "explain-space --sessions 10 prints the largest sessions with their identifiers."),
            string()
        );
        return 2;
    }
    const string format=options.exportFormat.empty() ? string("markdown") : options.exportFormat;
    if (format!="markdown" && format!="json" && format!="html") {
        reporter.writeFailure(
            string("There is no `") + format + "` format. The formats are: markdown, json, html.",
            string()
        );
        return 2;
    }
    if (reporter.isMachineReadable() && options.outputFilePath=="-") {
        reporter.writeFailure(
            string("The machine readable mode already writes to standard output, so an export "
                "cannot be printed there too. Choose a file with --output."),
            string()
        );
        return 2;
    }
    try {
        Database database(options.databaseFilePath, Database::OPEN_READ_ONLY);
        database.setBusyTimeoutMilliseconds(5000);
        ExportedSession session;
        if (!readSession(database, options.sessionIdentifier, session)) {
            reporter.writeFailure(
                string("There is no session with the identifier ") + options.sessionIdentifier
                + " in that database. explain-space --sessions 10 prints the identifiers.",
                string()
            );
            return 1;
        }
        string documentText;
        if (format=="markdown") {
            documentText=renderMarkdown(session, options.agentName);
        } else if (format=="json") {
            documentText=renderJson(session);
        } else {
            documentText=renderHtml(session, options.agentName);
        }
        long long writtenBytes=static_cast<long long>(documentText.size());
        string outputDescription;
        if (options.outputFilePath.empty() || options.outputFilePath=="-") {
            // A direct write to standard output: the document is the output, not a
            // report about the output.
            fputs(documentText.c_str(), stdout);
            fflush(stdout);
            outputDescription="standard output";
        } else {
            std::ofstream outputFile(options.outputFilePath, std::ios::trunc | std::ios::binary);
            if (!outputFile.is_open()) {
                reporter.writeFailure(
                    string("The export could not be written to ") + options.outputFilePath + ".",
                    string()
                );
                return 1;
            }
            outputFile<<documentText;
            if (!outputFile.good()) {
                reporter.writeFailure(
                    string("The export to ") + options.outputFilePath + " did not finish.",
                    string()
                );
                return 1;
            }
            outputDescription=options.outputFilePath;
            if (reporter.isMachineReadable()) {
                reporter.writeMachineResult("export", vector<MachineField>{
                    MachineField::text("session", session.sessionIdentifier),
                    MachineField::text("format", format),
                    MachineField::text("output", options.outputFilePath),
                    MachineField::number("bytes", writtenBytes),
                    MachineField::number("messages", static_cast<long long>(session.messages.size()))
                });
            } else if (!options.quiet) {
                reporter.writeLine(
                    string("Wrote ") + to_string(session.messages.size()) + " messages ("
                    + HumanReadableFormatting::formatBytes(writtenBytes) + ") to "
                    + options.outputFilePath + "."
                );
            }
            return 0;
        }
        if (reporter.isMachineReadable()) {
            reporter.writeMachineResult("export", vector<MachineField>{
                MachineField::text("session", session.sessionIdentifier),
                MachineField::text("format", format),
                MachineField::text("output", outputDescription),
                MachineField::number("bytes", writtenBytes),
                MachineField::number("messages", static_cast<long long>(session.messages.size()))
            });
        } else if (!options.quiet) {
            // A note about where the document went, since the document itself was the
            // output and carried no report.
            reporter.writeLine("");
            reporter.writeLine(
                string("Exported ") + to_string(session.messages.size()) + " messages to "
                + outputDescription + "."
            );
        }
        return 0;
    } catch (const DatabaseOpenException& openFailure) {
        reporter.writeFailure(openFailure.message(), to_string(openFailure.openResultCode()));
        return 1;
    } catch (const DatabaseOperationException& operationFailure) {
        reporter.writeFailure(operationFailure.message(), string());
        return 1;
    }
}
}
