#pragma once

#include "UUID.hpp"
#include "Entity.hpp"

struct EntityReference{
    UUID uuid;
    Entity entity;

    void set(Entity& ent){
        entity = ent;
        if(auto identity = entity.try_get<IdentityComponent>()){
            uuid = identity->uuid;
        }
    }
};