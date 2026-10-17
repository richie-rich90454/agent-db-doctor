// The implementation of the DiagnosisReport class.
//
// Every method is a trivial field access, which is exactly what a Java getter
// looks like. Keeping them out of the header means the compiler compiles them
// once into the library rather than in every file that includes the header.
// Provides `map`, an ordered dictionary keyed by a string.
#include <map>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiagnosisReport class and the IntegrityStatus enumeration.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
using namespace std;
using namespace AgentDbDoctor;
namespace AgentDbDoctor::Doctor {
DiagnosisReport::DiagnosisReport(string databaseFilePath)
    : databaseFilePath(std::move(databaseFilePath)) {
}
const string& DiagnosisReport::getDatabaseFilePath() const {
    return databaseFilePath;
}
void DiagnosisReport::setAgentName(string agentNameText) {
    agentName=std::move(agentNameText);
}
string DiagnosisReport::getAgentName() const {
    return agentName;
}
void DiagnosisReport::setFileExists(bool exists) {
    fileExists=exists;
}
bool DiagnosisReport::getFileExists() const {
    return fileExists;
}
void DiagnosisReport::setIsDirectory(bool isDirectoryPath) {
    isDirectory=isDirectoryPath;
}
bool DiagnosisReport::getIsDirectory() const {
    return isDirectory;
}
void DiagnosisReport::setLogicalSizeBytes(long long sizeBytes) {
    logicalSizeBytes=sizeBytes;
}
long long DiagnosisReport::getLogicalSizeBytes() const {
    return logicalSizeBytes;
}
void DiagnosisReport::setAllocatedSizeBytes(long long sizeBytes) {
    allocatedSizeBytes=sizeBytes;
}
long long DiagnosisReport::getAllocatedSizeBytes() const {
    return allocatedSizeBytes;
}
void DiagnosisReport::setWriteAheadLogSizeBytes(long long sizeBytes) {
    writeAheadLogSizeBytes=sizeBytes;
}
long long DiagnosisReport::getWriteAheadLogSizeBytes() const {
    return writeAheadLogSizeBytes;
}
void DiagnosisReport::setHasWriteAheadLog(bool hasLog) {
    hasWriteAheadLog=hasLog;
}
bool DiagnosisReport::getHasWriteAheadLog() const {
    return hasWriteAheadLog;
}
void DiagnosisReport::setModificationEpochMilliseconds(long long epochMilliseconds) {
    modificationEpochMilliseconds=epochMilliseconds;
}
long long DiagnosisReport::getModificationEpochMilliseconds() const {
    return modificationEpochMilliseconds;
}
void DiagnosisReport::setPageSizeBytes(long long sizeBytes) {
    pageSizeBytes=sizeBytes;
}
long long DiagnosisReport::getPageSizeBytes() const {
    return pageSizeBytes;
}
void DiagnosisReport::setPageCount(long long count) {
    pageCount=count;
}
long long DiagnosisReport::getPageCount() const {
    return pageCount;
}
void DiagnosisReport::setFreePageCount(long long count) {
    freePageCount=count;
}
long long DiagnosisReport::getFreePageCount() const {
    return freePageCount;
}
void DiagnosisReport::setReclaimableBytes(long long sizeBytes) {
    reclaimableBytes=sizeBytes;
}
long long DiagnosisReport::getReclaimableBytes() const {
    return reclaimableBytes;
}
void DiagnosisReport::setJournalMode(string mode) {
    journalMode=std::move(mode);
}
string DiagnosisReport::getJournalMode() const {
    return journalMode;
}
void DiagnosisReport::setAutoVacuumEnabled(bool isEnabled) {
    autoVacuumEnabled=isEnabled;
}
bool DiagnosisReport::getAutoVacuumEnabled() const {
    return autoVacuumEnabled;
}
void DiagnosisReport::setRowCount(const string& tableName, long long rowCount, bool isApproximate) {
    rowCounts[tableName]=rowCount;
    approximateRowCounts[tableName]=isApproximate;
}
long long DiagnosisReport::rowCount(const string& tableName) const {
    // `find` returns an iterator, and comparing it with `end` tests whether the key
    // was present. This is how a `map` reports a missing key without inserting a
    // default value, which is what `map::at` would do by throwing instead.
    const auto foundEntry=rowCounts.find(tableName);
    if (foundEntry==rowCounts.end()) {
        return 0;
    }
    return foundEntry->second;
}
bool DiagnosisReport::hasRowCount(const string& tableName) const {
    return rowCounts.find(tableName)!=rowCounts.end();
}
bool DiagnosisReport::isRowCountApproximate(const string& tableName) const {
    const auto foundEntry=approximateRowCounts.find(tableName);
    if (foundEntry==approximateRowCounts.end()) {
        return true;
    }
    return foundEntry->second;
}
vector<string> DiagnosisReport::countedTableNames() const {
    // The names are collected into a vector so the caller can iterate them without
    // being able to modify the map. A `map` iterates in sorted key order, which is
    // what makes two reports comparable line by line.
    vector<string> tableNames;
    tableNames.reserve(rowCounts.size());
    for (const auto& currentEntry : rowCounts) {
        tableNames.push_back(currentEntry.first);
    }
    return tableNames;
}
void DiagnosisReport::setRedundantEventRowCount(long long rowCount) {
    redundantEventRowCount=rowCount;
}
long long DiagnosisReport::getRedundantEventRowCount() const {
    return redundantEventRowCount;
}
void DiagnosisReport::setRedundantEventTypes(vector<string> typeNames) {
    redundantTypeNames=std::move(typeNames);
}
vector<string> DiagnosisReport::getRedundantEventTypes() const {
    return redundantTypeNames;
}
void DiagnosisReport::setSchemaGenerationDescription(string description) {
    schemaGenerationDescription=std::move(description);
}
string DiagnosisReport::getSchemaGenerationDescription() const {
    return schemaGenerationDescription;
}
void DiagnosisReport::setCompactionSupported(bool isSupported) {
    compactionSupported=isSupported;
}
bool DiagnosisReport::getCompactionSupported() const {
    return compactionSupported;
}
void DiagnosisReport::setIntegrityStatus(IntegrityStatus status) {
    integrityStatus=status;
}
IntegrityStatus DiagnosisReport::getIntegrityStatus() const {
    return integrityStatus;
}
void DiagnosisReport::setIntegrityMessage(string message) {
    integrityMessage=std::move(message);
}
string DiagnosisReport::getIntegrityMessage() const {
    return integrityMessage;
}
void DiagnosisReport::setOpenFailureCode(int sqliteResultCode) {
    openFailureCode=sqliteResultCode;
}
int DiagnosisReport::getOpenFailureCode() const {
    return openFailureCode;
}
void DiagnosisReport::addWarning(string warning) {
    warningMessages.push_back(std::move(warning));
}
vector<string> DiagnosisReport::getWarnings() const {
    return warningMessages;
}
void DiagnosisReport::addSuggestedNextStep(string suggestedStep) {
    nextStepMessages.push_back(std::move(suggestedStep));
}
vector<string> DiagnosisReport::getSuggestedNextSteps() const {
    return nextStepMessages;
}
void DiagnosisReport::setDurationNanoseconds(long long nanoseconds) {
    durationNanoseconds=nanoseconds;
}
long long DiagnosisReport::getDurationNanoseconds() const {
    return durationNanoseconds;
}
void DiagnosisReport::setMetadataCallCount(long long callCount) {
    metadataCallCount=callCount;
}
long long DiagnosisReport::getMetadataCallCount() const {
    return metadataCallCount;
}
void DiagnosisReport::setDatabaseOpenSucceeded(bool succeeded) {
    databaseOpenSucceeded=succeeded;
}
bool DiagnosisReport::getDatabaseOpenSucceeded() const {
    return databaseOpenSucceeded;
}
bool DiagnosisReport::isHealthy() const {
    if (!fileExists) {
        return false;
    }
    if (!databaseOpenSucceeded) {
        return false;
    }
    if (integrityStatus!=IntegrityStatus::INTACT
        && integrityStatus!=IntegrityStatus::NOT_CHECKED) {
        return false;
    }
    return true;
}
}
