// The implementation of the SchemaFingerprint class.
//
// The fingerprint is a 64 bit FNV-1a hash rather than `std::hash`, because
// `std::hash` is allowed to differ between runs and between standard libraries, and
// a fingerprint that changes when nothing else did would produce a warning on every
// run. FNV-1a is ten lines, stable forever, and good enough for noticing that a
// schema changed.
// Provides `ifstream` and `ofstream`, used for the store file.
#include <fstream>
// Provides `map`, used to hold the whole store while it is rewritten.
#include <map>
// Provides `rename` and `remove`, used to replace the store atomically.
#include <filesystem>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the SchemaFingerprint class declaration.
#include "AgentDbDoctor/Doctor/SchemaFingerprint.hpp"
// Provides PlatformMetadata, for the shared configuration directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Doctor {
namespace {
// The offset basis and prime of FNV-1a, the standard 64 bit values.
constexpr unsigned long long FNV_OFFSET_BASIS=14695981039346656037ULL;
constexpr unsigned long long FNV_PRIME=1099511628211ULL;
// Mixes one byte into the hash.
unsigned long long mixByte(unsigned long long hashValue, char currentCharacter) {
    hashValue^=static_cast<unsigned long long>(static_cast<unsigned char>(currentCharacter));
    hashValue*=FNV_PRIME;
    return hashValue;
}
// Mixes a string into the hash.
unsigned long long mixText(unsigned long long hashValue, const string& text) {
    for (const char currentCharacter : text) {
        hashValue=mixByte(hashValue, currentCharacter);
    }
    // A separator is mixed in so that "ab" and "a" + "b" cannot collide.
    return mixByte(hashValue, '\n');
}
// The store path is overridden by the tests before anything reads or writes it.
string overrideStoreFilePath;
// True when the override above has been set.
bool storePathWasOverridden=false;
}
string SchemaFingerprint::storeFilePath() {
    if (storePathWasOverridden) {
        return overrideStoreFilePath;
    }
    // The store lives beside the service configuration, because the watchdog is what
    // writes it and a user running `diagnose` only reads it.
    return PlatformMetadata::programDataDirectory() + "/agent-db-doctor/schema-fingerprints.tsv";
}
void SchemaFingerprint::setStorePathForTests(const string& storeFilePath) {
    overrideStoreFilePath=storeFilePath;
    storePathWasOverridden=true;
}
string SchemaFingerprint::fingerprintOf(Database& database) {
    // The schema is read in one small query. Ordering it makes the fingerprint
    // independent of the order SQLite happens to return rows in.
    Statement schemaStatement(
        database.rawHandle(),
        "SELECT type, name, COALESCE(sql, '') FROM sqlite_master ORDER BY type, name",
        "the schema of the database"
    );
    if (!schemaStatement.isValid()) {
        return string();
    }
    unsigned long long hashValue=FNV_OFFSET_BASIS;
    while (schemaStatement.step()) {
        hashValue=mixText(hashValue, schemaStatement.columnText(0));
        hashValue=mixText(hashValue, schemaStatement.columnText(1));
        hashValue=mixText(hashValue, schemaStatement.columnText(2));
    }
    // The step loop above ends on either "done" or an error. An error means the
    // schema could not be read, and the empty string says so rather than producing a
    // fingerprint of half a schema.
    const int finalStepResultCode=schemaStatement.lastStepResultCode();
    if (finalStepResultCode!=SQLITE_DONE && finalStepResultCode!=SQLITE_ROW) {
        return string();
    }
    // The hash is rendered as sixteen hexadecimal digits, which is stable across
    // platforms and readable in the store file.
    static constexpr const char* HEX_DIGITS="0123456789abcdef";
    string renderedFingerprint;
    renderedFingerprint.reserve(16);
    for (int digitIndex=15; digitIndex>=0; --digitIndex) {
        renderedFingerprint.push_back(HEX_DIGITS[(hashValue >> (digitIndex * 4)) & 0xFULL]);
    }
    return renderedFingerprint;
}
optional<string> SchemaFingerprint::storedFingerprintFor(const string& databaseFilePath) {
    std::ifstream storeFile(storeFilePath());
    if (!storeFile.is_open()) {
        // A store that does not exist yet is the normal state before the service has
        // run once, not an error.
        return nullopt;
    }
    string line;
    while (getline(storeFile, line)) {
        const size_t separatorPosition=line.find('\t');
        if (separatorPosition==string::npos) {
            continue;
        }
        if (line.substr(0, separatorPosition)==databaseFilePath) {
            return line.substr(separatorPosition + 1);
        }
    }
    return nullopt;
}
bool SchemaFingerprint::storeFingerprint(const string& databaseFilePath, const string& fingerprint) {
    if (fingerprint.empty()) {
        return false;
    }
    // The whole store is read, one line is replaced, and the file is written back.
    // The store is at most a few hundred short lines, so this is cheaper than it
    // sounds and needs no separate index.
    map<string, string> storedFingerprints;
    {
        std::ifstream storeFile(storeFilePath());
        string line;
        while (getline(storeFile, line)) {
            const size_t separatorPosition=line.find('\t');
            if (separatorPosition==string::npos) {
                continue;
            }
            storedFingerprints[line.substr(0, separatorPosition)]=line.substr(separatorPosition + 1);
        }
    }
    storedFingerprints[databaseFilePath]=fingerprint;
    const string storePath=storeFilePath();
    const std::filesystem::path storeDirectory=std::filesystem::path(storePath).parent_path();
    std::error_code directoryError;
    std::filesystem::create_directories(storeDirectory, directoryError);
    if (directoryError) {
        return false;
    }
    // The store is written beside the real one and renamed into place, so a reader
    // never sees a half written file.
    const string temporaryStorePath=storePath + ".writing";
    {
        std::ofstream temporaryStoreFile(temporaryStorePath, std::ios::trunc);
        if (!temporaryStoreFile.is_open()) {
            return false;
        }
        for (const auto& storedEntry : storedFingerprints) {
            temporaryStoreFile << storedEntry.first << "\t" << storedEntry.second << "\n";
        }
        if (!temporaryStoreFile.good()) {
            return false;
        }
    }
    std::error_code renameError;
    std::filesystem::rename(temporaryStorePath, storePath, renameError);
    if (renameError) {
        // A reader may hold the store open on Windows, in which case the rename
        // fails. The temporary file is removed and the caller reported as a failure,
        // which is all a fingerprint deserves.
        std::error_code cleanupError;
        std::filesystem::remove(temporaryStorePath, cleanupError);
        return false;
    }
    return true;
}
}
