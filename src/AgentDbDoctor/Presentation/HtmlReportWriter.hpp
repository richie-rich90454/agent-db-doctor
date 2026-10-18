// The HtmlReportWriter class, which turns a diagnostic pass into one shareable page.
//
// Every number a diagnose run produces is already in the report objects; this class
// puts them into a single standalone HTML file with no scripts, no stylesheets to
// fetch, and no network access, so it opens anywhere and can be attached to an
// issue or a message. Every value read from a database is escaped, because a file
// path or a warning is text from the outside world.
#ifndef AGENT_DB_DOCTOR_PRESENTATION_HTMLREPORTWRITER_HPP
#define AGENT_DB_DOCTOR_PRESENTATION_HTMLREPORTWRITER_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides the DiagnosisReport value type.
#include "AgentDbDoctor/Doctor/DiagnosisReport.hpp"
using namespace std;
using namespace AgentDbDoctor::Doctor;
namespace AgentDbDoctor::Presentation {
// The HtmlReportWriter class writes one page for a whole diagnostic pass.
//
// The class is `final` because the specification forbids inheritance. All of its
// methods are `static`, so there is never an instance to create.
class HtmlReportWriter final {
public:
    HtmlReportWriter()=delete;
    // Writes the page for `reports` to `reportFilePath`. Returns false when the file
    // cannot be written, which the caller reports; the diagnostics themselves are
    // never affected by a report that cannot be saved.
    static bool write(
        const string& reportFilePath,
        const vector<DiagnosisReport>& reports,
        const string& toolVersion
    );
};
}
#endif
