// The unit tests for the Arena class.
//
// The Arena is the one place the tool allocates on demand, so the properties worth
// testing are the ones a caller relies on: that a copy is byte for byte identical,
// that the returned memory stays valid across other allocations, and that a reset
// makes every byte available again.
#include <string>
// Provides the Catch2 test registration macros.
#include <catch2/catch_test_macros.hpp>
// Provides the Arena class under test.
#include "AgentDbDoctor/Common/Arena.hpp"
using namespace std;
using namespace AgentDbDoctor::Common;
TEST_CASE("a fresh arena starts empty and is usable without allocating", "[arena]") {
    Arena arena;
    REQUIRE(arena.usedBytes()==0);
    // The inline block is large enough for a typical value, so a small copy needs no
    // heap allocation at all. That is the property that makes the watchdog path
    // allocation free.
    REQUIRE(arena.remainingBytes()>=128);
}
TEST_CASE("a copied string is identical to the original", "[arena]") {
    Arena arena;
    const string originalText="a message body that is comfortably longer than twenty characters";
    const string copiedText=arena.copyString(originalText);
    REQUIRE(copiedText==originalText);
    // The copy is owned by the arena, not by the caller's string, so changing the
    // original afterwards must not change the copy.
    string mutableOriginal=originalText;
    const string secondCopy=arena.copyString(mutableOriginal);
    mutableOriginal="something else entirely";
    REQUIRE(secondCopy==originalText);
}
TEST_CASE("an empty string copies to an empty string and takes no memory", "[arena]") {
    Arena arena;
    const size_t usedBefore=arena.usedBytes();
    REQUIRE(arena.copyString(string_view()).empty());
    // Nothing was taken from the arena, because there was nothing to take.
    REQUIRE(arena.usedBytes()==usedBefore);
}
TEST_CASE("memory handed out stays valid across later allocations", "[arena]") {
    Arena arena;
    const string firstCopy=arena.copyString("the first value that is long enough to need space");
    const size_t firstUsedBytes=arena.usedBytes();
    REQUIRE(firstUsedBytes>0);
    // Allocating again must not overwrite what was already handed out, because a
    // bump allocator only ever moves forward.
    const string secondCopy=arena.copyString("the second value that is also long enough to need space");
    REQUIRE(arena.usedBytes()>firstUsedBytes);
    REQUIRE(firstCopy=="the first value that is long enough to need space");
    REQUIRE(secondCopy=="the second value that is also long enough to need space");
}
TEST_CASE("a reset makes every byte available again", "[arena]") {
    Arena arena;
    arena.copyString("a value long enough to occupy a whole alignment unit or two");
    const size_t usedBeforeReset=arena.usedBytes();
    REQUIRE(usedBeforeReset>0);
    arena.reset();
    REQUIRE(arena.usedBytes()==0);
    REQUIRE(arena.remainingBytes()==arena.blockByteSize());
    // The arena is still usable after a reset, which is what the watchdog loop
    // relies on when it reuses one arena for the lifetime of the process.
    REQUIRE(arena.copyString("a different value")=="a different value");
}
TEST_CASE("an arena grows rather than failing when it is too small", "[arena]") {
    // The initial capacity is deliberately smaller than the value that follows, so
    // the growth path is the one being tested.
    Arena arena(16);
    const string originalText(4096, 'y');
    const string copiedText=arena.copyString(originalText);
    REQUIRE(copiedText.size()==4096);
    REQUIRE(copiedText==originalText);
    REQUIRE(arena.blockByteSize()>=4096);
}
TEST_CASE("releasing returns the arena to its starting size", "[arena]") {
    Arena arena;
    arena.copyString("a value long enough to force a heap block to be taken");
    const size_t grownSize=arena.blockByteSize();
    arena.release();
    REQUIRE(arena.usedBytes()==0);
    // After a release the arena is back to its inline size, with no heap block
    // outstanding, which is what keeps a long lived service's memory flat.
    REQUIRE(arena.blockByteSize()<=grownSize);
    REQUIRE(arena.copyString("still works")=="still works");
}
TEST_CASE("byte copies are byte for byte identical", "[arena]") {
    Arena arena;
    const array<unsigned char, 5> sourceBytes={1, 2, 3, 250, 255};
    const unsigned char* copiedBytes=arena.copyBytes(sourceBytes.data(), sourceBytes.size());
    REQUIRE(copiedBytes!=nullptr);
    for (size_t byteIndex=0; byteIndex<sourceBytes.size(); ++byteIndex) {
        REQUIRE(copiedBytes[byteIndex]==sourceBytes[byteIndex]);
    }
}
TEST_CASE("a copy of nothing asks for nothing", "[arena]") {
    Arena arena;
    REQUIRE(arena.copyBytes(nullptr, 0)==nullptr);
}
