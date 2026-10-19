// The implementation of the JsonValue reader.
//
// The reader is a recursive descent parser written by hand. It is deliberately
// small and strict: it accepts exactly one document per call, rejects trailing
// text, and refuses to nest deeper than a fixed limit, because the text it reads
// arrives from a child process and a window that stops drawing is worse than a
// line that does not parse.
//
// Numbers are kept as 64 bit integers when the document wrote them that way. The
// events carry byte counts and nanosecond counts that a double holds only
// approximately, and a size that is off by a few bytes reads as a bug in the
// tool rather than in the reader.
// Provides `uint32_t`, the type the code point escapes are decoded into.
#include <cstdint>
// Provides `from_chars`, which converts a run of digits without a locale.
#include <charconv>
// Provides `system_error` for the error code `from_chars` reports.
#include <system_error>
// Provides `string`, which owns and stores a sequence of characters.
#include <string>
// Provides `pair`, which names each member of an object.
#include <utility>
// Provides the JsonValue class declaration.
#include "Json.hpp"
using namespace std;
namespace AgentDbDoctor::Gui {
// How deep a document may nest. Two levels are all the command line's own events
// ever use, and the limit is what keeps a line of ten thousand opening brackets
// from running the window out of stack.
static constexpr int MAXIMUM_NESTING_DEPTH=64;
// The character that stands in for a code point that cannot be decoded, which is
// what an unpaired surrogate half becomes.
static constexpr unsigned int REPLACEMENT_CHARACTER=0xFFFD;
JsonValue::Kind JsonValue::kind() const {
    return valueKind;
}
bool JsonValue::is(Kind wantedKind) const {
    return valueKind==wantedKind;
}
bool JsonValue::booleanValue() const {
    return valueKind==Kind::Boolean ? valueBoolean : false;
}
long long JsonValue::integerValue() const {
    if (valueKind!=Kind::Number) {
        return 0;
    }
    if (valueNumberIsIntegral) {
        return valueInteger;
    }
    // A fractional number is rounded toward zero, which is what the header
    // promises and what a caller asking for an integer expects.
    return static_cast<long long>(valueNumber);
}
const string& JsonValue::textValue() const {
    static const string emptyText;
    return valueKind==Kind::Text ? valueText : emptyText;
}
const vector<JsonValue>& JsonValue::elements() const {
    static const vector<JsonValue> noElements;
    return valueKind==Kind::Array ? valueElements : noElements;
}
const vector<pair<string, JsonValue>>& JsonValue::members() const {
    static const vector<pair<string, JsonValue>> noMembers;
    return valueKind==Kind::Object ? valueMembers : noMembers;
}
const JsonValue* JsonValue::member(const string& memberName) const {
    if (valueKind!=Kind::Object) {
        return nullptr;
    }
    for (const pair<string, JsonValue>& memberEntry : valueMembers) {
        if (memberEntry.first==memberName) {
            return &memberEntry.second;
        }
    }
    return nullptr;
}
string JsonValue::memberText(const string& memberName, const string& fallback) const {
    const JsonValue* memberValue=member(memberName);
    if (memberValue==nullptr || !memberValue->is(Kind::Text)) {
        return fallback;
    }
    return memberValue->textValue();
}
long long JsonValue::memberInteger(const string& memberName, long long fallback) const {
    const JsonValue* memberValue=member(memberName);
    if (memberValue==nullptr || !memberValue->is(Kind::Number)) {
        return fallback;
    }
    return memberValue->integerValue();
}
bool JsonValue::memberBoolean(const string& memberName, bool fallback) const {
    const JsonValue* memberValue=member(memberName);
    if (memberValue==nullptr || !memberValue->is(Kind::Boolean)) {
        return fallback;
    }
    return memberValue->booleanValue();
}
bool JsonValue::parse(const string& text, JsonValue& valueOut) {
    // The reader is a local class rather than a second class in the header. That
    // keeps the building of values, which is the reader's whole job, beside the
    // value it builds, and it keeps the spelling of a value a private matter of
    // this file.
    struct Reader final {
        // The document being read.
        const string& text;
        // Where the reader is in it.
        size_t position=0;
        // Reads the document itself: one value followed by nothing but spaces.
        bool readDocument(JsonValue& documentOut) {
            skipWhitespace();
            JsonValue document;
            if (!readValue(document, 0)) {
                return false;
            }
            skipWhitespace();
            if (position!=text.size()) {
                return false;
            }
            documentOut=document;
            return true;
        }
    private:
        // True when the reader is past the end of the document.
        bool atEnd() const {
            return position>=text.size();
        }
        // The character under the reader, or the null character at the end.
        char current() const {
            return atEnd() ? '\0' : text[position];
        }
        // True when the character is one of the ten decimal digits.
        static bool isDigit(char candidate) {
            return candidate>='0' && candidate<='9';
        }
        // Skips spaces, tabs, and line breaks, the four characters JSON allows
        // between the parts of a document.
        void skipWhitespace() {
            while (!atEnd()) {
                const char currentCharacter=current();
                if (currentCharacter!=' ' && currentCharacter!='\t' && currentCharacter!='\n'
                    && currentCharacter!='\r') {
                    return;
                }
                position++;
            }
        }
        // Consumes the character when it is the one expected.
        bool consume(char expected) {
            if (current()!=expected) {
                return false;
            }
            position++;
            return true;
        }
        bool readValue(JsonValue& valueOut, int depth) {
            if (depth>MAXIMUM_NESTING_DEPTH) {
                return false;
            }
            switch (current()) {
                case '{': return readObject(valueOut, depth);
                case '[': return readArray(valueOut, depth);
                case '"': {
                    string parsedText;
                    if (!readString(parsedText)) {
                        return false;
                    }
                    valueOut.valueKind=JsonValue::Kind::Text;
                    valueOut.valueText=std::move(parsedText);
                    return true;
                }
                case 't': return readLiteral("true", valueOut);
                case 'f': return readLiteral("false", valueOut);
                case 'n': return readLiteral("null", valueOut);
                default: return readNumber(valueOut);
            }
        }
        bool readLiteral(const char* literalText, JsonValue& valueOut) {
            for (size_t literalIndex=0; literalText[literalIndex]!='\0'; ++literalIndex) {
                if (current()!=literalText[literalIndex]) {
                    return false;
                }
                position++;
            }
            if (literalText[0]=='t') {
                valueOut.valueKind=JsonValue::Kind::Boolean;
                valueOut.valueBoolean=true;
            } else if (literalText[0]=='f') {
                valueOut.valueKind=JsonValue::Kind::Boolean;
                valueOut.valueBoolean=false;
            } else {
                valueOut.valueKind=JsonValue::Kind::Null;
            }
            return true;
        }
        bool readObject(JsonValue& valueOut, int depth) {
            if (!consume('{')) {
                return false;
            }
            JsonValue object;
            object.valueKind=JsonValue::Kind::Object;
            skipWhitespace();
            if (consume('}')) {
                valueOut=std::move(object);
                return true;
            }
            while (true) {
                skipWhitespace();
                string memberName;
                if (!readString(memberName)) {
                    return false;
                }
                skipWhitespace();
                if (!consume(':')) {
                    return false;
                }
                skipWhitespace();
                JsonValue memberValue;
                if (!readValue(memberValue, depth + 1)) {
                    return false;
                }
                object.valueMembers.push_back(
                    pair<string, JsonValue>(std::move(memberName), std::move(memberValue)));
                skipWhitespace();
                if (consume(',')) {
                    continue;
                }
                if (consume('}')) {
                    valueOut=std::move(object);
                    return true;
                }
                return false;
            }
        }
        bool readArray(JsonValue& valueOut, int depth) {
            if (!consume('[')) {
                return false;
            }
            JsonValue array;
            array.valueKind=JsonValue::Kind::Array;
            skipWhitespace();
            if (consume(']')) {
                valueOut=std::move(array);
                return true;
            }
            while (true) {
                skipWhitespace();
                JsonValue elementValue;
                if (!readValue(elementValue, depth + 1)) {
                    return false;
                }
                array.valueElements.push_back(std::move(elementValue));
                skipWhitespace();
                if (consume(',')) {
                    continue;
                }
                if (consume(']')) {
                    valueOut=std::move(array);
                    return true;
                }
                return false;
            }
        }
        bool readString(string& textOut) {
            if (!consume('"')) {
                return false;
            }
            string result;
            while (!atEnd()) {
                const char currentCharacter=text[position++];
                if (currentCharacter=='"') {
                    textOut=std::move(result);
                    return true;
                }
                if (currentCharacter!='\\') {
                    // A control character written raw inside a string is not JSON.
                    // The tool never writes one, because its writer escapes them.
                    if (static_cast<unsigned char>(currentCharacter)<0x20) {
                        return false;
                    }
                    result.push_back(currentCharacter);
                    continue;
                }
                if (atEnd()) {
                    return false;
                }
                const char escapeCharacter=text[position++];
                switch (escapeCharacter) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': {
                        uint32_t codePoint=0;
                        if (!readHexQuad(codePoint)) {
                            return false;
                        }
                        // Characters beyond the basic plane travel as two escapes,
                        // a high half followed by a low half, because JSON allows
                        // only four hexadecimal digits per escape.
                        if (codePoint>=0xD800 && codePoint<=0xDBFF) {
                            const bool lowHalfFollows=position + 1<text.size()
                                && text[position]=='\\' && text[position + 1]=='u';
                            uint32_t lowHalf=0;
                            if (lowHalfFollows) {
                                position+=2;
                                if (!readHexQuad(lowHalf)) {
                                    return false;
                                }
                            }
                            if (lowHalf>=0xDC00 && lowHalf<=0xDFFF) {
                                codePoint=0x10000 + ((codePoint - 0xD800)<<10) + (lowHalf - 0xDC00);
                            } else {
                                codePoint=REPLACEMENT_CHARACTER;
                            }
                        } else if (codePoint>=0xDC00 && codePoint<=0xDFFF) {
                            codePoint=REPLACEMENT_CHARACTER;
                        }
                        appendUtf8(result, codePoint);
                        break;
                    }
                    default: return false;
                }
            }
            return false;
        }
        // Reads the four hexadecimal digits of a `\u` escape.
        bool readHexQuad(uint32_t& codePointOut) {
            uint32_t codePoint=0;
            for (int digitIndex=0; digitIndex<4; ++digitIndex) {
                if (atEnd()) {
                    return false;
                }
                const int digitValue=hexDigitValue(text[position]);
                if (digitValue<0) {
                    return false;
                }
                codePoint=codePoint * 16 + static_cast<uint32_t>(digitValue);
                position++;
            }
            codePointOut=codePoint;
            return true;
        }
        // The value of one hexadecimal digit, or minus one when it is not one.
        static int hexDigitValue(char candidate) {
            if (candidate>='0' && candidate<='9') {
                return candidate - '0';
            }
            if (candidate>='a' && candidate<='f') {
                return candidate - 'a' + 10;
            }
            if (candidate>='A' && candidate<='F') {
                return candidate - 'A' + 10;
            }
            return -1;
        }
        // Writes one code point as the one to four bytes UTF-8 uses for it.
        static void appendUtf8(string& textOut, uint32_t codePoint) {
            if (codePoint<0x80) {
                textOut.push_back(static_cast<char>(codePoint));
                return;
            }
            if (codePoint<0x800) {
                textOut.push_back(static_cast<char>(0xC0 | (codePoint>>6)));
                textOut.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                return;
            }
            if (codePoint<0x10000) {
                textOut.push_back(static_cast<char>(0xE0 | (codePoint>>12)));
                textOut.push_back(static_cast<char>(0x80 | ((codePoint>>6) & 0x3F)));
                textOut.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
                return;
            }
            textOut.push_back(static_cast<char>(0xF0 | (codePoint>>18)));
            textOut.push_back(static_cast<char>(0x80 | ((codePoint>>12) & 0x3F)));
            textOut.push_back(static_cast<char>(0x80 | ((codePoint>>6) & 0x3F)));
            textOut.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        bool readNumber(JsonValue& valueOut) {
            const size_t numberStart=position;
            if (current()=='-') {
                position++;
            }
            if (!isDigit(current())) {
                return false;
            }
            // JSON forbids a leading zero, so "01" is a malformed number rather than
            // the number one written oddly. The command line never writes one; a
            // garbled line that does is refused rather than guessed at.
            const bool startsWithZero=current()=='0';
            position++;
            if (startsWithZero && isDigit(current())) {
                return false;
            }
            while (isDigit(current())) {
                position++;
            }
            bool isWrittenAsInteger=true;
            if (current()=='.') {
                isWrittenAsInteger=false;
                position++;
                if (!isDigit(current())) {
                    return false;
                }
                while (isDigit(current())) {
                    position++;
                }
            }
            if (current()=='e' || current()=='E') {
                isWrittenAsInteger=false;
                position++;
                if (current()=='+' || current()=='-') {
                    position++;
                }
                if (!isDigit(current())) {
                    return false;
                }
                while (isDigit(current())) {
                    position++;
                }
            }
            const string numberText=text.substr(numberStart, position - numberStart);
            valueOut.valueKind=JsonValue::Kind::Number;
            if (isWrittenAsInteger) {
                long long parsedInteger=0;
                const from_chars_result conversion=from_chars(
                    numberText.data(),
                    numberText.data() + numberText.size(),
                    parsedInteger
                );
                if (conversion.ec==errc() && conversion.ptr==numberText.data() + numberText.size()) {
                    valueOut.valueInteger=parsedInteger;
                    valueOut.valueNumberIsIntegral=true;
                    valueOut.valueNumber=static_cast<double>(parsedInteger);
                    return true;
                }
                // A count too large for 64 bits falls through to the approximate
                // reading below rather than failing the whole document: a number
                // nobody can act on is still better shown than dropped.
            }
            const double approximateValue=approximateNumberFromText(numberText);
            valueOut.valueInteger=static_cast<long long>(approximateValue);
            valueOut.valueNumberIsIntegral=false;
            valueOut.valueNumber=approximateValue;
            return true;
        }
        // Reads a number that has a fraction or an exponent by hand. `strtod` is
        // deliberately not used: it reads the decimal separator of the host's
        // locale, so on a machine that writes one and a half as "1,5" it would stop
        // parsing at the comma and silently report a different number.
        static double approximateNumberFromText(const string& numberText) {
            size_t textPosition=0;
            bool isNegative=false;
            if (textPosition<numberText.size() && numberText[textPosition]=='-') {
                isNegative=true;
                textPosition++;
            }
            double accumulatedValue=0.0;
            while (textPosition<numberText.size() && isDigit(numberText[textPosition])) {
                accumulatedValue=accumulatedValue * 10.0
                    + static_cast<double>(numberText[textPosition] - '0');
                textPosition++;
            }
            if (textPosition<numberText.size() && numberText[textPosition]=='.') {
                textPosition++;
                double placeValue=1.0;
                while (textPosition<numberText.size() && isDigit(numberText[textPosition])) {
                    placeValue*=10.0;
                    accumulatedValue+=
                        static_cast<double>(numberText[textPosition] - '0') / placeValue;
                    textPosition++;
                }
            }
            int exponent=0;
            bool isNegativeExponent=false;
            if (textPosition<numberText.size()
                && (numberText[textPosition]=='e' || numberText[textPosition]=='E')) {
                textPosition++;
                if (textPosition<numberText.size() && numberText[textPosition]=='+') {
                    textPosition++;
                } else if (textPosition<numberText.size() && numberText[textPosition]=='-') {
                    isNegativeExponent=true;
                    textPosition++;
                }
                while (textPosition<numberText.size() && isDigit(numberText[textPosition])) {
                    // The exponent is capped rather than trusted: ten thousand
                    // multiplications of a double reach infinity anyway, and the
                    // bound keeps a document full of digits from spinning here.
                    if (exponent<10000) {
                        exponent=exponent * 10 + (numberText[textPosition] - '0');
                    }
                    textPosition++;
                }
            }
            for (int exponentStep=0; exponentStep<exponent; ++exponentStep) {
                accumulatedValue=isNegativeExponent
                    ? accumulatedValue / 10.0
                    : accumulatedValue * 10.0;
            }
            return isNegative ? -accumulatedValue : accumulatedValue;
        }
    };
    Reader reader{text};
    return reader.readDocument(valueOut);
}
bool looksLikeJsonObject(const string& text) {
    size_t textStart=0;
    while (textStart<text.size()) {
        const char currentCharacter=text[textStart];
        if (currentCharacter!=' ' && currentCharacter!='\t' && currentCharacter!='\n'
            && currentCharacter!='\r') {
            break;
        }
        textStart++;
    }
    if (textStart>=text.size() || text[textStart]!='{') {
        return false;
    }
    size_t textEnd=text.size();
    while (textEnd>textStart) {
        const char currentCharacter=text[textEnd - 1];
        if (currentCharacter!=' ' && currentCharacter!='\t' && currentCharacter!='\n'
            && currentCharacter!='\r') {
            break;
        }
        textEnd--;
    }
    return textEnd>textStart && text[textEnd - 1]=='}';
}
}
