// The implementation of the dashboard screen.
//
// Layout, left to right: the agent browser (every supported agent with its
// database count and total size), the database list for the selected agent,
// and the detail view for the selected database with its session browser.
// The console log runs along the bottom. The screen never opens a database:
// the background scan already did, and this file only reads its snapshot.
#include <algorithm>
// Provides `sort`, which orders the session browser.
#include <algorithm>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides the dashboard screen declaration.
#include "DashboardScreen.hpp"
// Provides Dear ImGui: every widget on this screen.
#include <imgui.h>
// Provides the number to text conversions the reports use.
#include "AgentDbDoctor/Presentation/HumanReadableFormatting.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Agents;
using namespace AgentDbDoctor::Doctor;
using namespace AgentDbDoctor::Presentation;
namespace AgentDbDoctor::Gui {
namespace {
// A health dot colour: green for sound, grey for a folder, amber for a
// problem, red for damage. The words beside it say the same thing, because
// colour alone is not a message.
ImVec4 healthColour(const DiagnosisReport& report) {
    if (report.getIsDirectory()) {
        return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
    }
    if (report.isHealthy()) {
        return ImVec4(0.20f, 0.65f, 0.30f, 1.0f);
    }
    const IntegrityStatus integrityStatus=report.getIntegrityStatus();
    if (integrityStatus==IntegrityStatus::STRUCTURALLY_DAMAGED
        || integrityStatus==IntegrityStatus::UNREADABLE) {
        return ImVec4(0.80f, 0.25f, 0.20f, 1.0f);
    }
    return ImVec4(0.85f, 0.60f, 0.15f, 1.0f);
}
string healthWords(const DiagnosisReport& report) {
    if (report.getIsDirectory()) {
        return string("Folder");
    }
    if (report.isHealthy()) {
        return string("Sound");
    }
    const IntegrityStatus integrityStatus=report.getIntegrityStatus();
    if (integrityStatus==IntegrityStatus::STRUCTURALLY_DAMAGED) {
        return string("Damaged");
    }
    if (integrityStatus==IntegrityStatus::UNREADABLE) {
        return string("Unreadable");
    }
    if (integrityStatus==IntegrityStatus::NOT_A_DATABASE) {
        return string("Not a database");
    }
    return string("Attention");
}
// Shortens a path to its last two fragments, so a hundred workspace rows fit
// the list. The full path is shown in the detail view and in the tooltip.
string shortPath(const string& filePath) {
    const size_t lastSeparator=filePath.find_last_of("/\\");
    if (lastSeparator==string::npos) {
        return filePath;
    }
    const size_t beforeLast=filePath.find_last_of("/\\", lastSeparator - 1);
    if (beforeLast==string::npos) {
        return filePath;
    }
    return string("...") + filePath.substr(beforeLast);
}
} // namespace
void renderDashboard(
    GuiState& state,
    const vector<AgentDescriptor>& agents,
    ActionRunner& runner,
    ActionUiState& ui) {
    // One lock per frame: copy everything the frame needs, then draw from the
    // copies. The worker never waits on the interface this way.
    vector<ScannedDatabase> databases;
    vector<string> logLines;
    string scanStatus;
    int selectedDatabaseIndex;
    string selectedAgentName;
    {
        lock_guard<mutex> snapshotLock(state.snapshotMutex);
        databases=state.databases;
        logLines=state.logLines;
        scanStatus=state.scanStatus;
        selectedDatabaseIndex=state.selectedDatabaseIndex;
        selectedAgentName=state.selectedAgentName;
    }
    long long selectedAgentBytes=0;
    int selectedAgentCount=0;
    const float windowHeight=ImGui::GetContentRegionAvail().y;
    const float logHeight=140.0f;
    const float mainHeight=windowHeight-logHeight-ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("Agents", ImVec2(300, mainHeight), true)) {
        ImGui::TextUnformatted("Agents");
        ImGui::Separator();
        if (ImGui::Selectable("Everything", selectedAgentName.empty())) {
            lock_guard<mutex> snapshotLock(state.snapshotMutex);
            state.selectedAgentName.clear();
            state.selectedDatabaseIndex=-1;
        }
        for (const AgentDescriptor& agent : agents) {
            int agentCount=0;
            long long agentBytes=0;
            for (const ScannedDatabase& scanned : databases) {
                if (scanned.found.agentName==agent.displayName) {
                    agentCount++;
                    agentBytes+=scanned.found.sizeBytes;
                }
            }
            const bool isSelected=selectedAgentName==agent.displayName;
            string agentLabel=agent.displayName + " (" + to_string(agentCount) + ")";
            if (ImGui::Selectable(agentLabel.c_str(), isSelected)) {
                lock_guard<mutex> snapshotLock(state.snapshotMutex);
                state.selectedAgentName=agent.displayName;
                state.selectedDatabaseIndex=-1;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\n%s",
                    agent.storageDescription.c_str(),
                    agentCount==0 ? "Nothing found on this machine."
                                  : HumanReadableFormatting::formatBytes(agentBytes).c_str());
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Databases", ImVec2(500, mainHeight), true)) {
        ImGui::TextUnformatted("Databases");
        ImGui::Separator();
        int visibleIndex=0;
        for (size_t databaseIndex=0; databaseIndex<databases.size(); ++databaseIndex) {
            const ScannedDatabase& scanned=databases[databaseIndex];
            if (!selectedAgentName.empty() && scanned.found.agentName!=selectedAgentName) {
                continue;
            }
            selectedAgentCount++;
            selectedAgentBytes+=scanned.found.sizeBytes;
            const ImVec4 dotColour=healthColour(scanned.report);
            ImGui::TextColored(dotColour, "%s", "\u25CF");
            ImGui::SameLine();
            const string rowLabel=scanned.found.agentName + "  " + shortPath(scanned.found.filePath);
            const bool isSelected=static_cast<int>(databaseIndex)==selectedDatabaseIndex;
            if (ImGui::Selectable(rowLabel.c_str(), isSelected)) {
                lock_guard<mutex> snapshotLock(state.snapshotMutex);
                state.selectedDatabaseIndex=static_cast<int>(databaseIndex);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", scanned.found.filePath.c_str());
            }
            visibleIndex++;
        }
        if (visibleIndex==0) {
            ImGui::TextDisabled(
                "%s", state.scanDone ? "Nothing found for this selection." : scanStatus.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("Detail", ImVec2(0, mainHeight), true)) {
        ImGui::TextUnformatted("Detail");
        ImGui::Separator();
        if (selectedDatabaseIndex>=0
            && static_cast<size_t>(selectedDatabaseIndex)<databases.size()) {
            const ScannedDatabase& scanned=databases[static_cast<size_t>(selectedDatabaseIndex)];
            const DiagnosisReport& report=scanned.report;
            ImGui::TextColored(healthColour(report), "%s", healthWords(report).c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(scanned.found.agentName.c_str());
            ImGui::TextWrapped("%s", scanned.found.filePath.c_str());
            ImGui::Separator();
            ImGui::Text(
                "Size: %s",
                HumanReadableFormatting::formatBytes(report.getLogicalSizeBytes()).c_str());
            if (report.getHasWriteAheadLog()) {
                ImGui::Text(
                    "Write ahead log: %s",
                    HumanReadableFormatting::formatBytes(report.getWriteAheadLogSizeBytes())
                        .c_str());
            }
            if (report.getPageCount()>0) {
                ImGui::Text(
                    "Pages: %s of %s bytes",
                    HumanReadableFormatting::formatCount(report.getPageCount()).c_str(),
                    HumanReadableFormatting::formatCount(report.getPageSizeBytes()).c_str());
                ImGui::Text(
                    "Unused pages: %s (%s)",
                    HumanReadableFormatting::formatCount(report.getFreePageCount()).c_str(),
                    HumanReadableFormatting::formatBytes(report.getReclaimableBytes()).c_str());
            }
            if (!report.getJournalMode().empty()) {
                ImGui::Text("Journal: %s", report.getJournalMode().c_str());
            }
            if (!report.getSchemaGenerationDescription().empty()) {
                ImGui::TextWrapped(
                    "Schema: %s", report.getSchemaGenerationDescription().c_str());
            }
            for (const string& tableName : report.countedTableNames()) {
                ImGui::Text(
                    "Rows in %s: %s%s",
                    tableName.c_str(),
                    HumanReadableFormatting::formatCount(report.rowCount(tableName)).c_str(),
                    report.isRowCountApproximate(tableName) ? " (approximate)" : "");
            }
            if (!report.getRedundantEventTypes().empty()) {
                ImGui::Text(
                    "Older copies: %s",
                    HumanReadableFormatting::formatCount(report.getRedundantEventRowCount())
                        .c_str());
            }
            if (report.getIntegrityStatus()!=IntegrityStatus::NOT_CHECKED
                && report.getIntegrityStatus()!=IntegrityStatus::INTACT) {
                ImGui::TextWrapped("Integrity: %s", report.getIntegrityMessage().c_str());
            }
            for (const string& warning : report.getWarnings()) {
                ImGui::TextWrapped("Warning: %s", warning.c_str());
            }
            if (ImGui::CollapsingHeader("Suggested next steps")) {
                for (const string& nextStep : report.getSuggestedNextSteps()) {
                    ImGui::BulletText("%s", nextStep.c_str());
                }
            }
            if (ImGui::CollapsingHeader("Actions", ImGuiTreeNodeFlags_DefaultOpen)) {
                renderDatabaseActions(scanned, runner, ui);
            }
            if (!scanned.newestSessions.empty()
                && ImGui::CollapsingHeader("Newest sessions")) {
                static int sortColumn=2;
                static bool sortDescending=true;
                vector<SessionRow> sessionRows=scanned.newestSessions;
                if (ImGui::BeginTable(
                        "Sessions",
                        3,
                        ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg
                            | ImGuiTableFlags_ScrollY,
                        ImVec2(0, 220))) {
                    ImGui::TableSetupColumn("Session", ImGuiTableColumnFlags_DefaultSort);
                    ImGui::TableSetupColumn("Title");
                    ImGui::TableSetupColumn("Changed");
                    ImGui::TableHeadersRow();
                    if (ImGuiTableSortSpecs* sortSpecs=ImGui::TableGetSortSpecs()) {
                        if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount>0) {
                            sortColumn=sortSpecs->Specs[0].ColumnIndex;
                            sortDescending=sortSpecs->Specs[0].SortDirection
                                == ImGuiSortDirection_Descending;
                            sortSpecs->SpecsDirty=false;
                        }
                    }
                    sort(
                        sessionRows.begin(),
                        sessionRows.end(),
                        [](const SessionRow& left, const SessionRow& right) {
                            if (sortColumn==1) {
                                return sortDescending ? left.sessionTitle>right.sessionTitle
                                                      : left.sessionTitle<right.sessionTitle;
                            }
                            if (sortColumn==2) {
                                return sortDescending ? left.epochSeconds>right.epochSeconds
                                                      : left.epochSeconds<right.epochSeconds;
                            }
                            return sortDescending
                                ? left.sessionIdentifier>right.sessionIdentifier
                                : left.sessionIdentifier<right.sessionIdentifier;
                        });
                    for (const SessionRow& row : sessionRows) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(row.sessionIdentifier.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextWrapped("%s", row.sessionTitle.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text(
                            "%s",
                            row.epochSeconds>0
                                ? HumanReadableFormatting::formatTimestamp(
                                      row.epochSeconds * 1000LL)
                                      .c_str()
                                : "unknown");
                    }
                    ImGui::EndTable();
                }
            }
        } else {
            ImGui::TextDisabled(
                "%s",
                databases.empty()
                    ? (state.scanDone ? "No databases found." : scanStatus.c_str())
                    : "Select a database to inspect it.");
        }
    }
    ImGui::EndChild();
    if (ImGui::BeginChild("Log", ImVec2(0, 0), true)) {
        ImGui::Text(
            "%s  |  %d shown of %d agents  |  %s",
            scanStatus.c_str(),
            selectedAgentCount,
            static_cast<int>(agents.size()),
            HumanReadableFormatting::formatBytes(selectedAgentBytes).c_str());
        ImGui::Separator();
        ImGui::BeginChild("LogLines", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const string& line : logLines) {
            ImGui::TextUnformatted(line.c_str());
        }
        if (!logLines.empty()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();
}
}
