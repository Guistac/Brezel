#pragma once
#include <pugixml.hpp>
#include <unordered_set>
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

class IDProvider {
public:
    virtual ~IDProvider() = default;
    
    virtual UUID generate() = 0;
    
    virtual void saveState(pugi::xml_node& root) const = 0;
    virtual bool loadState(pugi::xml_node& root) = 0;
    
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

    bool loadState(pugi::xml_node& root) override {
        if(auto node = root.child("IDGeneratorState")){
            if(auto attr = node.attribute("NextID")){
                m_nextId = attr.as_ullong();
                return true;
            }
        }
        return false;
    }

    bool isUsed(UUID id) const override {
        return m_usedIds.find(id) != m_usedIds.end();
    }

    void markAsUsed(UUID id) override {
        m_usedIds.insert(id);
        
        uint64_t rawVal = static_cast<uint64_t>(id);
        if (rawVal >= m_nextId) {
            m_nextId = rawVal + 1;
        }
    }

private:
    uint64_t m_nextId = 1;
    std::unordered_set<UUID> m_usedIds;
};

} // namespace Brezel