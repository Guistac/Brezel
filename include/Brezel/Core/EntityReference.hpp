#pragma once
#include "Brezel/Core/UUID.hpp"
#include "Brezel/Core/Entity.hpp"

namespace Brezel {

struct EntityReference {
    UUID uuid;
    Entity entity;

    void set(Entity& ent){
        entity = ent;
        if(auto identity = entity.try_get<IdentityComponent>()){
            uuid = identity->uuid;
        }
    }
};

} // namespace Brezel