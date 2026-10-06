#pragma once
#include <tinyxml2.h>
#include <random>
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

class IDProvider {
public:
    virtual ~IDProvider() = default;
    
    virtual UUID generate() = 0;
    
    virtual void saveState(tinyxml2::XMLDocument* doc, tinyxml2::XMLElement* root) const = 0;
    virtual bool loadState(tinyxml2::XMLElement* root) = 0;
    
    virtual bool isUsed(UUID id) const = 0;
    virtual void markAsUsed(UUID id) = 0;
};

class RandomIDProvider : public IDProvider {
public:
    RandomIDProvider() : m_engine(std::random_device{}()) {}

    UUID generate() override {
        return UUID(m_dist(m_engine));
    }

    void saveState(tinyxml2::XMLDocument* doc, tinyxml2::XMLElement* root) const override {
        auto node = doc->NewElement("IDGeneratorState");
        node->SetAttribute("Type", "Random");
        root->InsertEndChild(node);
    }

    bool loadState(tinyxml2::XMLElement* root) override {
        return root->FirstChildElement("IDGeneratorState") != nullptr;
    }

    bool isUsed(UUID id) const override {
        return false;
    }

    void markAsUsed(UUID id) override {
        // No-op for random provider
    }

private:
    std::mt19937_64 m_engine;
    std::uniform_int_distribution<uint64_t> m_dist{1, std::numeric_limits<uint64_t>::max()};
};

} // namespace Brezel