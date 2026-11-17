// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Reflection: the visitor over every field type that exists (int, float, bool, std::string, StringID, EntityReference,
// lists through VectorAccessor), actions, tags, the nested-struct conventions, the catch-all reflect, and which field
// types do not exist (start plan section 4 row 9). Headers: Reflection/Visitor.hpp, Core/ComponentRegistry.hpp,
// Core/CoreComponents.hpp, Concurrency/StreamComponent.hpp, Concurrency/QueueComponent.hpp.

#include "BrezelTest.hpp"
#include "TestComponents.hpp"

#include "Brezel/Concurrency/QueueComponent.hpp"
#include "Brezel/Concurrency/StreamComponent.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace Brezel;
using namespace BrezelTest;

namespace {

std::string tagString(std::initializer_list<Tag> tags) {
    std::string out;
    for (Tag t : tags) {
        switch (t) {
            case Tag::Persistent: out += "P"; break;
            case Tag::Load_Critical: out += "C"; break;
            case Tag::Load_Warn: out += "W"; break;
            case Tag::Hidden: out += "H"; break;
            case Tag::ReadOnly: out += "R"; break;
            case Tag::Networked: out += "N"; break;
            case Tag::CommandStack: out += "U"; break;
        }
    }
    return out;
}

/// Records every call as "<kind> <label> <tags>"; walks list elements itself, labelled E0, E1, ...
struct Recorder : ComponentVisitor {
    std::vector<std::string> log;
    void add(const char* kind, StringID label, std::initializer_list<Tag> tags) {
        log.push_back(std::string(kind) + " " + label.toString() + " " + tagString(tags));
    }
    void visit_property(StringID l, int&, std::initializer_list<Tag> t) override { add("int", l, t); }
    void visit_property(StringID l, float&, std::initializer_list<Tag> t) override { add("float", l, t); }
    void visit_property(StringID l, bool&, std::initializer_list<Tag> t) override { add("bool", l, t); }
    void visit_property(StringID l, std::string&, std::initializer_list<Tag> t) override { add("string", l, t); }
    void visit_property(StringID l, StringID&, std::initializer_list<Tag> t) override { add("sid", l, t); }
    void visit_property(StringID l, EntityReference&, std::initializer_list<Tag> t) override { add("ref", l, t); }
    void visit_property(StringID l, VectorAccessorBase& va, std::initializer_list<Tag> t) override {
        log.push_back("list " + l.toString() + " " + tagString(t) + " size " + std::to_string(va.size()));
        for (size_t i = 0; i < va.size(); ++i) {
            va.visit_element(i, StringID::from("E" + std::to_string(i)), *this, t);
        }
    }
    void visit_action(StringID l, std::function<void()> fn) override {
        log.push_back("action " + l.toString());
        actions.push_back(std::move(fn));
    }
    bool beginComponent(StringID name) override {
        log.push_back("begin " + name.toString());
        return true;
    }
    void endComponent() override { log.push_back("end"); }
    std::vector<std::function<void()>> actions;
};

/// Writes fixed values through the references a reflect hands out
struct Writer : ComponentVisitor {
    using ComponentVisitor::visit_property;  // reflect is a template on the visitor type: hidden overloads would not compile
    void visit_property(StringID, int& v, std::initializer_list<Tag>) override { v = 42; }
    void visit_property(StringID, float& v, std::initializer_list<Tag>) override { v = 4.5f; }
    void visit_property(StringID, bool& v, std::initializer_list<Tag>) override { v = false; }
    void visit_property(StringID, std::string& v, std::initializer_list<Tag>) override { v = "written"; }
};

/// A component with an action
struct Counter {
    int value{3};
};
template <typename V>
void reflect(Counter& c, V& v) {
    v.visit_property("value", c.value, {Tag::Persistent, Tag::ReadOnly, Tag::Hidden});
    v.visit_action("reset", [&c] { c.value = 0; });
}

enum class Mode { A, B };

/// Whether ComponentVisitor has a visit_property overload that takes a T& (a field type that exists)
template <typename T>
constexpr bool canVisit = requires(ComponentVisitor& v, T& field) { v.visit_property(StringID{}, field); };

struct StreamPayload {
    int sample{0};
};

} // namespace

BREZEL_TEST(visitor_every_field_type, "Reflection/Visitor.hpp",
            "a reflect hands the visitor every field in declaration order with its tags: int, float, bool, string, "
            "StringID, EntityReference and lists of plain values, strings and structs; list elements of plain types "
            "carry the list's tags") {
    Position p{1.0f, 2.0f, 3};
    Label l{"t", true, StringID::from("k"), "n"};
    Link k;
    Lists lists{{1, 2}, {"w"}, {Inner{3, 4.5f}}};
    Recorder r;
    reflect(p, r);
    reflect(l, r);
    reflect(k, r);
    reflect(lists, r);
    const std::vector<std::string> expected = {
        "float x PN", "float y PN", "int layer P",
        "string text PN", "bool visible PN", "sid kind P", "string note ",
        "ref target PN",
        "list numbers PN size 2", "int E0 PN", "int E1 PN",
        "list words PN size 1", "string E0 PN",
        "list items PN size 1", "int a PN", "float b PN",
    };
    CHECK((r.log == expected));
    if (r.log != expected) {
        for (const auto& line : r.log) std::printf("    got: %s\n", line.c_str());
    }
}

BREZEL_TEST(visitor_writes_through_references, "Reflection/Visitor.hpp",
            "visit_property gets a mutable reference to the field: a visitor that writes changes the component (the "
            "same reflect drives saving and loading)") {
    Position p;
    Label l;
    Writer w;
    reflect(p, w);
    reflect(l, w);
    CHECK_EQ(p.x, 4.5f);
    CHECK_EQ(p.layer, 42);
    CHECK_EQ(l.text, std::string("written"));
    CHECK(!l.visible);
    CHECK_EQ(l.note, std::string("written"));  // untagged fields are visited too; the visitor decides
}

BREZEL_TEST(visitor_actions_and_defaults, "Reflection/Visitor.hpp",
            "visit_action hands out a callable that acts on the component; the base ComponentVisitor ignores every "
            "property and action and begins every component and list") {
    Counter c;
    Recorder r;
    reflect(c, r);
    CHECK((r.log == std::vector<std::string>{"int value PRH", "action reset"}));
    REQUIRE(r.actions.size() == 1);
    r.actions[0]();
    CHECK_EQ(c.value, 0);
    ComponentVisitor base;
    Position p{1.0f, 2.0f, 3};
    reflect(p, base);
    reflect(c, base);
    CHECK_EQ(p.layer, 3);
    CHECK(base.beginComponent(StringID::from("Position")));
    CHECK(base.beginList(StringID::from("numbers"), 3));
}

BREZEL_TEST(visitor_field_types_that_exist, "Reflection/Visitor.hpp",
            "the field types are int, float, bool, std::string, StringID, EntityReference and lists "
            "(VectorAccessorBase); double, 64-bit and unsigned integers, enums, fixed arrays and structs have no "
            "overload (start plan section 4 row 9)") {
    static_assert(canVisit<int>);
    static_assert(canVisit<float>);
    static_assert(canVisit<bool>);
    static_assert(canVisit<std::string>);
    static_assert(canVisit<StringID>);
    static_assert(canVisit<EntityReference>);
    static_assert(canVisit<VectorAccessor<int>>);
    static_assert(!canVisit<double>);
    static_assert(!canVisit<int64_t>);
    static_assert(!canVisit<uint64_t>);
    static_assert(!canVisit<uint32_t>);
    static_assert(!canVisit<int16_t>);
    static_assert(!canVisit<char>);
    static_assert(!canVisit<Mode>);
    static_assert(!canVisit<std::array<float, 3>>);
    static_assert(!canVisit<Inner>);
    static_assert(!canVisit<UUID>);
    CHECK(true);  // the checks above are compile-time
}

BREZEL_TEST(vector_accessor_resize_resets_values, "Reflection/Visitor.hpp",
            "VectorAccessor::resize clears the vector before resizing: every element comes back default, even the "
            "ones that fit") {
    std::vector<int> values{1, 2, 3};
    VectorAccessor<int> va(values);
    CHECK_EQ(va.size(), size_t(3));
    va.resize(2);
    CHECK((values == std::vector<int>{0, 0}));
    va.resize(4);
    CHECK((values == std::vector<int>{0, 0, 0, 0}));
}

BREZEL_TEST(vector_accessor_struct_elements, "Reflection/Visitor.hpp",
            "a list element that is not a plain type is reflected through its member reflect(visitor), not the free "
            "function; the element label and the list's tags are not passed to it") {
    std::vector<Inner> items{Inner{1, 1.5f}, Inner{2, 2.5f}};
    VectorAccessor<Inner> va(items);
    Recorder r;
    va.visit_element(1, StringID::from("E1"), r, {Tag::Hidden});
    CHECK((r.log == std::vector<std::string>{"int a PN", "float b PN"}));
    Writer w;
    va.visit_element(0, StringID::from("E0"), w);
    CHECK_EQ(items[0].a, 42);
    CHECK_EQ(items[1].a, 2);
}

BREZEL_TEST(nested_struct_is_flattened, "Reflection/Visitor.hpp",
            "ComponentVisitor has no bracket for a struct inside a component: a nested struct's fields are visited "
            "as fields of the component, with their own labels") {
    Nested n{5, Inner{6, 7.5f}};
    Recorder r;
    reflect(n, r);
    CHECK((r.log == std::vector<std::string>{"int id PN", "int a PN", "float b PN"}));
}

BREZEL_TEST(tag_helpers, "Reflection/Visitor.hpp",
            "seven tags; isPersistent, isNetworked and hasCommandStack test a tag list") {
    static_assert(static_cast<int>(Tag::CommandStack) == 6);
    ComponentVisitor v;
    CHECK(v.isPersistent({Tag::Networked, Tag::Persistent}));
    CHECK(!v.isPersistent({Tag::Networked}));
    CHECK(v.isNetworked({Tag::Networked}));
    CHECK(!v.isNetworked({}));
    CHECK(v.hasCommandStack({Tag::CommandStack}));
    CHECK(!v.hasCommandStack({Tag::Persistent}));
}

BREZEL_TEST(catch_all_reflect_visits_nothing, "Core/ComponentRegistry.hpp",
            "a type without a reflect of its own gets the catch-all Brezel::reflect, which visits nothing; through "
            "the registry the visitor sees the component begin and end with no property") {
    registerTestComponents();
    Opaque o;
    Recorder direct;
    Brezel::reflect(o, direct);
    CHECK(direct.log.empty());
    TestWorld world("Reflection", false);
    Entity e = world.project.createEntity("e");
    e.add<Opaque>();
    Recorder viaRegistry;
    CHECK(ComponentRegistry::reflectEntityComponent(e, StringID::from("Opaque"), viaRegistry));
    CHECK((viaRegistry.log == std::vector<std::string>{"begin Opaque", "end"}));
}

BREZEL_TEST(channel_components_reflect_copies, "Concurrency/StreamComponent.hpp",
            "StreamComponent and QueueComponent reflect isConnected, statusWord and lastTimestampSec read-only; "
            "statusWord and the time are copies, so a write through the visitor reaches isConnected only") {
    StreamComponent<StreamPayload> stream;
    stream.statusWord = 3;
    stream.lastTimestampNs = 2'000'000'000ULL;
    Recorder r;
    reflect(stream, r);
    CHECK((r.log == std::vector<std::string>{"bool isConnected R", "int statusWord R", "float lastTimestampSec R"}));
    Writer w;
    reflect(stream, w);
    CHECK(!stream.isConnected);  // written (false)
    CHECK_EQ(stream.statusWord, uint32_t(3));
    CHECK_EQ(stream.lastTimestampNs, uint64_t(2'000'000'000ULL));
    QueueComponent<StreamPayload, 4> queue;
    queue.isConnected = true;
    queue.statusWord = 9;
    reflect(queue, w);
    CHECK(!queue.isConnected);
    CHECK_EQ(queue.statusWord, uint32_t(9));
}

BREZEL_TEST_MAIN("reflection")
