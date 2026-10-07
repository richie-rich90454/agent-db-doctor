// The dashboard screen: agents, databases, detail, sessions, and the log.
//
// The screen reads a copy of the background snapshot and draws four regions:
// the agent browser on the left, the database list and detail in the middle,
// and the console log along the bottom. Every number comes from the same
// diagnosis reports the command line prints, so the two front ends never
// disagree.
#ifndef AGENT_DB_DOCTOR_GUI_DASHBOARDSCREEN_HPP
#define AGENT_DB_DOCTOR_GUI_DASHBOARDSCREEN_HPP
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the shared state the screen reads.
#include "GuiState.hpp"
// Provides the AgentDescriptor value type for the agent browser.
#include "AgentDbDoctor/Agents/AgentRegistry.hpp"
namespace AgentDbDoctor::Gui {
// Draws the whole dashboard: agent browser, database list, detail view,
// session browser, and log console. Call once per frame; the snapshot copy
// is taken under the lock and released before anything draws.
void renderDashboard(
    GuiState& state,
    const std::vector<AgentDbDoctor::Agents::AgentDescriptor>& agents);
}
#endif
