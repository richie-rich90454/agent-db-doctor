// The implementation of the AuditLog class.
//
// Every write reads the log, adds the line, prunes what is too old, and rewrites
// the file through a temporary name. The rewrite is what makes the pruning and
// the atomic replacement fall out of the same pass, exactly as the history store
// does it, and the file is capped at twenty thousand short lines, so the cost is
// bounded no matter how long the service runs.
// Provides `ifstream` and `ofstream`, used to read and write the store.
#include <fstream>
// Provides `rename`, `remove`, and `create_directories`, used to replace the
// store atomically and to create its folder on the first write.
#include <filesystem>
// Provides `from_chars`, the integer parser that allocates nothing and does not
// consult the locale, so a number in the file reads the same on every machine.
#include <charconv>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the AuditLog class declaration and the AuditEntry value type.
#include "AgentDbDoctor/Service/AuditLog.hpp"
// Provides PlatformMetadata, for the shared configuration directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Service {
namespace {
// The store path is overridden by the tests before anything reads or writes it.
string overrideStoreFilePath;
// True when the override above has been set.
bool storePathWasOverridden=false;
// Makes a field safe to write: a tab or a line break inside a value would break
// the line it is on and every line after it, so both are written as a space. The
// trade is a slightly mangled sentence in exchange for a log that always parses.
string sanitizeField(const string& text) {
    string sanitizedText;
    sanitizedText.reserve(text.size());
    for (const char currentCharacter : text) {
        if (currentCharacter=='\t' || currentCharacter=='\n' || currentCharacter=='\r') {
            sanitizedText.push_back(' ');
        } else {
            sanitizedText.push_back(currentCharacter);
        }
    }
    return sanitizedText;
}
// Parses a whole number that must fill the text exactly. Returns false when the
// text is not a number, which makes the caller skip the line.
bool parseWholeNumber(const string& text, long long& valueOut) {
    if (text.empty()) {
        return false;
    }
    long long parsedValue=0;
    const auto parseResult=from_chars(text.data(), text.data() + text.size(), parsedValue);
    if (parseResult.ec!=errc{} || parseResult.ptr!=text.data() + text.size()) {
        return false;
    }
    valueOut=parsedValue;
    return true;
}
// Splits one line into its fields. Returns false when the line does not hold
// exactly the eight fields an entry has, or when one of the numbers cannot be
// read, in which case the caller skips the line.
bool parseLine(const string& line, AuditEntry& entryOut) {
    const size_t fieldCount=8;
    size_t fieldStart=0;
    string fields[fieldCount];
    for (size_t fieldIndex=0; fieldIndex<fieldCount - 1; ++fieldIndex) {
        const size_t nextTab=line.find('\t', fieldStart);
        if (nextTab==string::npos) {
            return false;
        }
        fields[fieldIndex]=line.substr(fieldStart, nextTab - fieldStart);
        fieldStart=nextTab + 1;
    }
    // The last field runs to the end of the line, because it is the only one that
    // may contain anything a sentence contains.
    fields[fieldCount - 1]=line.substr(fieldStart);
    AuditEntry entry;
    if (!parseWholeNumber(fields[0], entry.epochSeconds)) {
        return false;
    }
    entry.action=fields[1];
    entry.databaseFilePath=fields[2];
    if (!parseWholeNumber(fields[3], entry.sizeBeforeBytes)) {
        return false;
    }
    if (!parseWholeNumber(fields[4], entry.sizeAfterBytes)) {
        return false;
    }
    entry.backupFilePath=fields[5];
    entry.outcome=fields[6];
    long long exitCode=0;
    if (!parseWholeNumber(fields[7], exitCode)) {
        return false;
    }
    entry.exitCode=static_cast<int>(exitCode);
    entryOut=std::move(entry);
    return true;
}
// Reads every entry the store holds, in file order, skipping lines that are not
// shaped like one.
vector<AuditEntry> readAllEntries(const string& storePath) {
    vector<AuditEntry> entries;
    std::ifstream storeFile(storePath);
    if (!storeFile.is_open()) {
        return entries;
    }
    string line;
    while (getline(storeFile, line)) {
        if (line.empty()) {
            continue;
        }
        AuditEntry entry;
        if (parseLine(line, entry)) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}
// Writes the entries back through a temporary file and a rename.
bool writeAllEntries(const string& storePath, const vector<AuditEntry>& entries) {
    const std::filesystem::path storeDirectory=std::filesystem::path(storePath).parent_path();
    std::error_code directoryError;
    std::filesystem::create_directories(storeDirectory, directoryError);
    if (directoryError) {
        return false;
    }
    const string temporaryStorePath=storePath + ".writing";
    {
        std::ofstream temporaryStoreFile(temporaryStorePath, std::ios::trunc);
        if (!temporaryStoreFile.is_open()) {
            return false;
        }
        for (const AuditEntry& entry : entries) {
            temporaryStoreFile << entry.epochSeconds << "\t" << sanitizeField(entry.action) << "\t"
                << sanitizeField(entry.databaseFilePath) << "\t" << entry.sizeBeforeBytes << "\t"
                << entry.sizeAfterBytes << "\t" << sanitizeField(entry.backupFilePath) << "\t"
                << sanitizeField(entry.outcome) << "\t" << entry.exitCode << "\n";
        }
        if (!temporaryStoreFile.good()) {
            return false;
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporaryStorePath, storePath, renameError);
    if (renameError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporaryStorePath, cleanupError);
        return false;
    }
    return true;
}
}
string AuditLog::storeFilePath() {
    if (storePathWasOverridden) {
        return overrideStoreFilePath;
    }
    return PlatformMetadata::programDataDirectory() + "/agent-db-doctor/audit.tsv";
}
void AuditLog::setStorePathForTests(const string& storeFilePath) {
    overrideStoreFilePath=storeFilePath;
    storePathWasOverridden=true;
}
bool AuditLog::append(const AuditEntry& entry) {
    const string storePath=storeFilePath();
    vector<AuditEntry> entries=readAllEntries(storePath);
    entries.push_back(entry);
    // The pruning window is measured from the new entry's own timestamp rather
    // than from the wall clock, so a log written by a machine whose clock was
    // wrong is pruned against its own history rather than against today.
    const long long oldestKeptSecond=entry.epochSeconds
        - (static_cast<long long>(RETENTION_DAYS) * 24 * 60 * 60);
    vector<AuditEntry> keptEntries;
    keptEntries.reserve(entries.size());
    for (const AuditEntry& existingEntry : entries) {
        if (existingEntry.epochSeconds>=oldestKeptSecond) {
            keptEntries.push_back(existingEntry);
        }
    }
    // If the cap is still exceeded, the oldest lines go: the newest entries are
    // the ones an investigating user needs.
    if (keptEntries.size()>MAXIMUM_LINE_COUNT) {
        keptEntries.erase(
            keptEntries.begin(),
            keptEntries.begin() + static_cast<ptrdiff_t>(keptEntries.size() - MAXIMUM_LINE_COUNT)
        );
    }
    return writeAllEntries(storePath, keptEntries);
}
vector<AuditEntry> AuditLog::readAll() {
    return readAllEntries(storeFilePath());
}
}
