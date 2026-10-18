// The implementation of the HistoryStore class.
//
// Every write reads the store, decides whether the line is new, and rewrites the
// whole file. That sounds wasteful and is not: the store is capped at twenty
// thousand short lines, the service writes at most once an hour per changed
// database, and a rewrite is what makes both the duplicate suppression and the
// pruning fall out of the same pass. The rewrite goes through a temporary file and
// a rename, so a reader never sees half a store.
// Provides `ifstream` and `ofstream`, used to read and write the store.
#include <fstream>
// Provides `map`, used to hold the whole store while it is rewritten.
#include <map>
// Provides `rename` and `remove`, used to replace the store atomically.
#include <filesystem>
// Provides the HistoryStore class declaration.
#include "AgentDbDoctor/Service/HistoryStore.hpp"
// Provides PlatformMetadata, for the shared configuration directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
using namespace std;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Service {
namespace {
// One entry in the store: the path, the time, and the size. The order in the file
// is path, time, size, and every line for a path is in time order because that is
// the order the service writes them.
struct StoreEntry final {
    string databaseFilePath;
    long long epochSeconds=0;
    long long sizeBytes=0;
};
// The store path is overridden by the tests before anything reads or writes it.
string overrideStoreFilePath;
// True when the override above has been set.
bool storePathWasOverridden=false;
// Reads every entry the store holds, in file order.
vector<StoreEntry> readAllEntries(const string& storePath) {
    vector<StoreEntry> entries;
    std::ifstream storeFile(storePath);
    if (!storeFile.is_open()) {
        return entries;
    }
    string line;
    while (getline(storeFile, line)) {
        const size_t firstTab=line.find('\t');
        if (firstTab==string::npos) {
            continue;
        }
        const size_t secondTab=line.find('\t', firstTab + 1);
        if (secondTab==string::npos) {
            continue;
        }
        StoreEntry entry;
        entry.databaseFilePath=line.substr(0, firstTab);
        entry.epochSeconds=stoll(line.substr(firstTab + 1, secondTab - firstTab - 1));
        entry.sizeBytes=stoll(line.substr(secondTab + 1));
        entries.push_back(std::move(entry));
    }
    return entries;
}
// Writes the entries back through a temporary file and a rename.
bool writeAllEntries(const string& storePath, const vector<StoreEntry>& entries) {
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
        for (const StoreEntry& entry : entries) {
            temporaryStoreFile << entry.databaseFilePath << "\t" << entry.epochSeconds << "\t"
                << entry.sizeBytes << "\n";
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
string HistoryStore::storeFilePath() {
    if (storePathWasOverridden) {
        return overrideStoreFilePath;
    }
    return PlatformMetadata::programDataDirectory() + "/agent-db-doctor/history.tsv";
}
void HistoryStore::setStorePathForTests(const string& storeFilePath) {
    overrideStoreFilePath=storeFilePath;
    storePathWasOverridden=true;
}
bool HistoryStore::recordSize(
    const string& databaseFilePath,
    long long sizeBytes,
    long long epochSeconds
) {
    const string storePath=storeFilePath();
    vector<StoreEntry> entries=readAllEntries(storePath);
    // The last entry for this database decides whether the new size is news. Only
    // the last one matters: the sizes in between are already recorded.
    const StoreEntry* lastEntryForThisPath=nullptr;
    for (const StoreEntry& entry : entries) {
        if (entry.databaseFilePath==databaseFilePath) {
            lastEntryForThisPath=&entry;
        }
    }
    if (lastEntryForThisPath!=nullptr && lastEntryForThisPath->sizeBytes==sizeBytes) {
        // The size did not change, so there is nothing to record. This is what keeps
        // an idle machine's history a handful of lines rather than thousands.
        return false;
    }
    entries.push_back(StoreEntry{databaseFilePath, epochSeconds, sizeBytes});
    // The pruning pass drops entries older than the retention window, and if the
    // store is still larger than the cap the oldest remaining lines are dropped.
    const long long oldestKeptSecond=epochSeconds - (static_cast<long long>(RETENTION_DAYS) * 24 * 60 * 60);
    vector<StoreEntry> keptEntries;
    keptEntries.reserve(entries.size());
    for (const StoreEntry& entry : entries) {
        if (entry.epochSeconds>=oldestKeptSecond) {
            keptEntries.push_back(entry);
        }
    }
    if (keptEntries.size()>MAXIMUM_LINE_COUNT) {
        keptEntries.erase(
            keptEntries.begin(),
            keptEntries.begin() + static_cast<ptrdiff_t>(keptEntries.size() - MAXIMUM_LINE_COUNT)
        );
    }
    return writeAllEntries(storePath, keptEntries);
}
vector<HistoryPoint> HistoryStore::pointsFor(const string& databaseFilePath, int maximumPoints) {
    vector<HistoryPoint> points;
    for (const StoreEntry& entry : readAllEntries(storeFilePath())) {
        if (entry.databaseFilePath==databaseFilePath) {
            points.push_back(HistoryPoint{entry.epochSeconds, entry.sizeBytes});
        }
    }
    if (maximumPoints>0 && points.size()>static_cast<size_t>(maximumPoints)) {
        points.erase(
            points.begin(),
            points.begin() + static_cast<ptrdiff_t>(points.size() - static_cast<size_t>(maximumPoints))
        );
    }
    return points;
}
vector<string> HistoryStore::knownDatabasePaths() {
    vector<string> paths;
    for (const StoreEntry& entry : readAllEntries(storeFilePath())) {
        bool isAlreadyKnown=false;
        for (const string& knownPath : paths) {
            if (knownPath==entry.databaseFilePath) {
                isAlreadyKnown=true;
                break;
            }
        }
        if (!isAlreadyKnown) {
            paths.push_back(entry.databaseFilePath);
        }
    }
    return paths;
}
}
