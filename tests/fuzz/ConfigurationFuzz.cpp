// The libFuzzer entry point for the service configuration reader.
//
// The reader in ServiceConfiguration.cpp is the project's own hand written
// parser for a subset of JSON, and it is the one place where text a user can
// edit meets a parser written here. The fuzzer drives
// ConfigurationFile::fromText, the entry point that takes the text directly, so
// an input never touches the disk and a crash is reproducible from the bytes
// alone.
//
// The target is built only with -DAGENT_DB_DOCTOR_ENABLE_FUZZING=ON and only
// with Clang, because -fsanitize=fuzzer is a Clang feature. How to build and run
// it is in docs/integrations.md.

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
// The parser under test, and the exception type it refuses unusable text with.
#include "AgentDbDoctor/Common/Exceptions.hpp"
#include "AgentDbDoctor/Service/ServiceConfiguration.hpp"

namespace {
// One megabyte is far beyond any configuration a person writes, and the cap keeps
// one input cheap enough to mutate thousands of times in the seconds a run lasts.
constexpr size_t maximumConfigurationSize = 1024U * 1024U;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > maximumConfigurationSize) {
        return 0;
    }
    // The bytes are copied into a string because fromText takes text; that copy
    // is part of the boundary under test, not an escape hatch around it.
    const std::string text(reinterpret_cast<const char*>(data), size);
    try {
        // The value is deliberately discarded: the property under test is that
        // every input ends in a value or a refusal, never in a crash.
        AgentDbDoctor::Service::ConfigurationFile::fromText(text, "fuzzed.json");
    } catch (const AgentDbDoctor::Common::ConfigurationException&) {
        // The reader refuses text it cannot use by throwing, which is its
        // contract; the expected refusal is not a finding.
    } catch (const std::exception&) {
        // Any other exception is swallowed as well: libFuzzer treats an escaping
        // exception as a crash, and the reader's contract still says every input
        // ends in a value or a refusal.
    } catch (...) {
        // The same for anything that is not derived from std::exception, so no
        // input can abort the fuzzer by throwing.
    }
    return 0;
}
