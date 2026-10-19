// The Json class, a small reader for the machine readable output the command line
// program produces.
//
// The window is a view of the command line program: it collects the arguments,
// starts the real binary, and reads the JSON objects the binary writes. That is
// what keeps the two front ends from ever disagreeing about a number, and it is why
// this parser exists rather than a second implementation of the diagnostics.
//
// It parses rather than scans deliberately. A regular expression that pulls fields
// out of these objects would break the first time a path contained a brace, and
// paths are exactly the kind of text that does.
#ifndef AGENT_DB_DOCTOR_GUI_JSON_HPP
#define AGENT_DB_DOCTOR_GUI_JSON_HPP
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `pair`, which names each member of an object.
#include <utility>
// Provides `vector`, a growable array of values.
#include <vector>
using namespace std;
namespace AgentDbDoctor::Gui {
// The JsonValue class holds one parsed JSON value.
//
// The class is `final` because the specification forbids inheritance. It is a
// value: copyable, comparable by its accessors, and built only by the parser.
class JsonValue final {
public:
    // The kind of value. A value that was never parsed is `Null`.
    enum class Kind {
        Null,
        Boolean,
        Number,
        Text,
        Array,
        Object
    };
    JsonValue()=default;
    // The kind of this value.
    Kind kind() const;
    // True when this value exists and is of the given kind.
    bool is(Kind wantedKind) const;
    // The boolean, or false when this value is not a boolean.
    bool booleanValue() const;
    // The number as a 64 bit integer, rounded toward zero. Zero when this value is
    // not a number.
    long long integerValue() const;
    // The text, or the empty string when this value is not text.
    const string& textValue() const;
    // The elements, or an empty vector when this value is not an array.
    const vector<JsonValue>& elements() const;
    // The members in the order the document listed them, or an empty vector when
    // this value is not an object. Used where the member names are not known in
    // advance, which is the case for a `result` event's field list and for any
    // event a newer command line adds.
    const vector<pair<string, JsonValue>>& members() const;
    // The member with the given name, or nullptr when this value is not an object or
    // has no such member.
    const JsonValue* member(const string& memberName) const;
    // The text of a member, or `fallback` when the member is absent or not text.
    string memberText(const string& memberName, const string& fallback) const;
    // The number of a member, or `fallback` when the member is absent or not a
    // number.
    long long memberInteger(const string& memberName, long long fallback) const;
    // The boolean of a member, or `fallback` when the member is absent or not a
    // boolean.
    bool memberBoolean(const string& memberName, bool fallback) const;
    // Parses `text` as one JSON document. Returns false when the text is not one,
    // in which case `valueOut` is left untouched.
    static bool parse(const string& text, JsonValue& valueOut);
private:
    Kind valueKind=Kind::Null;
    bool valueBoolean=false;
    double valueNumber=0.0;
    // The number as an exact 64 bit integer when the document wrote it as one, so a
    // byte count larger than a double holds exactly is never rounded. The flag says
    // whether this slot is in use rather than the double above it.
    long long valueInteger=0;
    // True when `valueInteger` holds the number exactly.
    bool valueNumberIsIntegral=false;
    string valueText;
    vector<JsonValue> valueElements;
    vector<pair<string, JsonValue>> valueMembers;
};
// True when `text` looks like a JSON object, which is how the reader decides
// between an event and a line of prose in the merged output stream.
bool looksLikeJsonObject(const string& text);
}
#endif
