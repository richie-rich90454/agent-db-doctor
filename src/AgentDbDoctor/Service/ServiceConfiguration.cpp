// The implementation of the ServiceConfiguration class and the configuration file
// reader and writer.
//
// The JSON handling below is a small recursive descent parser and writer. It
// accepts exactly what the configuration format uses: an object whose values are
// numbers, strings, booleans, or arrays of strings. A dependency that can parse any
// JSON would be several thousand lines to read six fields, and the specification
// permits no JSON library.
// Provides `ifstream` and `ofstream`, which read and write text files.
#include <fstream>
// Provides `map`, the ordered dictionary the parsed object members go in.
#include <map>
// Provides `optional`, a container that may or may not hold a value.
#include <optional>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `vector`, a growable array of values.
#include <vector>
// Provides `from_chars`, the integer parser that allocates nothing, and `errc`,
// the error code it reports.
#include <charconv>
// Provides `istreambuf_iterator`, which reads a whole file into a string.
#include <iterator>
// Provides `create_directories`, used before the configuration file is written.
#include <filesystem>
// Provides the ServiceConfiguration class declaration.
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"
// Provides PlatformMetadata, for the program data directory.
#include "AgentDbDoctor/FileSystem/PlatformMetadata.hpp"
// Provides the ConfigurationException and the FileSystemException.
#include "AgentDbDoctor/Common/Exceptions.hpp"
using namespace std;
using namespace AgentDbDoctor;
using namespace AgentDbDoctor::Common;
using namespace AgentDbDoctor::FileSystem;
namespace AgentDbDoctor::Service {
// The fixed name of the configuration file, inside the platform's configuration
// directory.
static constexpr const char* CONFIGURATION_FILE_NAME="config.json";
// The name of the folder the log files go in, beside the configuration.
static constexpr const char* LOG_FOLDER_NAME="logs";
// The name of the service log file.
static constexpr const char* LOG_FILE_NAME="agent-db-doctor.log";
// The folder the tool keeps its own files in, inside the platform's configuration
// directory.
static constexpr const char* TOOL_FOLDER_NAME="agent-db-doctor";
// The JsonValue structure is one node of a parsed document.
//
// The `kind` field says which of the other fields is meaningful, which is how a
// tagged union is spelled out when the standard library's `variant` would be more
// machinery than the job needs.
struct JsonValue final {
    // One of `object`, `array`, `string`, `number`, `boolean`, or `empty`.
    string kind;
    // The members of an object, keyed by name. A `map` keeps them in name order, so
    // the order they appeared in the file does not matter.
    map<string, JsonValue> members;
    // The elements of an array.
    vector<JsonValue> elements;
    // The text of a string, or the digits of a number.
    string text;
    // The value of a boolean.
    bool booleanValue=false;
};
// The deepest nesting the parser accepts. A configuration file is a flat object of
// scalars and short lists, so anything past this is either a mistake or an attempt
// to exhaust the stack of a process that may be running as an administrator.
static constexpr int MAXIMUM_NESTING_DEPTH=32;
// The JsonParser class turns configuration text into a JsonValue tree.
//
// The class is `final` because the specification forbids inheritance. Its only
// member is the text being parsed and a cursor into it, so it holds no state
// between calls and does not need to be created by a caller.
class JsonParser final {
public:
    // The parser is created only by the static parse method, which builds one for
    // the length of a single call. The deleted default constructor stops anyone else
    // creating one.
    JsonParser()=delete;
    // Parses `text` and returns the root value. Throws `ConfigurationException`
    // naming the line when the text is not valid, because a user who typed a
    // missing comma deserves to be told where it is.
    static JsonValue parse(const string& text);
private:
    // Builds a parser over `text`. The text is held by reference, so parsing a
    // file does not copy it, and the parser is created for the length of a single
    // call by the static `parse` method above.
    explicit JsonParser(const string& text);
    // Parses one value at the cursor, skipping whitespace first.
    JsonValue parseValue();
    // Parses a brace delimited object.
    JsonValue parseObject();
    // Parses a square bracket delimited array.
    JsonValue parseArray();
    // Parses a quoted string, handling the backslash escapes a Windows path can
    // contain.
    JsonValue parseString();
    // Parses a number, a boolean, or the word null.
    JsonValue parseBareWord();
    // Skips spaces, tabs, carriage returns, and newlines, counting the newlines so
    // that error messages can name a line.
    void skipWhitespace();
    // Reads the character at the cursor without consuming it. Returns a null
    // character at the end of the text.
    char peek() const;
    // Consumes and returns the character at the cursor.
    char take();
    // True when the character at the cursor is the one given.
    bool matches(char expectedCharacter) const;
    // Fails with a sentence naming the line and what was wrong. The `noreturn`
    // attribute tells the compiler that control does not come back, which stops it
    // warning about a missing return on every call.
    [[noreturn]] void fail(const string& whatWasWrong) const;
    // The whole text being parsed. It is held by reference so that reading a file
    // does not copy it.
    const string& text;
    // How far through the text the cursor is.
    size_t cursor=0;
    // How many newlines have been passed, which is the current line number.
    int currentLine=1;
    // How deep the value nest currently is. Every object and array adds one, and the
    // parser refuses documents past `MAXIMUM_NESTING_DEPTH` rather than recursing
    // until the stack runs out. A configuration file that arrives from somewhere
    // other than the user's own editor must not be able to end the service.
    int nestingDepth=0;
};
JsonParser::JsonParser(const string& text)
    : text(text) {
}
void JsonParser::fail(const string& whatWasWrong) const {
    throw ConfigurationException(
        string("The configuration file could not be read. On line ")
        + to_string(currentLine) + ": " + whatWasWrong
    );
}
char JsonParser::peek() const {
    if (cursor>=text.size()) {
        return '\0';
    }
    return text[cursor];
}
char JsonParser::take() {
    if (cursor>=text.size()) {
        fail("the file ends in the middle of a value.");
    }
    const char currentCharacter=text[cursor];
    cursor++;
    if (currentCharacter=='\n') {
        currentLine++;
    }
    return currentCharacter;
}
bool JsonParser::matches(char expectedCharacter) const {
    return peek()==expectedCharacter;
}
void JsonParser::skipWhitespace() {
    while (cursor<text.size()) {
        const char currentCharacter=text[cursor];
        if (currentCharacter==' ' || currentCharacter=='\t' || currentCharacter=='\r') {
            cursor++;
            continue;
        }
        if (currentCharacter=='\n') {
            currentLine++;
            cursor++;
            continue;
        }
        break;
    }
}
JsonValue JsonParser::parseString() {
    JsonValue value;
    value.kind="string";
    // The opening quote has already been checked by the caller.
    take();
    string collectedText;
    while (true) {
        if (cursor>=text.size()) {
            fail("a string was opened with a quote and never closed.");
        }
        const char currentCharacter=take();
        if (currentCharacter=='"') {
            break;
        }
        if (currentCharacter!='\\') {
            collectedText.push_back(currentCharacter);
            continue;
        }
        // A backslash introduces an escape. Only the two that a Windows path needs
        // are handled; anything else is taken literally, which is more forgiving
        // than rejecting a file that a person could obviously read.
        if (cursor>=text.size()) {
            fail("a backslash was the last character in the file.");
        }
        const char escapedCharacter=take();
        if (escapedCharacter=='n') {
            collectedText.push_back('\n');
        } else if (escapedCharacter=='t') {
            collectedText.push_back('\t');
        } else {
            collectedText.push_back(escapedCharacter);
        }
    }
    value.text=std::move(collectedText);
    return value;
}
JsonValue JsonParser::parseBareWord() {
    JsonValue value;
    string collectedText;
    while (cursor<text.size()) {
        const char currentCharacter=text[cursor];
        // A bare word ends at anything that cannot be part of one, which is any
        // whitespace, a comma, or a closing bracket.
        if (currentCharacter==' ' || currentCharacter=='\t' || currentCharacter=='\n'
            || currentCharacter=='\r' || currentCharacter==',' || currentCharacter==']'
            || currentCharacter=='}') {
            break;
        }
        collectedText.push_back(currentCharacter);
        cursor++;
    }
    if (collectedText=="true" || collectedText=="false") {
        value.kind="boolean";
        value.booleanValue=collectedText=="true";
        return value;
    }
    if (collectedText=="null") {
        value.kind="empty";
        return value;
    }
    // Anything left over has to be a number. The check is that it is not empty and
    // that every character is a digit, a minus, or a decimal point, which is enough
    // for the byte counts and intervals this format holds.
    bool looksLikeNumber=!collectedText.empty();
    for (const char currentCharacter : collectedText) {
        if (currentCharacter!='-' && currentCharacter!='.'
            && (currentCharacter<'0' || currentCharacter>'9')) {
            looksLikeNumber=false;
            break;
        }
    }
    if (!looksLikeNumber) {
        fail("the value " + collectedText + " is not a number, a string, or a true or false value.");
    }
    value.kind="number";
    value.text=collectedText;
    return value;
}
JsonValue JsonParser::parseObject() {
    JsonValue value;
    value.kind="object";
    take();
    skipWhitespace();
    if (matches('}')) {
        take();
        return value;
    }
    while (true) {
        skipWhitespace();
        if (!matches('"')) {
            fail("a member name was expected, and it must be in quotes.");
        }
        const JsonValue memberName=parseString();
        skipWhitespace();
        if (!matches(':')) {
            fail("a colon was expected after the member name " + memberName.text + ".");
        }
        take();
        skipWhitespace();
        const JsonValue memberValue=parseValue();
        value.members[memberName.text]=std::move(memberValue);
        skipWhitespace();
        if (matches(',')) {
            take();
            continue;
        }
        if (matches('}')) {
            take();
            break;
        }
        fail("a comma or a closing brace was expected after a value.");
    }
    return value;
}
JsonValue JsonParser::parseArray() {
    JsonValue value;
    value.kind="array";
    take();
    skipWhitespace();
    if (matches(']')) {
        take();
        return value;
    }
    while (true) {
        skipWhitespace();
        value.elements.push_back(parseValue());
        skipWhitespace();
        if (matches(',')) {
            take();
            continue;
        }
        if (matches(']')) {
            take();
            break;
        }
        fail("a comma or a closing bracket was expected after a value.");
    }
    return value;
}
JsonValue JsonParser::parseValue() {
    skipWhitespace();
    if (cursor>=text.size()) {
        fail("a value was expected but the file ended.");
    }
    if (matches('{')) {
        if (nestingDepth>=MAXIMUM_NESTING_DEPTH) {
            fail("the configuration nests deeper than any real setting does, so it was refused.");
        }
        nestingDepth++;
        JsonValue objectValue=parseObject();
        nestingDepth--;
        return objectValue;
    }
    if (matches('[')) {
        if (nestingDepth>=MAXIMUM_NESTING_DEPTH) {
            fail("the configuration nests deeper than any real setting does, so it was refused.");
        }
        nestingDepth++;
        JsonValue arrayValue=parseArray();
        nestingDepth--;
        return arrayValue;
    }
    if (matches('"')) {
        return parseString();
    }
    return parseBareWord();
}
JsonValue JsonParser::parse(const string& text) {
    // Holding the text by reference means it is not copied.
    JsonParser parser(text);
    JsonValue rootValue=parser.parseValue();
    parser.skipWhitespace();
    if (parser.cursor<text.size()) {
        parser.fail("there is more text after the end of the configuration object.");
    }
    return rootValue;
}
string ServiceConfiguration::describeLocations() const {
    return string("Configuration: ") + configurationFilePath
        + ". Log: " + logFilePath + ".";
}
ServiceConfiguration ServiceConfiguration::createDefault() {
    ServiceConfiguration configuration;
    configuration.configurationFilePath=ConfigurationFile::defaultFilePath();
    configuration.logFilePath=ConfigurationFile::defaultLogFilePath();
    // The watched list is left empty on purpose. An empty list means every supported
    // agent, which is the right default, because the whole job of the tool is to
    // notice problems the user has not thought to look for.
    return configuration;
}
bool ServiceConfiguration::isValid(string& explanationOut) const {
    if (warningThresholdBytes<=0) {
        explanationOut=string("The warning threshold must be a positive number of bytes, because zero ")
            + "would mean the service complained about every database on every tick.";
        return false;
    }
    if (criticalThresholdBytes<warningThresholdBytes) {
        explanationOut=string("The critical threshold is lower than the warning threshold, which would mean ")
            + "the warning never appears. The critical threshold should be the larger of the two.";
        return false;
    }
    if (checkIntervalSeconds<10) {
        explanationOut=string("The check interval must be at least ten seconds. Anything more often wastes ")
            + "power and fills the log without telling you anything new.";
        return false;
    }
    // A week is the largest interval that still means something. A larger value is a
    // typo, and a very large one used to overflow the sleep arithmetic and turn the
    // service into a processor spin instead of a watcher.
    static constexpr int MAXIMUM_CHECK_INTERVAL_SECONDS=7*24*60*60;
    if (checkIntervalSeconds>MAXIMUM_CHECK_INTERVAL_SECONDS) {
        explanationOut=string("The check interval must be at most seven days. A larger number is "
            "almost certainly a typo, and the service refuses to guess what was meant.");
        return false;
    }
    return true;
}
string ConfigurationFile::defaultDirectoryPath() {
    return PlatformMetadata::programDataDirectory() + "/" + TOOL_FOLDER_NAME;
}
string ConfigurationFile::defaultFilePath() {
    return defaultDirectoryPath() + "/" + CONFIGURATION_FILE_NAME;
}
string ConfigurationFile::defaultLogFilePath() {
    return defaultDirectoryPath() + "/" + LOG_FOLDER_NAME + "/" + LOG_FILE_NAME;
}
string ConfigurationFile::renderAsJson(const ServiceConfiguration& configuration) {
    // The file is written by hand rather than by a library so that its layout stays
    // predictable and readable to the person who has to edit it.
    string text=string("{\n");
    text+="  \"watchedDatabases\": [";
    for (size_t pathIndex=0; pathIndex<configuration.watchedDatabasePaths.size(); ++pathIndex) {
        if (pathIndex>0) {
            text+=", ";
        }
        // The quotes and backslashes inside a path are escaped, because a Windows
        // path contains both and an unescaped quote would end the string early.
        string escapedPath;
        for (const char currentCharacter : configuration.watchedDatabasePaths[pathIndex]) {
            if (currentCharacter=='"' || currentCharacter=='\\') {
                escapedPath.push_back('\\');
            }
            escapedPath.push_back(currentCharacter);
        }
        text+="\"" + escapedPath + "\"";
    }
    text+="],\n";
    text+="  \"warningThresholdBytes\": " + to_string(configuration.warningThresholdBytes) + ",\n";
    text+="  \"criticalThresholdBytes\": " + to_string(configuration.criticalThresholdBytes) + ",\n";
    text+="  \"checkIntervalSeconds\": " + to_string(configuration.checkIntervalSeconds) + ",\n";
    text+="  \"allowAutomaticCompaction\": ";
    text+=configuration.allowAutomaticCompaction ? "true" : "false";
    text+="\n}\n";
    return text;
}
void ConfigurationFile::write(const ServiceConfiguration& configuration, const string& filePath) {
    // The parent directory is created first, because a fresh install has no folder
    // to write into and the user should not have to make one by hand.
    const size_t lastSeparator=filePath.find_last_of("/\\");
    if (lastSeparator!=string::npos) {
        const string parentDirectory=filePath.substr(0, lastSeparator);
        std::error_code ignoredError;
        std::filesystem::create_directories(parentDirectory, ignoredError);
    }
    std::ofstream outputFile(filePath, std::ios::trunc);
    if (!outputFile.is_open()) {
        // A failure here is almost always a permission problem, and the message says
        // so rather than naming an error number.
        throw FileSystemException(
            string("The configuration file could not be written"),
            filePath,
            5
        );
    }
    // The stream is flushed and closed by its own destructor when this function
    // returns, so nothing else has to be done.
    outputFile << renderAsJson(configuration);
}
optional<ServiceConfiguration> ConfigurationFile::read(const string& filePath) {
    std::ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        // A missing file is a normal first run rather than a mistake, so no value is
        // returned and no exception is thrown. A malformed one is a mistake, and the
        // parser reports it with a line number.
        return nullopt;
    }
    // Reading the whole file into one string is right here: the file holds six
    // fields, and the parser needs random access to it.
    string fileText(
        (istreambuf_iterator<char>(inputFile)),
        istreambuf_iterator<char>()
    );
    const JsonValue rootValue=JsonParser::parse(fileText);
    if (rootValue.kind!="object") {
        throw ConfigurationException(
            string("The configuration file must contain an object in braces, and this one begins "
                "with something else.")
        );
    }
    ServiceConfiguration configuration;
    configuration.configurationFilePath=filePath;
    configuration.logFilePath=defaultLogFilePath();
    // Each field is read only if it is present. A file with one field in it is
    // valid, and the missing ones keep their defaults, so a user can set just the
    // threshold they care about.
    const auto findMember=[&rootValue](const string& memberName) -> const JsonValue* {
        const auto foundMember=rootValue.members.find(memberName);
        if (foundMember==rootValue.members.end()) {
            return nullptr;
        }
        return &foundMember->second;
    };
    const JsonValue* watchedDatabases=findMember("watchedDatabases");
    if (watchedDatabases!=nullptr) {
        if (watchedDatabases->kind!="array") {
            throw ConfigurationException(
                string("The setting watchedDatabases must be a list of paths in square brackets.")
            );
        }
        for (const JsonValue& element : watchedDatabases->elements) {
            if (element.kind=="string") {
                configuration.watchedDatabasePaths.push_back(element.text);
            }
        }
    }
    const JsonValue* warningThreshold=findMember("warningThresholdBytes");
    if (warningThreshold!=nullptr && warningThreshold->kind=="number") {
        // `from_chars` is the standard library's integer parser that allocates
        // nothing and does not consult the locale, so a number in the file always
        // reads the same way on every machine.
        long long parsedValue=0;
        const auto parseResult=from_chars(
            warningThreshold->text.data(),
            warningThreshold->text.data() + warningThreshold->text.size(),
            parsedValue
        );
        if (parseResult.ec==errc{}) {
            configuration.warningThresholdBytes=parsedValue;
        }
    }
    const JsonValue* criticalThreshold=findMember("criticalThresholdBytes");
    if (criticalThreshold!=nullptr && criticalThreshold->kind=="number") {
        long long parsedValue=0;
        const auto parseResult=from_chars(
            criticalThreshold->text.data(),
            criticalThreshold->text.data() + criticalThreshold->text.size(),
            parsedValue
        );
        if (parseResult.ec==errc{}) {
            configuration.criticalThresholdBytes=parsedValue;
        }
    }
    const JsonValue* checkInterval=findMember("checkIntervalSeconds");
    if (checkInterval!=nullptr && checkInterval->kind=="number") {
        long long parsedValue=0;
        const auto parseResult=from_chars(
            checkInterval->text.data(),
            checkInterval->text.data() + checkInterval->text.size(),
            parsedValue
        );
        if (parseResult.ec==errc{}) {
            configuration.checkIntervalSeconds=static_cast<int>(parsedValue);
        }
    }
    const JsonValue* allowCompaction=findMember("allowAutomaticCompaction");
    if (allowCompaction!=nullptr && allowCompaction->kind=="boolean") {
        configuration.allowAutomaticCompaction=allowCompaction->booleanValue;
    }
    string validationExplanation;
    if (!configuration.isValid(validationExplanation)) {
        throw ConfigurationException(validationExplanation);
    }
    return configuration;
}
}
