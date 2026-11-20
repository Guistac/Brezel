#pragma once

#include "Brezel/Core/ComponentRegistry.hpp"
#include "Brezel/Reflection/Visitor.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Reads one field of one component of an entity by name, through the component's reflect function.
//
// A component is named by its saved name (the string it was registered with), a field by its reflect key. The read
// goes through the registry's type info only: `componentInfoByTypeName`, `hasComponent` and `reflect` (which visits
// the component through `try_get`). It never creates a component (no `addReflectEntityComponent`), never dispatches a
// mutation or an event, and never calls `onUpdated`.
//
// Reflect functions write back what they visit: a value read into a temporary is clamped and stored again, a list is
// resized to the count the visitor hands back. The reading visitor hands every value back exactly as it was given, so
// reading a component whose values are already in their ranges leaves it bit-identical. A component holding values
// its own reflect would normalise (a NaN rate, a count past a cap) is normalised by the read, as by any visit.
//
// Kinds: `int`, `float`, `bool` and `std::string` fields. A field reflected through a temporary (a `uint32_t` copied
// into an `int`, a fixed `char` array copied into a `std::string`) reads as that temporary. `StringID`,
// `EntityReference` and list (`VectorAccessorBase`) properties are skipped; a list's elements are never visited, so a
// read never resizes it. A field tagged `Hidden` is not read and not listed. When two properties share a key, the
// first visited wins.
//
// Thread: the caller holds world access to the entity (the project's write lock, or is its single writer): the
// reflect writes the values back, so a shared read lock is not enough while another thread may read the component.

namespace Brezel {

/// The kind of a readable field
enum class FieldKind : uint8_t { INT, FLOAT, BOOL, STRING };

/// A field's value: INT, FLOAT and BOOL in `number` (a bool as 1 or 0), STRING in `text`
struct FieldValue {
    FieldKind kind{FieldKind::FLOAT};
    double number{0.0};
    std::string text;
};

/// A readable field of a component, as its reflect visits it
struct FieldInfo {
    StringID name;
    FieldKind kind{FieldKind::FLOAT};
    bool isReadOnly{false};    ///< Tagged ReadOnly (a live value the user does not edit)
    bool isPersistent{false};  ///< Tagged Persistent (saved)
};

namespace Detail {

inline bool hasTag(std::initializer_list<Tag> tags, Tag tag) {
    for (Tag t : tags)
        if (t == tag) return true;
    return false;
}

/// Reads one key, or lists every readable field. Never assigns to what it visits.
class FieldReadVisitor final : public ComponentVisitor {
public:
    /// Read mode: the first readable property named `target`
    explicit FieldReadVisitor(StringID target) : m_target(target), m_list(false) {}
    /// List mode: every readable property, in visit order
    FieldReadVisitor() : m_list(true) {}

    void visit_property(StringID name, int& value, std::initializer_list<Tag> tags = {}) override {
        take(name, FieldKind::INT, static_cast<double>(value), nullptr, tags);
    }
    void visit_property(StringID name, float& value, std::initializer_list<Tag> tags = {}) override {
        take(name, FieldKind::FLOAT, static_cast<double>(value), nullptr, tags);
    }
    void visit_property(StringID name, bool& value, std::initializer_list<Tag> tags = {}) override {
        take(name, FieldKind::BOOL, value ? 1.0 : 0.0, nullptr, tags);
    }
    void visit_property(StringID name, std::string& value, std::initializer_list<Tag> tags = {}) override {
        take(name, FieldKind::STRING, 0.0, &value, tags);
    }
    // StringID, EntityReference and lists: not read (the base class's empty overloads; a list is never resized)
    using ComponentVisitor::visit_property;

    std::optional<FieldValue> result;
    std::vector<FieldInfo> fields;

private:
    void take(StringID name, FieldKind kind, double number, const std::string* text,
              std::initializer_list<Tag> tags) {
        if (hasTag(tags, Tag::Hidden)) return;
        if (m_list) {
            fields.push_back(FieldInfo{name, kind, hasTag(tags, Tag::ReadOnly), hasTag(tags, Tag::Persistent)});
            return;
        }
        if (result || name != m_target) return;
        FieldValue v{};
        v.kind = kind;
        v.number = number;
        if (text) v.text = *text;
        result = std::move(v);
    }

    StringID m_target{};
    bool m_list{false};
};

/// The type info of a component the entity carries, or nullptr
inline const ComponentRegistry::ComponentTypeInfo* carriedComponent(Entity entity, StringID component) {
    if (!entity.isValid()) return nullptr;
    const auto it = ComponentRegistry::componentInfoByTypeName.find(component);
    if (it == ComponentRegistry::componentInfoByTypeName.end()) return nullptr;
    const ComponentRegistry::ComponentTypeInfo& info = it->second;
    if (!info.hasComponent || !info.reflect || !info.hasComponent(entity)) return nullptr;
    return &info;
}

} // namespace Detail

/// Reads one field of one component of an entity, by the component's saved name and the field's reflect key.
/// nullopt when the component type is unknown, the entity lacks it, or no visited field has that key or a kind
/// above. Never creates a component, never dispatches, never fires onUpdated. Hidden fields are not read. Lists are
/// skipped. Thread: see the top of this header.
inline std::optional<FieldValue> readComponentField(Entity entity, StringID component, StringID field) {
    const auto* info = Detail::carriedComponent(entity, component);
    if (!info) return std::nullopt;
    Detail::FieldReadVisitor visitor(field);
    info->reflect(entity, visitor);
    return std::move(visitor.result);
}

/// The readable fields of one component of an entity, in visit order (for a picker): not Hidden, the kinds above.
/// Empty when the type is unknown or the entity lacks it.
inline std::vector<FieldInfo> listComponentFields(Entity entity, StringID component) {
    const auto* info = Detail::carriedComponent(entity, component);
    if (!info) return {};
    Detail::FieldReadVisitor visitor;
    info->reflect(entity, visitor);
    return std::move(visitor.fields);
}

/// The saved names of the registered components the entity carries (IdentityComponent left out), in the registry's
/// storage order.
inline std::vector<StringID> listEntityComponents(Entity entity) {
    std::vector<StringID> out;
    if (!entity.isValid()) return out;
    static const StringID identitySid = StringID::from("IdentityComponent");
    auto& registry = entity.registry();
    const auto raw = entity.handle().entity();
    for (auto [typeId, storage] : registry.storage()) {
        if (!storage.contains(raw)) continue;
        const auto it = ComponentRegistry::componentInfoById.find(typeId);
        if (it == ComponentRegistry::componentInfoById.end()) continue;
        if (it->second.saveString == identitySid) continue;
        out.push_back(it->second.saveString);
    }
    return out;
}

} // namespace Brezel
