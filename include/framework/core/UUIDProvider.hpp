
#pragma once

#include <pugixml.hpp>

#include <unordered_set>

#include "UUID.hpp"

class IDProvider {
public:
    virtual ~IDProvider() = default;
    
    // Returns a Strong-Typed UUID
    virtual UUID generate() = 0;
    
    virtual void saveState(pugi::xml_node& root) const = 0;
    virtual void loadState(pugi::xml_node& root) = 0;
    
    // Validation using the UUID type
    virtual bool isUsed(UUID id) const = 0;
    virtual void markAsUsed(UUID id) = 0;
};


class SequentialIDProvider : public IDProvider {
public:
    UUID generate() override {
        uint64_t rawId = m_nextId++;
        UUID newId(rawId);
        m_usedIds.insert(newId);
        return newId;
    }

    void saveState(pugi::xml_node& root) const override {
        auto node = root.append_child("IDGeneratorState");
        node.append_attribute("Type") = "Sequential";
        node.append_attribute("NextID") = m_nextId;
    }

    void loadState(pugi::xml_node& root) override {
        auto node = root.child("IDGeneratorState");
        if (node) {
            m_nextId = node.attribute("NextID").as_ullong(1);
        }
    }

    bool isUsed(UUID id) const override {
        return m_usedIds.find(id) != m_usedIds.end();
    }

    void markAsUsed(UUID id) override {
        m_usedIds.insert(id);
        
        // Internal counter safety:
        // Ensure m_nextId is always higher than the highest loaded ID
        uint64_t rawVal = static_cast<uint64_t>(id);
        if (rawVal >= m_nextId) {
            m_nextId = rawVal + 1;
        }
    }

private:
    uint64_t m_nextId = 1;
    std::unordered_set<UUID> m_usedIds;
};