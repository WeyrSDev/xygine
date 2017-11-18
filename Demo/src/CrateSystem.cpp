/*********************************************************************
(c) Matt Marchant 2017
http://trederia.blogspot.com

xygineXT - Zlib license.

This software is provided 'as-is', without any express or
implied warranty. In no event will the authors be held
liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute
it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented;
you must not claim that you wrote the original software.
If you use this software in a product, an acknowledgment
in the product documentation would be appreciated but
is not required.

2. Altered source versions must be plainly marked as such,
and must not be misrepresented as being the original software.

3. This notice may not be removed or altered from any
source distribution.
*********************************************************************/

#include "CrateSystem.hpp"
#include "ClientServerShared.hpp"
#include "MapData.hpp"
#include "PacketIDs.hpp"
#include "PlayerSystem.hpp"
#include "PowerupSystem.hpp"
#include "Explosion.hpp"
#include "AnimationController.hpp"
#include "CommandIDs.hpp"

#include <xyginext/ecs/Scene.hpp>
#include <xyginext/ecs/components/Transform.hpp>
#include <xyginext/ecs/components/QuadTreeItem.hpp>
#include <xyginext/ecs/components/CommandTarget.hpp>
#include <xyginext/network/NetHost.hpp>
#include <xyginext/util/Vector.hpp>
#include <xyginext/util/Wavetable.hpp>
#include <xyginext/util/Random.hpp>

namespace
{
    const float MinVelocity = 25.f; //min len sqr
    const float PushAcceleration = 10.f;
    const float LethalVelocity = 100000.f; //vel sqr before box becomes lethal
    const float RespawnTime = 5.f;
}

CrateSystem::CrateSystem(xy::MessageBus& mb, xy::NetHost& nh)
    : xy::System(mb, typeid(CrateSystem)),
    m_host(nh)
{
    requireComponent<Crate>();
    requireComponent<xy::Transform>();

    m_waveTable = xy::Util::Wavetable::sine(8.f, 1.2f);
}

//public
void CrateSystem::process(float dt)
{
    auto& entities = getEntities();
    for (auto& entity : entities)
    {
        auto& crate = entity.getComponent<Crate>();
        switch (crate.state)
        {
        default:break;
        case Crate::Ground:
            crate.velocity *= 0.89f; //friction
            groundCollision(entity);
            break;
        case Crate::Falling:
            crate.velocity.y += Gravity * dt;
            airCollision(entity);
            break;
        //case Crate::Breaking:
        //    //TODO probably moot state, we'll just spawn some particles instead
        //    break;
        }

        auto& tx = entity.getComponent<xy::Transform>();
        tx.move(crate.velocity * dt);

        //check velocity and put to eith full stop or make lethal
        float l2 = xy::Util::Vector::lengthSquared(crate.velocity);
        if (l2 < MinVelocity)
        {
            crate.velocity = {};
        }

        crate.lethal = (l2 > LethalVelocity);

        //check if crate explosive and do occasional shake if not moving
        if (crate.explosive && l2 == 0)
        {
            crate.shake.shakeTime -= dt;
            if (crate.shake.shakeTime < 0)
            {
                if (!crate.shake.shaking)
                {
                    crate.shake.shaking = true;
                    crate.shake.startPosition = tx.getPosition();
                    crate.shake.shakeTime = Crate::ShakeTime;
                }
                else
                {
                    crate.shake.shaking = false;
                    crate.shake.shakeTime = Crate::PauseTime;
                }
            }

            if (crate.shake.shaking)
            {
                tx.setPosition(crate.shake.startPosition.x, crate.shake.startPosition.y);
                tx.move(m_waveTable[crate.shake.shakeIndex], 0.f);
                crate.shake.shakeIndex = (crate.shake.shakeIndex + 1) % m_waveTable.size();
            }
        }

        //update the respawn queue
        for (auto it = m_respawnQueue.begin(); it != m_respawnQueue.end(); ++it)
        {
            it->first -= dt;
            if (it->first < 0)
            {
                //spawn crate
                spawn(it->second);

                //swap n pop
                std::swap(it, m_respawnQueue.end() - 1);
                m_respawnQueue.pop_back();
                break;
            }
        }
    }
}

//private
void CrateSystem::groundCollision(xy::Entity entity)
{
    const auto& collision = entity.getComponent<CollisionComponent>();
    auto hitboxCount = collision.getHitboxCount();
    const auto& hitboxes = collision.getHitboxes();

    auto& crate = entity.getComponent<Crate>();

    for (auto i = 0u; i < hitboxCount; ++i)
    {
        auto collisionCount = hitboxes[i].getCollisionCount();
        const auto& manifolds = hitboxes[i].getManifolds();

        if (hitboxes[i].getType() == CollisionType::Crate)
        {
            for (auto j = 0; j < collisionCount; ++j)
            {
                switch (manifolds[j].otherType)
                {
                default:break;
                case CollisionType::Platform:
                case CollisionType::Solid:
                case CollisionType::Crate:
                    entity.getComponent<xy::Transform>().move(manifolds[j].normal * manifolds[j].penetration);
                    if (manifolds[j].normal.x != 0)
                    {
                        crate.velocity.x = -crate.velocity.x;
                    }
                    break;
                case CollisionType::Player:
                    if (manifolds[j].normal.x != 0)
                    {
                        entity.getComponent<xy::Transform>().move(manifolds[j].normal * manifolds[j].penetration);
                        crate.velocity += manifolds[j].normal * manifolds[j].penetration * PushAcceleration;
                        if (!crate.lethal) //only take ownership if not moving (or at least slow enough to not kill)
                        {
                            crate.lastOwner = manifolds[j].otherEntity.getComponent<Player>().playerNumber;
                        }
                    }
                    break;
                case CollisionType::Powerup:
                    if (manifolds[j].otherEntity.hasComponent<Powerup>())
                    {
                        const auto& powerup = manifolds[j].otherEntity.getComponent<Powerup>();
                        if (powerup.state != Powerup::State::Idle)
                        {
                            crate.lastOwner = powerup.owner;
                            destroy(entity);
                            return;
                        }
                    }
                    break;
                }

                if (/*crate.lethal || */manifolds[j].penetration > (CrateBounds.width / 2.f))
                {
                    destroy(entity);
                    return;
                }
            }
        }
        else if (hitboxes[i].getType() == CollisionType::Foot)
        {
            if (collisionCount == 0)
            {
                crate.groundContact = false;
                crate.state = Crate::Falling;
                crate.velocity.x *= 0.3f;
                return;
            }
            
        }
    }
}

void CrateSystem::airCollision(xy::Entity entity)
{
    const auto& collision = entity.getComponent<CollisionComponent>();
    auto hitboxCount = collision.getHitboxCount();
    const auto& hitboxes = collision.getHitboxes();

    auto& crate = entity.getComponent<Crate>();

    for (auto i = 0u; i < hitboxCount; ++i)
    {
        auto collisionCount = hitboxes[i].getCollisionCount();
        const auto& manifolds = hitboxes[i].getManifolds();

        if (hitboxes[i].getType() == CollisionType::Crate)
        {
            for (auto j = 0; j < collisionCount; ++j)
            {
                if (manifolds[j].otherType == CollisionType::Teleport)
                {
                    if (manifolds[j].normal.y < 0)
                    {
                        //move up
                        entity.getComponent<xy::Transform>().move(0.f, -TeleportDistance);
                    }
                    else
                    {
                        //move down
                        entity.getComponent<xy::Transform>().move(0.f, TeleportDistance);
                    }
                }
                else if (manifolds[j].otherType == CollisionType::NPC
                    || manifolds[j].otherType == CollisionType::Player)
                {
                    //SQUISH
                    if ((crate.velocity.y) > std::abs(crate.velocity.x * 2.f)
                        && manifolds[j].normal.y < 0)
                    {
                        destroy(entity);
                    }
                    return;
                }
                else
                {
                    entity.getComponent<xy::Transform>().move(manifolds[j].normal * manifolds[j].penetration);
                    if (manifolds[j].normal.y < 1 && crate.groundContact)
                    {
                        crate.state = Crate::Ground;
                        crate.velocity.y = 0.f;
                        return;
                    }
                }
            }
        }
        else if (hitboxes[i].getType() == CollisionType::Foot)
        {
            crate.groundContact = (collisionCount != 0);
        }
    }
}

void CrateSystem::destroy(xy::Entity entity)
{
    const auto& tx = entity.getComponent<xy::Transform>();
    getScene()->destroyEntity(entity);

    //broadcast to client
    ActorEvent evt;
    evt.actor = entity.getComponent<Actor>();
    evt.x = tx.getPosition().x;
    evt.y = tx.getPosition().y;
    evt.type = ActorEvent::Died;

    m_host.broadcastPacket(PacketID::ActorEvent, evt, xy::NetFlag::Reliable, 1);

    const auto& crate = entity.getComponent<Crate>();

    if (crate.explosive)
    {
        //spawn explosion
        auto expEnt = getScene()->createEntity();
        expEnt.addComponent<Explosion>().owner = entity.getComponent<Crate>().lastOwner;
        expEnt.addComponent<xy::Transform>().setPosition(tx.getPosition());
        expEnt.getComponent<xy::Transform>().setOrigin(ExplosionOrigin);
        expEnt.addComponent<Actor>().id = expEnt.getIndex();
        expEnt.getComponent<Actor>().type = ActorID::Explosion;
        expEnt.addComponent<CollisionComponent>().addHitbox(ExplosionBounds, CollisionType::Explosion);
        expEnt.getComponent<CollisionComponent>().setCollisionCategoryBits(CollisionFlags::Explosion);
        expEnt.getComponent<CollisionComponent>().setCollisionMaskBits(CollisionFlags::ExplosionMask);
        expEnt.addComponent<xy::QuadTreeItem>().setArea(ExplosionBounds);
        expEnt.addComponent<AnimationController>();
        expEnt.addComponent<xy::CommandTarget>().ID = CommandID::MapItem;

        //broadcast to clients
        evt.actor = expEnt.getComponent<Actor>();
        evt.x = tx.getPosition().x;
        evt.y = tx.getPosition().y;
        evt.type = ActorEvent::Spawned;

        m_host.broadcastPacket(PacketID::ActorEvent, evt, xy::NetFlag::Reliable, 1);
    }

    if (crate.respawn)
    {
        m_respawnQueue.push_back(std::make_pair(RespawnTime, crate));
    }
}

void CrateSystem::spawn(Crate crate)
{
    auto entity = getScene()->createEntity();
    entity.addComponent<xy::Transform>().setPosition(crate.spawnPosition);
    entity.getComponent<xy::Transform>().setOrigin(CrateOrigin);
    entity.addComponent<Actor>().id = entity.getIndex();
    entity.getComponent<Actor>().type = ActorID::Crate;

    entity.addComponent<xy::QuadTreeItem>().setArea(CrateBounds);

    entity.addComponent<AnimationController>();
    entity.addComponent<xy::CommandTarget>().ID = CommandID::MapItem;

    crate.velocity = {};
    crate.lastOwner = 3;
    crate.lethal = false;
    entity.addComponent<Crate>() = crate;

    entity.addComponent<CollisionComponent>().addHitbox(CrateBounds, CollisionType::Crate);
    entity.getComponent<CollisionComponent>().addHitbox(CrateFoot, CollisionType::Foot);
    entity.getComponent<CollisionComponent>().setCollisionCategoryBits(CollisionFlags::Crate);
    entity.getComponent<CollisionComponent>().setCollisionMaskBits(CollisionFlags::CrateMask);

    //let clients know
    ActorEvent evt;
    evt.actor = entity.getComponent<Actor>();
    evt.x = crate.spawnPosition.x;
    evt.y = crate.spawnPosition.y;
    evt.type = ActorEvent::Spawned;

    m_host.broadcastPacket(PacketID::ActorEvent, evt, xy::NetFlag::Reliable, 1);
}

void CrateSystem::onEntityAdded(xy::Entity entity)
{
    entity.getComponent<Crate>().shake.shakeIndex = xy::Util::Random::value(0, m_waveTable.size() - 1);
    entity.getComponent<Crate>().shake.shakeTime = xy::Util::Random::value(4.f, Crate::PauseTime + 4.f);
}