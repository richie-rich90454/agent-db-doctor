// The implementation of the RecoveryPlaybook class.
//
// Every step is written as if it were going to be read at three in the morning by
// someone whose drive is full. That is not a stylistic choice; it is the reason
// this class exists. No step assumes knowledge of SQLite, and no step asks the user
// to decide something they have no information to decide.
#include <string>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the RecoveryPlaybook class declaration.
#include "AgentDbDoctor/Recovery/RecoveryPlaybook.hpp"
// Provides the RecoveryRecommendation value type.
#include "AgentDbDoctor/Recovery/RecoveryRecommendation.hpp"
// Provides the error translators, so a raw code never reaches the user alone.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::Doctor;
namespace AgentDbDoctor::Recovery {
string RecoveryPlaybook::openingSentence(const string& agentName, const string& databaseFilePath) {
    // The agent name is optional, because a user may be running the tool against a
    // file directly. The sentence is built in two parts so it reads naturally in
    // both cases rather than leaving a dangling possessive.
    if (agentName.empty()) {
        return string("Here is what was found with ") + databaseFilePath + ".";
    }
    return string("Here is what was found with the ") + agentName + " database at "
        + databaseFilePath + ".";
}
string RecoveryPlaybook::closingSentence() {
    return string(
        "None of the steps above have run. Your database is exactly as it was. "
    ) + "When you are ready, the agent-db-doctor service would have noticed this before it became a problem. Run: agent-db-doctor service install";
}
vector<RecoveryRecommendation> RecoveryPlaybook::buildPlanForUnreadableDatabase(
    const string& databaseFilePath,
    int sqliteResultCode,
    const string& openMessage
) {
    vector<RecoveryRecommendation> plan;
    // Step one is always the same, and it is always first: stop writing to the disk.
    // Every other step is safer once nothing else is touching the file, and several
    // are impossible while something else holds it.
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::URGENT,
        string("Stop the coding agent and leave the computer alone for a moment."),
        string(
            "Close the coding agent, and close any backup program, cloud sync client, or antivirus "
            "scan that is running. Do not restart the computer yet, and do not run a repair tool.\n\n"
            "This matters because the file is already in a state the operating system does not "
            "agree with. Anything else that writes to this disk can overwrite the parts that are "
            "still readable."
        ),
        string(),
        false
    ));
    // Step two is a copy, before anything else. It is the cheapest insurance there
    // is, and it costs nothing but disk space the user is trying to free, so the
    // explanation says plainly what the trade off is.
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::URGENT,
        string("Copy the file somewhere else before you touch it again."),
        string("Copy ") + databaseFilePath
            + " and any file next to it whose name starts with the same text and "
              "ends in -wal or -shm, onto a different drive or an external disk.\n\n"
              "This costs one file's worth of space. It is the only way to be sure the "
              "next step does not make things worse, and it is the step most people skip.",
        string(),
        false
    ));
    // Step three names the error, so the user has a search term and knows this tool
    // is not guessing.
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::URGENT,
        string("What the error actually says"),
        string("SQLite reported: ") + SqliteErrorTranslator::explain(sqliteResultCode)
        + "\n\nIn this tool's own words: " + openMessage
        + "\n\nThe numeric code is " + to_string(sqliteResultCode)
        + ". It is included so you can search for it, not because you are expected to know it.",
        string(),
        false
    ));
    // Step four is the drive check. A failing drive is the root cause of a large
    // share of these problems, and checking it before writing anything is the only
    // way to know whether writing is safe at all.
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::URGENT,
        string("Check whether the drive itself is healthy."),
        string(
            "Run the check your operating system provides. On Windows, open Command Prompt and run "
            "chkdsk D: /f, where D is the drive the database is on. On Linux, run "
            "sudo smartctl -a /dev/sdX. On macOS, run Disk Utility's First Aid on the volume.\n\n"
            "If the check reports errors, replace the drive. Writing to a failing drive is what "
            "turns a recoverable problem into an unrecoverable one."
        ),
        string(),
        false
    ));
    // Step five is the honest limit. The specification insists the tool says this
    // plainly rather than letting a user keep trying commands that cannot work.
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::BEYOND_RECOVERY,
        string("What this tool cannot do."),
        string(
            "If the drive is healthy and the file still cannot be read, the bytes are gone. No "
            "program can restore bytes that have been overwritten, including this one.\n\n"
            "What usually is still recoverable is anything that was never written into the database "
            "file: your project, your configuration, and any session the agent had already sent to a "
            "server. Check the coding agent's own web interface or another machine where the same "
            "project was used. This tool will not pretend there is a command that fixes this."
        ),
        string(),
        false
    ));
    return plan;
}
vector<RecoveryRecommendation> RecoveryPlaybook::buildPlanForDamagedDatabase(
    const DiagnosisReport& report
) {
    // Every command printed here exists. The plan used to offer two flags,
    // `--rebuild-indexes` and `--rebuild-from-dump`, and the command had neither, so
    // following the tool's own plan produced "the following argument was not
    // expected". There is one flag, `--rebuild`, and it chooses between the two
    // repairs from the diagnosis: it rebuilds the indexes when the rows can still be
    // read, which is the case this plan describes, and it dumps what can be read when
    // they cannot, which is the unrecoverable plan instead.
    vector<RecoveryRecommendation> plan;
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::URGENT,
        string("Close the coding agent."),
        string(
            string("SQLite reports: ") + report.getIntegrityMessage()
            + "\n\nClose the coding agent first. While it is open it may write to the file again, and "
            "everything below assumes it is not."
        ),
        string(),
        false
    ));
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::URGENT,
        string("Copy the file somewhere else before touching it."),
        string(
            "Copy the database and any file next to it whose name starts with the same text and ends "
            "in -wal or -shm. This costs one file's worth of space and it is the only way to be sure "
            "that the next step cannot make things worse."
        ),
        string(),
        false
    ));
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::ATTENTION,
        string("Rebuild the indexes on the copy."),
        string(
            "SQLite can usually rebuild a damaged index from the data it describes, which is the most "
            "common kind of damage and the easiest to repair. The tool works on a copy beside your "
            "file and never opens your original for writing.\n\n"
            "If the rows themselves cannot be read, the same command notices that on its own and dumps "
            "what SQLite can still read into a fresh database instead, leaving out the sessions that "
            "are too badly damaged rather than writing them half way."
        ),
        string("agent-db-doctor recover --database \"") + report.getDatabaseFilePath() + "\" --rebuild",
        true
    ));
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::INFORMATION,
        string("If the copy is good, put it back."),
        string(
            "Once you have a database that opens cleanly, move it into place over the damaged one, "
            "keeping the damaged file with a different name rather than deleting it. The coding agent "
            "will open the new file the next time it starts."
        ),
        string(),
        true
    ));
    return plan;
}
vector<RecoveryRecommendation> RecoveryPlaybook::buildPlanForOversizedDatabase(
    const DiagnosisReport& report
) {
    vector<RecoveryRecommendation> plan;
    const long long duplicateRows=report.getRedundantEventRowCount();
    const long long reclaimableBytes=report.getReclaimableBytes();
    if (duplicateRows>0) {
        plan.push_back(RecoveryRecommendation(
            SeverityLevel::ATTENTION,
            string("Remove the duplicate rows."),
            string("This database holds " + to_string(duplicateRows)
                + " rows that are older copies of messages that have since been changed. Each one is a "
                  "complete copy of a state the conversation has already moved past, so the current text "
                  "is stored elsewhere and nothing is lost by removing them.\n\n"
                + "The tool makes a copy of the file first and tells you where it put it."
            ),
            string("agent-db-doctor prune --database \"") + report.getDatabaseFilePath() + "\"",
            true
        ));
    }
    if (reclaimableBytes>0) {
        plan.push_back(RecoveryRecommendation(
            SeverityLevel::ATTENTION,
            string("Shrink the file."),
            string(
                "After the duplicates are gone, SQLite still holds the pages they used. The file "
                "stays the same size on disk until those pages are handed back, which is what "
                "compaction does.\n\n"
            ) + "There are " + to_string(reclaimableBytes)
            + " bytes of unused space inside the file right now. Compaction needs free disk space "
            + "equal to the whole file while it works, because it writes a new file before it "
            + "replaces the old one.",
            string("agent-db-doctor compact --database \"") + report.getDatabaseFilePath() + "\"",
            true
        ));
    }
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::INFORMATION,
        string("Stop it happening again."),
        string(
            "This database grew because every change to a message writes a full new copy of it. The "
            "service watches the size and tells you when it crosses a threshold you choose, so the "
            "next time it grows, you find out on a day when it does not matter."
        ),
        string("agent-db-doctor service install"),
        false
    ));
    return plan;
}
// The plan for a path that is a folder rather than a database.
//
// Several supported agents keep plain text files in a folder. There is no damage to
// describe, so the plan says so in one step and stops. It exists because the honest
// answer for a folder used to be missing, and the folder was falling through to the
// healthy plan, which described it as a sound database whose pages had all checked
// out. Nothing had been checked at all.
vector<RecoveryRecommendation> RecoveryPlaybook::buildPlanForFolderOfTextFiles() {
    vector<RecoveryRecommendation> plan;
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::INFORMATION,
        string("Nothing to recover, because this is not a database."),
        string(
            "The path you gave is a folder, not a database file. Several supported coding "
            "agents keep their history as plain text files inside a folder rather than in a "
            "SQLite database, and there is nothing here for a database repair tool to read, "
            "check, or rebuild.\n\n"
            "Nothing has been changed, and nothing is damaged. If the coding agent that owns "
            "this folder is not working, the cause is not in this folder, and this tool cannot "
            "tell you what it is."
        ),
        string(),
        false
    ));
    return plan;
}
vector<RecoveryRecommendation> RecoveryPlaybook::buildPlanForHealthyDatabase(
    const DiagnosisReport& report
) {
    vector<RecoveryRecommendation> plan;
    plan.push_back(RecoveryRecommendation(
        SeverityLevel::INFORMATION,
        string("Nothing needs doing."),
        string(
            "This database is healthy. Every page checked out, and there is nothing to repair. "
            "Its size is "
        ) + to_string(report.getLogicalSizeBytes())
        + " bytes, and it is not holding unused space inside itself beyond what a file system "
        + "normally wastes.",
        string(),
        false
    ));
    return plan;
}
}
