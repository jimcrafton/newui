// reflectgen_sizeof_probe - prints sizeof() for every newui::reflection
// registry class, straight from whatever include/newui/reflection.h
// *currently* says - not a set of numbers hardcoded once and left to
// drift. reflectgen.py's own memory-footprint estimate
// (`--sizeof-probe <this exe>`, see reflectgen.py's
// estimate_memory_footprint()) runs this and parses its stdout; it's
// also a plain standalone tool - build and run it directly for the
// current numbers with no Python involved.
//
// Only needs sizeof() - no constructor is ever actually called - so this
// deliberately doesn't link against the `newui` library at all
// (reflection.h needs nothing beyond its own includes for a type to be
// *complete*, which is all sizeof() requires; see its own header comment).
//
// Output: one "Name=bytes" line per class, in a fixed, machine-parseable
// order - add a new registry class to reflection.h, add its line here to
// keep the estimate honest.

#include <newui/reflection.h>

#include <cstdio>
#include <vector>

using namespace newui::reflection;

namespace {
    // A minimal stand-in SourceT/ValueT for every template below - the
    // *shape* of these classes (which is all sizeof() measures) doesn't
    // depend on which concrete types they're instantiated with, only on
    // how many/which member fields (function pointers, std::function,
    // std::string, ...) each holds.
    struct Probe {
        int x;
    };
}

int main() {
    std::printf("Class=%zu\n", sizeof(Class));
    std::printf("Property=%zu\n", sizeof(TypedProperty<Probe, int>));
    // A collection-shaped Property (either auto-detected from a
    // std::vector<T>/std::array<T,N>/std::map<K,V>-returning getter, or
    // built explicitly via ClassBuilder::propertyCollection()) is a
    // TypedPropertyCollection, not a TypedProperty - real real member-
    // count difference (four extra std::function-shaped backing modes -
    // see its own class comment), worth its own probed size rather than
    // reusing Property's.
    std::printf("PropertyCollection=%zu\n", sizeof(TypedPropertyCollection<Probe, std::vector<int>>));
    std::printf("Field=%zu\n", sizeof(TypedField<int>));
    // A collection-shaped Field (a private/public std::vector<T>/etc.
    // member with no accessor methods - e.g. unittests/test_reflection.cpp's
    // Gadget::numbers_) is a TypedFieldCollection - only one MemberPtr
    // more than TypedMemberField's own single member, but probed
    // separately anyway rather than assumed equal.
    std::printf("FieldCollection=%zu\n", sizeof(TypedFieldCollection<Probe, std::vector<int>>));
    std::printf("Method=%zu\n", sizeof(TypedMethod<Probe, int>));
    std::printf("Delegate=%zu\n", sizeof(TypedDelegate<Probe>));
    std::printf("Constructor=%zu\n", sizeof(TypedConstructor<Probe>));
    std::printf("Enum=%zu\n", sizeof(Enum));
    std::printf("EnumValue=%zu\n", sizeof(EnumValue));
    std::printf("Argument=%zu\n", sizeof(Argument));
    return 0;
}
