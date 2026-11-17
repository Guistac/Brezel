// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Test components of Brezel's own (no type and no word of an application): a position, a label (a name), lists (of
// plain values, of strings and of a nested struct), an entity reference, a nested struct, a transient scratch value and
// an empty tag. Each has a free-function reflect found by argument-dependent lookup, the convention of
// docs/Framework_Guide.md section 4.1. Also the TestWorld fixture: a Project, a Dispatcher with the standard handlers,
// the calling thread as the project's writer, and the process-wide "active project" reset on destruction.
#pragma once

#include "Brezel/Core/ComponentRegistry.hpp"
#include "Brezel/Core/EntityReference.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Concurrency/AnyMutation.hpp"
#include "Brezel/Concurrency/StandardMutations.hpp"
#include "Brezel/Reflection/Visitor.hpp"

#include <string>
#include <thread>
#include <vector>

namespace BrezelTest {

using Brezel::Tag;

/// Two floats and an int; x and y are networked and persistent, layer is persistent only
struct Position {
    float x{0.0f};
    float y{0.0f};
    int layer{0};
};
template <typename V>
void reflect(Position& p, V& v) {
    v.visit_property("x", p.x, {Tag::Persistent, Tag::Networked});
    v.visit_property("y", p.y, {Tag::Persistent, Tag::Networked});
    v.visit_property("layer", p.layer, {Tag::Persistent});
}

/// A name: a string, a bool and an interned id; `note` carries no tag (never saved, never networked)
struct Label {
    std::string text;
    bool visible{true};
    Brezel::StringID kind;
    std::string note;
};
template <typename V>
void reflect(Label& l, V& v) {
    v.visit_property("text", l.text, {Tag::Persistent, Tag::Networked});
    v.visit_property("visible", l.visible, {Tag::Persistent, Tag::Networked});
    v.visit_property("kind", l.kind, {Tag::Persistent});
    v.visit_property("note", l.note);
}

/// A nested struct: reflected through its own member reflect (the convention VectorAccessor needs for elements)
struct Inner {
    int a{0};
    float b{0.0f};
    template <typename V>
    void reflect(V& v) {
        v.visit_property("a", a, {Tag::Persistent, Tag::Networked});
        v.visit_property("b", b, {Tag::Persistent, Tag::Networked});
    }
    bool operator==(const Inner&) const = default;
};

/// A component holding a nested struct: ComponentVisitor has no struct bracket, so the outer reflect visits the
/// inner fields itself (flattened)
struct Nested {
    int id{0};
    Inner inner;
};
template <typename V>
void reflect(Nested& n, V& v) {
    v.visit_property("id", n.id, {Tag::Persistent, Tag::Networked});
    n.inner.reflect(v);
}

/// Lists: of plain values, of strings and of a nested struct
struct Lists {
    std::vector<int> numbers;
    std::vector<std::string> words;
    std::vector<Inner> items;
};
template <typename V>
void reflect(Lists& l, V& v) {
    Brezel::VectorAccessor<int> numbers(l.numbers);
    v.visit_property("numbers", numbers, {Tag::Persistent, Tag::Networked});
    Brezel::VectorAccessor<std::string> words(l.words);
    v.visit_property("words", words, {Tag::Persistent, Tag::Networked});
    Brezel::VectorAccessor<Inner> items(l.items);
    v.visit_property("items", items, {Tag::Persistent, Tag::Networked});
}

/// An entity reference
struct Link {
    Brezel::EntityReference target;
};
template <typename V>
void reflect(Link& l, V& v) {
    v.visit_property("target", l.target, {Tag::Persistent, Tag::Networked});
}

/// Registered Transient: reflected, but not saved, cloned or encoded by the PersistentOnly filter
struct Scratch {
    int counter{0};
};
template <typename V>
void reflect(Scratch& s, V& v) {
    v.visit_property("counter", s.counter, {Tag::Persistent});
}

/// A tag component. It carries one field: an empty registered type does not compile, because the registry's reflect
/// lambda calls try_get, which EnTT's empty-type optimization does not provide (Framework_Guide.md section 4.3)
struct Marker {
    bool present{true};
};

/// Registered with no reflect of its own: the registry's catch-all reflect visits nothing
struct Opaque {
    int hidden{7};
};

/// Never registered
struct Unregistered {
    int value{0};
};
template <typename V>
void reflect(Unregistered& u, V& v) {
    v.visit_property("value", u.value, {Tag::Persistent, Tag::Networked});
}

/// Registers every test component (idempotent: registering again replaces the entry). Startup only.
inline void registerTestComponents() {
    namespace CR = Brezel::ComponentRegistry;
    CR::registerComponent<Position>("Position", CR::ComponentFlag::Networked);
    CR::registerComponent<Label>("Label", CR::ComponentFlag::Networked);
    CR::registerComponent<Nested>("Nested", CR::ComponentFlag::None);
    CR::registerComponent<Lists>("Lists", CR::ComponentFlag::Networked);
    CR::registerComponent<Link>("Link", CR::ComponentFlag::Networked);
    CR::registerComponent<Scratch>("Scratch", CR::ComponentFlag::Transient);
    CR::registerComponent<Marker>("Marker", {.category = "Tags"});
    CR::registerComponent<Opaque>("Opaque");
}

/**
 * @brief Records the three Brezel events in the order they fire ("spawned <uuid>", "changed <uuid> <component>",
 * "destroyed <uuid>"), with the thread each fired on. Connect it to a dispatcher with connect(), and disconnect it
 * before it goes out of scope. Thread: the dispatcher's (the writer).
 */
struct EventLog {
    std::vector<std::string> events;
    std::vector<std::thread::id> threads;

    void onSpawned(const Brezel::Events::EntitySpawned& e) { record("spawned " + std::to_string(e.uuid.value)); }
    void onChanged(const Brezel::Events::ComponentChanged& e) {
        record("changed " + std::to_string(e.uuid.value) + " " + e.componentName.toString());
    }
    void onDestroyed(const Brezel::Events::EntityDestroyed& e) { record("destroyed " + std::to_string(e.uuid.value)); }

    void connect(Brezel::Dispatcher& d) {
        d.sink<Brezel::Events::EntitySpawned>().connect<&EventLog::onSpawned>(*this);
        d.sink<Brezel::Events::ComponentChanged>().connect<&EventLog::onChanged>(*this);
        d.sink<Brezel::Events::EntityDestroyed>().connect<&EventLog::onDestroyed>(*this);
    }
    void disconnect(Brezel::Dispatcher& d) {
        d.sink<Brezel::Events::EntitySpawned>().disconnect(static_cast<const void*>(this));
        d.sink<Brezel::Events::ComponentChanged>().disconnect(static_cast<const void*>(this));
        d.sink<Brezel::Events::EntityDestroyed>().disconnect(static_cast<const void*>(this));
    }

private:
    void record(std::string what) {
        events.push_back(std::move(what));
        threads.push_back(std::this_thread::get_id());
    }
};

/**
 * @brief A project with a dispatcher and the standard mutation handlers, written by the calling thread.
 * The destructor clears the registry's process-wide active project and dispatcher when they point here, so that a
 * later registerComponent() does not connect a handler to a destroyed project (the reset Stacato's own supervisor
 * does). Thread: the test's thread, which is the writer.
 */
struct TestWorld {
    Brezel::Project project;
    Brezel::Dispatcher dispatcher;

    explicit TestWorld(std::string_view name = "TestWorld", bool standardHandlers = true) : project(name) {
        project.setWriterThread(std::this_thread::get_id());
        if (standardHandlers) Brezel::Mutations::registerStandardMutationHandlers(project, dispatcher);
    }
    ~TestWorld() {
        if (Brezel::ComponentRegistry::s_activeProject == &project) {
            Brezel::ComponentRegistry::s_activeProject = nullptr;
            Brezel::ComponentRegistry::s_activeDispatcher = nullptr;
        }
        project.setWriterThread(std::thread::id{});
    }
    TestWorld(const TestWorld&) = delete;
    TestWorld& operator=(const TestWorld&) = delete;

    /// One mutation, applied as a writer's tick applies it: inside write(), through the dispatcher
    void apply(const Brezel::AnyMutation& mutation) {
        auto lock = project.write();
        mutation.execute(dispatcher);
    }
};

} // namespace BrezelTest
