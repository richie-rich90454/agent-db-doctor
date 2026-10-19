// The unit tests for the JSON reader and for the event parsing.
//
// These are the two halves of the window that have no window in them: the reader
// turns one line of the command line's output into a value, and the parsing turns a
// value into state. Both are tested here against hand written documents, including
// the malformed ones, because a window that cannot survive a surprising line from
// its own child is a window that stops drawing when it is needed most.
//
// The tests link nothing but the two files under test, so they run on a machine
// with no display, no SDL, and no database.
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the reader under test.
#include "Json.hpp"
// Provides the state and the event parsing under test.
#include "GuiState.hpp"
using namespace std;
using namespace AgentDbDoctor::Gui;
namespace {
// Parses and fails the test when the text is not one document. The value is
// returned through the out parameter so a test can read it without repeating the
// whole arrangement for every field.
JsonValue mustParse(const string& text) {
    JsonValue parsedValue;
    REQUIRE(JsonValue::parse(text, parsedValue));
    return parsedValue;
}
// The text of a string's bytes, as hexadecimal, so a test can say exactly which
// bytes a decoded escape became.
string bytesAsHex(const string& text) {
    static const char* HEX_DIGITS="0123456789abcdef";
    string hexadecimal;
    for (const char currentByte : text) {
        const unsigned char byteValue=static_cast<unsigned char>(currentByte);
        hexadecimal.push_back(HEX_DIGITS[byteValue>>4]);
        hexadecimal.push_back(HEX_DIGITS[byteValue & 0x0FU]);
    }
    return hexadecimal;
}
}
TEST_CASE("a document of every kind parses", "[json]") {
    const JsonValue document=mustParse(
        "{\"number\":42,\"negative\":-17,\"yes\":true,\"no\":false,\"nothing\":null,"
        "\"text\":\"hello\",\"list\":[1,2,3],\"nested\":{\"inner\":\"value\"}}");
    REQUIRE(document.is(JsonValue::Kind::Object));
    REQUIRE(document.memberInteger("number", 0)==42);
    REQUIRE(document.memberInteger("negative", 0)==-17);
    REQUIRE(document.memberBoolean("yes", false));
    REQUIRE_FALSE(document.memberBoolean("no", true));
    REQUIRE(document.memberBoolean("missing", false)==false);
    REQUIRE(document.memberBoolean("no", true)==false);
    const JsonValue* nothingValue=document.member("nothing");
    REQUIRE(nothingValue!=nullptr);
    REQUIRE(nothingValue->is(JsonValue::Kind::Null));
    // A missing member is absent rather than a value of some other kind, which is
    // what lets the callers use the member accessors without checking first.
    REQUIRE(document.member("absent")==nullptr);
    REQUIRE(document.memberText("text", "")=="hello");
    REQUIRE(document.memberText("absent", "fallback")=="fallback");
    REQUIRE(document.memberInteger("absent", 7)==7);
    const JsonValue* listValue=document.member("list");
    REQUIRE(listValue!=nullptr);
    REQUIRE(listValue->is(JsonValue::Kind::Array));
    REQUIRE(listValue->elements().size()==3);
    REQUIRE(listValue->elements()[0].integerValue()==1);
    REQUIRE(listValue->elements()[2].integerValue()==3);
    // A value of the wrong kind reports the neutral answer rather than throwing,
    // which is what keeps a surprising line from stopping the window.
    REQUIRE(listValue->memberText("number", "none")=="none");
    REQUIRE(listValue->integerValue()==0);
    const JsonValue* nestedValue=document.member("nested");
    REQUIRE(nestedValue!=nullptr);
    REQUIRE(nestedValue->memberText("inner", "")=="value");
    // The members keep the order the document listed them in, which the result
    // dialogue depends on: the command line writes its fields in a chosen order.
    REQUIRE(document.members().size()==8);
    REQUIRE(document.members()[0].first=="number");
    REQUIRE(document.members()[7].first=="nested");
}
TEST_CASE("numbers keep the values the command line wrote", "[json]") {
    // A byte count larger than a double holds exactly must not be rounded: the
    // window shows the number the command line printed, not an approximation of it.
    const JsonValue document=mustParse(
        "{\"big\":9223372036854775807,\"bytes\":21474836480,\"fraction\":-2.75,"
        "\"rounded\":7.9,\"exponent\":1.5e3,\"zero\":0}");
    REQUIRE(document.memberInteger("big", 0)==9223372036854775807LL);
    REQUIRE(document.memberInteger("bytes", 0)==21474836480LL);
    // A fractional number is rounded toward zero, as the header promises.
    REQUIRE(document.memberInteger("fraction", 0)==-2);
    REQUIRE(document.memberInteger("rounded", 0)==7);
    REQUIRE(document.memberInteger("exponent", 0)==1500);
    REQUIRE(document.memberInteger("zero", 0)==0);
}
TEST_CASE("escapes are decoded, including the two halves of one character", "[json]") {
    const JsonValue document=mustParse(
        "{\"punctuation\":\"a \\\"b\\\" \\\\ c \\/ d\","
        "\"whitespace\":\"tab\\there\\nnew line\","
        "\"accent\":\"caf\\u00e9\","
        "\"face\":\"\\ud83d\\ude00\","
        "\"plain\":\"no escapes here\"}");
    REQUIRE(document.memberText("punctuation", "")==string("a \"b\" \\ c / d"));
    REQUIRE(document.memberText("whitespace", "")==string("tab\there\nnew line"));
    // One accented letter is two bytes in UTF-8.
    REQUIRE(bytesAsHex(document.memberText("accent", ""))=="636166c3a9");
    // A character beyond the basic plane travels as a high and a low half, and is
    // written back as the four bytes UTF-8 uses for it.
    REQUIRE(bytesAsHex(document.memberText("face", ""))=="f09f9880");
    REQUIRE(document.memberText("plain", "")=="no escapes here");
}
TEST_CASE("objects and arrays nest as deeply as the document says", "[json]") {
    const JsonValue document=mustParse(
        "{\"outer\":{\"middle\":[{\"inner\":1},{\"inner\":2},[]]},\"emptyObject\":{},"
        "\"emptyArray\":[]}");
    const JsonValue* middleValue=document.member("outer")->member("middle");
    REQUIRE(middleValue!=nullptr);
    REQUIRE(middleValue->elements().size()==3);
    REQUIRE(middleValue->elements()[0].memberInteger("inner", 0)==1);
    REQUIRE(middleValue->elements()[1].memberInteger("inner", 0)==2);
    REQUIRE(middleValue->elements()[2].elements().empty());
    REQUIRE(document.member("emptyObject")->members().empty());
    REQUIRE(document.member("emptyArray")->elements().empty());
}
TEST_CASE("a malformed document is refused and leaves the value alone", "[json]") {
    // Every one of these arrived at some point in the shape of a line of output
    // that was nearly an object. None of them may be accepted, and none of them may
    // change the value the caller passed in.
    const vector<string> malformedDocuments={
        "",
        "   ",
        "{",
        "}",
        "[",
        "[1,",
        "{\"a\":}",
        "{\"a\" 1}",
        "{a:1}",
        "{\"a\":1,}",
        "[1,2,]",
        "\"unterminated",
        "{\"a\":\"unterminated}",
        "tru",
        "nul",
        "{\"a\":01}",
        "{\"a\":.5}",
        "{\"a\":5.}",
        "{\"a\":+5}",
        "{\"a\":1e}",
        "{\"a\":\"bad \\x escape\"}",
        "{\"a\":\"raw\nnewline\"}",
        "{\"a\":1}extra",
        "[1] garbage",
        "{\"a\":1}{\"b\":2}"};
    for (const string& malformedDocument : malformedDocuments) {
        JsonValue untouchedValue=mustParse("1");
        REQUIRE_FALSE(JsonValue::parse(malformedDocument, untouchedValue));
        // The value a caller passed in is only written when the text really was a
        // document, so a caller can keep the previous value and re-use the slot.
        REQUIRE(untouchedValue.is(JsonValue::Kind::Number));
        REQUIRE(untouchedValue.integerValue()==1);
    }
}
TEST_CASE("the nesting limit is enforced", "[json]") {
    // A line of thousands of opening brackets must not run the window out of stack.
    const auto nestedArrayDocument=[](int depth) {
        string documentText;
        for (int level=0; level<depth; ++level) {
            documentText+="[";
        }
        documentText+="1";
        for (int level=0; level<depth; ++level) {
            documentText+="]";
        }
        return documentText;
    };
    // Sixty four levels are all the command line's own events ever need, and they
    // are accepted.
    JsonValue deepValue;
    REQUIRE(JsonValue::parse(nestedArrayDocument(64), deepValue));
    // One more is refused rather than risking the stack of a window that has to
    // keep drawing.
    JsonValue tooDeepValue;
    REQUIRE_FALSE(JsonValue::parse(nestedArrayDocument(65), tooDeepValue));
    REQUIRE_FALSE(JsonValue::parse(nestedArrayDocument(5000), tooDeepValue));
}
TEST_CASE("only a line that looks like an object is offered to the reader", "[json]") {
    REQUIRE(looksLikeJsonObject("{\"event\":\"line\"}"));
    REQUIRE(looksLikeJsonObject("  {\"event\":\"line\"}  "));
    REQUIRE_FALSE(looksLikeJsonObject(""));
    REQUIRE_FALSE(looksLikeJsonObject("Looking at 4 databases."));
    REQUIRE_FALSE(looksLikeJsonObject("[1,2,3]"));
    REQUIRE_FALSE(looksLikeJsonObject("{"));
    REQUIRE_FALSE(looksLikeJsonObject("\"a string\""));
}
TEST_CASE("one database event becomes one record", "[state]") {
    // The shape of a real `database` object, as `diagnose --machine-readable`
    // writes it. The escapes in the path are the reader's business, not the
    // caller's, so the path in the state is the real one.
    const string databaseEvent=
        "{\"event\":\"database\","
        "\"path\":\"C:\\\\Users\\\\dev\\\\AppData\\\\Local\\\\opencode\\\\opencode.db\","
        "\"agent\":\"OpenCode\",\"exists\":true,\"isDirectory\":false,\"healthy\":false,"
        "\"sizeBytes\":21474836480,\"allocatedBytes\":19864223744,"
        "\"hasWriteAheadLog\":true,\"writeAheadLogBytes\":4194304,"
        "\"modifiedMilliseconds\":1761000000000,\"pageSizeBytes\":4096,\"pageCount\":5242880,"
        "\"freePageCount\":1310720,\"reclaimableBytes\":5368709120,\"journal\":\"wal\","
        "\"autoVacuum\":false,\"compactionSupported\":true,"
        "\"schema\":\"the first generation of the event log\",\"redundantEventRows\":184000,"
        "\"integrity\":\"intact\",\"integrityMessage\":\"every page checked out\","
        "\"databaseOpenSucceeded\":true,\"openFailureCode\":0,\"durationNanoseconds\":842000000,"
        "\"metadataCallCount\":37,\"redundantEventTypes\":[\"message.updated\"],"
        "\"rows\":{\"session\":{\"count\":812,\"approximate\":false},"
        "\"part\":{\"count\":94211,\"approximate\":true}},"
        "\"warnings\":[\"The event log is most of this file.\"],"
        "\"nextSteps\":[\"agent-db-doctor compact --database opencode.db\"]}";
    const JsonValue parsedEvent=mustParse(databaseEvent);
    REQUIRE(parsedEvent.memberText("event", "")=="database");
    GuiState state;
    applyJsonEvent(state, parsedEvent);
    REQUIRE(state.databases.size()==1);
    const DatabaseRecord& record=state.databases.front();
    REQUIRE(record.filePath=="C:\\Users\\dev\\AppData\\Local\\opencode\\opencode.db");
    REQUIRE(record.agentName=="OpenCode");
    REQUIRE(record.exists);
    REQUIRE_FALSE(record.isDirectory);
    REQUIRE_FALSE(record.isHealthy);
    REQUIRE(record.sizeBytes==21474836480LL);
    REQUIRE(record.allocatedBytes==19864223744LL);
    REQUIRE(record.hasWriteAheadLog);
    REQUIRE(record.writeAheadLogBytes==4194304LL);
    REQUIRE(record.modifiedMilliseconds==1761000000000LL);
    REQUIRE(record.pageSizeBytes==4096LL);
    REQUIRE(record.pageCount==5242880LL);
    REQUIRE(record.freePageCount==1310720LL);
    REQUIRE(record.reclaimableBytes==5368709120LL);
    REQUIRE(record.journalMode=="wal");
    REQUIRE_FALSE(record.autoVacuumEnabled);
    REQUIRE(record.compactionSupported);
    REQUIRE(record.schemaDescription=="the first generation of the event log");
    REQUIRE(record.redundantEventRows==184000LL);
    REQUIRE(record.integrity=="intact");
    REQUIRE(record.integrityMessage=="every page checked out");
    REQUIRE(record.databaseOpenSucceeded);
    REQUIRE(record.openFailureCode==0LL);
    REQUIRE(record.durationNanoseconds==842000000LL);
    REQUIRE(record.metadataCallCount==37LL);
    REQUIRE(record.redundantEventTypes.size()==1);
    REQUIRE(record.redundantEventTypes[0]=="message.updated");
    REQUIRE(record.rowCounts.size()==2);
    REQUIRE(record.rowCounts[0].tableName=="session");
    REQUIRE(record.rowCounts[0].rowCount==812LL);
    REQUIRE_FALSE(record.rowCounts[0].isApproximate);
    REQUIRE(record.rowCounts[1].tableName=="part");
    REQUIRE(record.rowCounts[1].rowCount==94211LL);
    REQUIRE(record.rowCounts[1].isApproximate);
    REQUIRE(record.warnings.size()==1);
    REQUIRE(record.warnings[0]=="The event log is most of this file.");
    REQUIRE(record.nextSteps.size()==1);
    REQUIRE(record.nextSteps[0]=="agent-db-doctor compact --database opencode.db");
    // The words the window shows for this record, which is what a reader of the
    // table actually sees.
    REQUIRE(healthWord(record)=="Attention");
    REQUIRE(totalDatabaseBytes(state)==21474836480LL);
    REQUIRE(totalReclaimableBytes(state)==5368709120LL);
    REQUIRE(largestDatabasesFirst(state).size()==1);
    // A second look at the same database updates the row rather than adding one,
    // which is what a diagnose of a single database does to a table full of them.
    const JsonValue secondEvent=mustParse(
        "{\"event\":\"database\",\"path\":\"C:\\\\Users\\\\dev\\\\AppData\\\\Local\\\\opencode"
        "\\\\opencode.db\",\"agent\":\"OpenCode\",\"exists\":true,\"healthy\":true,"
        "\"sizeBytes\":1024,\"integrity\":\"intact\"}");
    applyJsonEvent(state, secondEvent);
    REQUIRE(state.databases.size()==1);
    REQUIRE(state.databases.front().isHealthy);
    REQUIRE(state.databases.front().sizeBytes==1024LL);
    REQUIRE(healthWord(state.databases.front())=="Sound");
    REQUIRE(findDatabaseRecord(state, "C:\\nowhere\\absent.db")==nullptr);
}
TEST_CASE("one result event becomes typed fields", "[state]") {
    const JsonValue parsedEvent=mustParse(
        "{\"event\":\"result\",\"command\":\"compact\",\"fields\":{"
        "\"previewOnly\":false,\"refused\":false,\"rolledBack\":false,\"rowsRemoved\":1200,"
        "\"protectedSessions\":42,\"verifiedSessions\":8,\"safetyCopyRemoved\":true,"
        "\"safetyCopyBytes\":2147483648,\"reclaimableBytesNow\":5368709120}}");
    GuiState state;
    applyJsonEvent(state, parsedEvent);
    REQUIRE(state.lastResult.isAvailable);
    REQUIRE(state.lastResult.commandName=="compact");
    REQUIRE(state.lastResult.fields.size()==9);
    const ResultField& rowsRemovedField=state.lastResult.fields[3];
    REQUIRE(rowsRemovedField.name=="rowsRemoved");
    REQUIRE(rowsRemovedField.kind==JsonValue::Kind::Number);
    REQUIRE(rowsRemovedField.numberValue==1200LL);
    // A number is written with thousands separators, and a number whose name says
    // it is a byte count is written as a size, because that is the number a person
    // is deciding about.
    REQUIRE(formatResultFieldValue(rowsRemovedField)=="1,200");
    REQUIRE(formatResultFieldValue(state.lastResult.fields[7])=="2.0 GB");
    REQUIRE(formatResultFieldValue(state.lastResult.fields[8])=="5.0 GB");
    const ResultField& removedField=state.lastResult.fields[6];
    REQUIRE(removedField.name=="safetyCopyRemoved");
    REQUIRE(removedField.kind==JsonValue::Kind::Boolean);
    REQUIRE(removedField.booleanValue);
    REQUIRE(formatResultFieldValue(removedField)=="yes");
    const ResultField& previewField=state.lastResult.fields[0];
    REQUIRE(formatResultFieldValue(previewField)=="no");
}
TEST_CASE("the events a run ends with move the state as they should", "[state]") {
    GuiState state;
    // The first look starts: the list is emptied and the bar says what is running.
    beginScan(state);
    REQUIRE(state.scan.isRunning);
    REQUIRE(state.run.isActive);
    REQUIRE_FALSE(state.scan.hasFinished);
    applyJsonEvent(state, mustParse(
        "{\"event\":\"progress\",\"task\":\"Reading the event log\",\"done\":250,"
        "\"total\":1000,\"finished\":false}"));
    REQUIRE(state.progress.task=="Reading the event log");
    REQUIRE(state.progress.completedUnits==250LL);
    REQUIRE(state.progress.totalUnits==1000LL);
    REQUIRE_FALSE(state.progress.finished);
    // An event this version does not know is ignored rather than treated as an
    // error: a newer command line must not break an older window.
    applyJsonEvent(state, mustParse("{\"event\":\"something-newer\",\"value\":1}"));
    REQUIRE(state.run.isActive);
    applyJsonEvent(state, mustParse("{\"event\":\"warning\",\"text\":\"Mind the gap.\"}"));
    applyJsonEvent(state, mustParse(
        "{\"event\":\"next\",\"steps\":[\"Run the vacuum next.\"]}"));
    applyJsonEvent(state, mustParse(
        "{\"event\":\"backup\",\"path\":\"C:\\\\data\\\\opencode.db.backup\","
        "\"sizeBytes\":1024}"));
    applyJsonEvent(state, mustParse("{\"event\":\"exit\",\"exitCode\":0}"));
    REQUIRE(state.hasExitCode);
    REQUIRE(state.lastExitCode==0);
    REQUIRE(state.runWarnings.size()==1);
    REQUIRE(state.runNextSteps.size()==1);
    REQUIRE(state.runBackups.size()==1);
    REQUIRE(state.runBackups[0].sizeBytes==1024LL);
    // The run is closed by the interface when the child ends, which is where the
    // exit code the operating system reports comes from.
    REQUIRE(endRun(state, 0));
    REQUIRE_FALSE(state.run.isActive);
    REQUIRE_FALSE(state.scan.isRunning);
    REQUIRE(state.scan.hasFinished);
    REQUIRE(state.scan.finishedMilliseconds>0);
    REQUIRE_FALSE(endRun(state, 0));
}
TEST_CASE("the log pane keeps the newest lines and no more", "[state]") {
    GuiState state;
    for (int lineIndex=0; lineIndex<600; ++lineIndex) {
        appendLogLine(state, "line " + to_string(lineIndex));
    }
    REQUIRE(state.logLines.size()==LOG_LINE_LIMIT);
    // The oldest lines fall off the top, so what a reader sees is what just
    // happened rather than the beginning of the session.
    REQUIRE(state.logLines.front()=="line 100");
    REQUIRE(state.logLines.back()=="line 599");
}
TEST_CASE("an invocation becomes the exact command line it claims to be", "[state]") {
    CliInvocation compactionPreview;
    compactionPreview.command="compact";
    compactionPreview.databasePath="C:\\data\\opencode.db";
    compactionPreview.dryRun=true;
    const vector<string> previewArguments=buildCommandArguments(compactionPreview);
    REQUIRE(previewArguments.size()==6);
    REQUIRE(previewArguments[0]=="compact");
    REQUIRE(previewArguments[1]=="-d");
    REQUIRE(previewArguments[2]=="C:\\data\\opencode.db");
    REQUIRE(previewArguments[3]=="--dry-run");
    REQUIRE(previewArguments[4]=="--quiet");
    REQUIRE(previewArguments[5]=="--machine-readable");
    // A dry run never answers the question it exists to ask, so the two flags are
    // never passed together.
    CliInvocation reclaimRun;
    reclaimRun.command="reclaim";
    reclaimRun.databasePath="C:\\data\\opencode.db";
    reclaimRun.reclaimTargetBytes=21474836480LL;
    reclaimRun.answerYes=true;
    reclaimRun.keepSafetyCopy=true;
    reclaimRun.backupDestinationPath="D:\\copies";
    const vector<string> reclaimArguments=buildCommandArguments(reclaimRun);
    bool holdsTarget=false;
    bool holdsYes=false;
    bool holdsKeepBackup=false;
    bool holdsBackupTo=false;
    bool holdsDryRun=false;
    for (size_t argumentIndex=0; argumentIndex<reclaimArguments.size(); ++argumentIndex) {
        const string& argument=reclaimArguments[argumentIndex];
        if (argument=="--target" && argumentIndex + 1<reclaimArguments.size()) {
            holdsTarget=reclaimArguments[argumentIndex + 1]=="20GB";
        }
        if (argument=="--yes") {
            holdsYes=true;
        }
        if (argument=="--dry-run") {
            holdsDryRun=true;
        }
        if (argument=="--keep-backup") {
            holdsKeepBackup=true;
        }
        if (argument=="--backup-to" && argumentIndex + 1<reclaimArguments.size()) {
            holdsBackupTo=reclaimArguments[argumentIndex + 1]=="D:\\copies";
        }
    }
    REQUIRE(holdsTarget);
    REQUIRE(holdsYes);
    REQUIRE(holdsKeepBackup);
    REQUIRE(holdsBackupTo);
    REQUIRE_FALSE(holdsDryRun);
    // The action of a command that takes one is a positional, before the options,
    // which is the order the command line documents.
    CliInvocation sessionListing;
    sessionListing.command="sessions";
    sessionListing.action="list";
    sessionListing.databasePath="C:\\data\\opencode.db";
    sessionListing.sessionLimit=50;
    const vector<string> listingArguments=buildCommandArguments(sessionListing);
    REQUIRE(listingArguments[0]=="sessions");
    REQUIRE(listingArguments[1]=="list");
    REQUIRE(listingArguments[2]=="-d");
}
TEST_CASE("the settings survive a round trip through their file", "[state]") {
    InterfaceSettings settings;
    settings.themeMode=THEME_DARK;
    settings.reclaimTargetGigabytes=35;
    settings.keepSafetyCopy=true;
    settings.backupDestinationPath="D:\\database copies";
    settings.windowX=120;
    settings.windowY=64;
    settings.windowWidth=1280;
    settings.windowHeight=720;
    settings.hasWindowPlacement=true;
    settings.logPaneIsOpen=false;
    InterfaceSettings readBackSettings;
    parseSettings(serializeSettings(settings), readBackSettings);
    REQUIRE(readBackSettings.themeMode==THEME_DARK);
    REQUIRE(readBackSettings.reclaimTargetGigabytes==35LL);
    REQUIRE(readBackSettings.keepSafetyCopy);
    REQUIRE(readBackSettings.backupDestinationPath=="D:\\database copies");
    REQUIRE(readBackSettings.hasWindowPlacement);
    REQUIRE(readBackSettings.windowX==120);
    REQUIRE(readBackSettings.windowY==64);
    REQUIRE(readBackSettings.windowWidth==1280);
    REQUIRE(readBackSettings.windowHeight==720);
    REQUIRE_FALSE(readBackSettings.logPaneIsOpen);
    // A file a person edited by hand can hold a line the program does not know, a
    // value that makes no sense, and a window too small to use. None of those may
    // stop the window from opening.
    InterfaceSettings editedSettings;
    parseSettings(
        "theme=not-a-theme\n"
        "reclaimTargetGigabytes=not a number\n"
        "keepSafetyCopy=maybe\n"
        "something-newer=42\n"
        "windowWidth=10\n"
        "windowHeight=10\n",
        editedSettings);
    REQUIRE(editedSettings.themeMode==THEME_FOLLOW_SYSTEM);
    REQUIRE(editedSettings.reclaimTargetGigabytes==20LL);
    REQUIRE_FALSE(editedSettings.keepSafetyCopy);
    REQUIRE(editedSettings.windowWidth>=800);
    REQUIRE(editedSettings.windowHeight>=560);
    REQUIRE_FALSE(editedSettings.hasWindowPlacement);
}
TEST_CASE("an audit event becomes readable lines", "[state]") {
    // The audit listing is a list of records, and the window has no screen of its own
    // for them yet, so each entry becomes one line of the run's text: the log pane
    // and the result dialogue are where a command's own output lives.
    GuiState state;
    beginRun(state, "reading the audit log");
    applyJsonEvent(state, mustParse(
        "{\"event\":\"audit\",\"entries\":["
        "{\"epochSeconds\":1761000000,\"action\":\"vacuum\","
        "\"path\":\"C:\\\\data\\\\opencode.db\",\"sizeBeforeBytes\":21474836480,"
        "\"sizeAfterBytes\":10737418240,\"backupPath\":\"C:\\\\data\\\\opencode.db.backup\","
        "\"outcome\":\"succeeded\",\"exitCode\":0},"
        "{\"epochSeconds\":0,\"action\":\"compact\",\"path\":\"C:\\\\data\\\\other.db\","
        "\"outcome\":\"refused\",\"exitCode\":1}]}"));
    REQUIRE(state.runTextLines.size()==2);
    REQUIRE(state.runTextLines[0].find("vacuum")!=string::npos);
    REQUIRE(state.runTextLines[0].find("C:\\data\\opencode.db")!=string::npos);
    REQUIRE(state.runTextLines[0].find("20.0 GB -> 10.0 GB")!=string::npos);
    REQUIRE(state.runTextLines[0].find("succeeded")!=string::npos);
    REQUIRE(state.runTextLines[1].find("compact")!=string::npos);
    REQUIRE(state.runTextLines[1].find("refused")!=string::npos);
}
TEST_CASE("the summary sentences say what happened", "[state]") {
    GuiState state;
    REQUIRE(scanSummarySentence(state, 0).find("No look has finished")!=string::npos);
    beginScan(state);
    REQUIRE(scanSummarySentence(state, 0).find("Looking at every database")!=string::npos);
    applyJsonEvent(state, mustParse(
        "{\"event\":\"database\",\"path\":\"/tmp/one.db\",\"agent\":\"OpenCode\","
        "\"exists\":true,\"sizeBytes\":2048}"));
    REQUIRE(scanSummarySentence(state, 0).find("found so far")!=string::npos);
    endRun(state, 0);
    const long long scanFinishedAt=state.scan.finishedMilliseconds;
    REQUIRE(
        scanSummarySentence(state, scanFinishedAt + 120000).find("1 database was found")
        !=string::npos);
    REQUIRE(scanSummarySentence(state, scanFinishedAt + 120000).find("2 minutes ago")!=string::npos);
    // The verdict of a run that was stopped is not the verdict of a run that
    // failed: stopping a command is the user's decision and is described as one.
    state.hasError=true;
    state.lastErrorMessage="The safety copy could not be made.";
    REQUIRE(
        runVerdictSentence(state, 1, false).find("stopped with a problem")!=string::npos);
    REQUIRE(runVerdictSentence(state, 1, true).find("You stopped this command")!=string::npos);
    // A command line mistake is refused before any command runs, so no closing
    // object arrives at all. That must not be reported as a success, which is what
    // the process's own code would say: it is zero for every background process.
    GuiState refusedState;
    beginRun(refusedState, "a mistake");
    appendLogLine(refusedState, "The following argument was not expected: --no-such-flag");
    endRun(refusedState, 0);
    REQUIRE_FALSE(refusedState.hasExitCode);
    REQUIRE(
        runVerdictSentence(refusedState, 0, false).find("without writing its closing report")
        !=string::npos);
    REQUIRE(refusedState.lastRunEnding.find("without reporting")!=string::npos);
    GuiState finishedState;
    beginRun(finishedState, "a look");
    applyJsonEvent(finishedState, mustParse("{\"event\":\"exit\",\"exitCode\":0}"));
    endRun(finishedState, 0);
    REQUIRE(finishedState.hasExitCode);
    REQUIRE(finishedState.lastRunEnding=="The last command ended with exit code 0.");
}
