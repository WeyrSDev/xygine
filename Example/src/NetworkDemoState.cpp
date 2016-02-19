/*********************************************************************
Matt Marchant 2014 - 2016
http://trederia.blogspot.com

xygine - Zlib license.

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

#include <NetworkDemoState.hpp>
#include <NetworkDemoPacketIDs.hpp>
#include <NetworkDemoController.hpp>
#include <NetworkDemoPlayerController.hpp>
#include <NetworkDemoBallLogic.hpp>

#include <xygine/App.hpp>
#include <xygine/Reports.hpp>
#include <xygine/ui/Button.hpp>
#include <xygine/ui/TextBox.hpp>
#include <xygine/components/SfDrawableComponent.hpp>
#include <xygine/PostChromeAb.hpp>

#include <SFML/Window/Event.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/Graphics/RectangleShape.hpp>

namespace
{
    //const xy::PortNumber port = 5000;
    sf::Uint64 playerEntID = 0;
}

using namespace std::placeholders;
using namespace NetDemo;

NetworkDemoState::NetworkDemoState(xy::StateStack& stack, Context context)
    : State             (stack, context),
    m_messageBus        (context.appInstance.getMessageBus()),
    m_scene             (m_messageBus),
    m_collisionWorld    (m_scene, m_messageBus)
{
    launchLoadingScreen();
    xy::Stats::clear();
    buildMenu();

    m_packetHandler = std::bind(&NetworkDemoState::handlePacket, this, _1, _2, _3);
    m_connection.setPacketHandler(m_packetHandler);

    m_reportText.setFont(m_fontResource.get("assets/fonts/Console.ttf"));
    m_reportText.setPosition(40.f, 20.f);

    auto pp = xy::PostProcess::create<xy::PostChromeAb>();
    m_scene.addPostProcess(pp);
    m_scene.setView(context.defaultView);

    m_server.setDebugView(context.defaultView);

    quitLoadingScreen();
}

namespace
{
    float broadcastAccumulator = 0.f;
    sf::Clock broadcastClock;
}

//public
bool NetworkDemoState::update(float dt)
{
    //m_playerInput.dt += dt;
        
    m_playerInput.position = getContext().appInstance.getMouseWorldPosition().y;

    xy::Command cmd;
    cmd.entityID = playerEntID;
    cmd.action = [this](xy::Entity& entity, float dt)
    {
        sf::Lock lock(m_connection.getMutex());
        entity.getComponent<PlayerController>()->setInput(m_playerInput);
    };
    m_scene.sendCommand(cmd);
    //REPORT("Player Pos", std::to_string(m_playerInput.position));
  
    m_menu.update(dt);
    m_server.update(dt);
    m_connection.update(dt);
    m_scene.update(dt);


    const float sendRate = 1.f / m_connection.getSendRate();
    broadcastAccumulator += broadcastClock.restart().asSeconds();
    while (broadcastAccumulator >= sendRate)
    {
        m_playerInput.clientID = m_connection.getClientID();
        m_playerInput.timestamp = m_connection.getTime().asMilliseconds();

        broadcastAccumulator -= sendRate;
        sf::Packet packet;
        packet << xy::PacketID(PacketID::PlayerInput) << m_playerInput;
        m_connection.send(packet);

        //m_playerInput.dt = 0.f;
        m_playerInput.counter++;
    }

    m_reportText.setString(xy::Stats::getString());

    return true;
}

bool NetworkDemoState::handleEvent(const sf::Event& evt)
{
    switch (evt.type)
    {
    case sf::Event::KeyReleased:
        switch (evt.key.code)
        {
        case sf::Keyboard::Escape:
        //case sf::Keyboard::BackSpace:
            requestStackPop();
            requestStackPush(States::ID::MenuMain);
            break;
        default:break;
        }
    }
    
    m_menu.handleEvent(evt, getContext().appInstance.getMouseWorldPosition());
    return false;
}

void NetworkDemoState::handleMessage(const xy::Message& msg)
{
    
    
}

void NetworkDemoState::draw()
{
    auto& rw = getContext().renderWindow;

    rw.draw(m_scene);

    //m_server.drawDebug(rw);    

    rw.setView(getContext().defaultView);
    rw.draw(m_menu);
    rw.draw(m_reportText);
}

//private
void NetworkDemoState::buildMenu()
{
    auto& font = m_fontResource.get("assets/fonts/Console.ttf");

    auto textbox = xy::UI::create<xy::UI::TextBox>(font);
    textbox->setLabelText("IP Address:");
    textbox->setPosition(960.f, 500.f);
    textbox->setAlignment(xy::UI::Alignment::Centre);
    textbox->setText("127.0.0.1");
    m_menu.addControl(textbox);


    auto button = xy::UI::create<xy::UI::Button>(font, m_textureResource.get("assets/images/ui/start_button.png"));
    button->setText("Connect");
    button->setAlignment(xy::UI::Alignment::Centre);
    button->setPosition(960.f, 600.f);
    button->addCallback([textbox, this]()
    {
        //validate text/IP address
        auto address = textbox->getText();
        m_connection.setServerInfo({ address }, xy::Network::ServerPort);

        //connect to server
        if (address == "127.0.0.1" || address == "localhost")
        {
            m_server.start();
            sf::sleep(sf::seconds(1.f));           
        }

        m_connection.connect();

        //close menu
        m_menu.setVisible(false);
        getContext().renderWindow.setMouseCursorVisible(false);
    });
    m_menu.addControl(button);

    getContext().renderWindow.setMouseCursorVisible(true);
}

void NetworkDemoState::handlePacket(xy::Network::PacketType type, sf::Packet& packet, xy::Network::ClientConnection* connection)
{
    switch (type)
    {
    default: break;
    case xy::Network::Connect:
    {
        sf::Packet newPacket;
        newPacket << xy::PacketID(PacketID::PlayerDetails);
        newPacket << m_connection.getClientID();
        newPacket << "Player One";
        connection->send(newPacket, true);
    }
        break;
    case PacketID::BallSpawned:
    {
        sf::Uint64 id;
        sf::Vector2f pos, vel;
        packet >> id >> pos.x >> pos.y >> vel.x >> vel.y;
        spawnBall(id, pos, vel);
    }
        break;
    case PacketID::BallUpdate:
    {
        sf::Uint64 entid;
        sf::Vector2f pos, vel;
        sf::Uint32 step;
        packet >> entid >> pos.x >> pos.y >> vel.x >> vel.y >> step;

        xy::Command cmd;
        cmd.entityID = entid;
        cmd.action = [pos, vel, step](xy::Entity& entity, float)
        {
            entity.getComponent<BallLogic>()->reconcile(pos, vel, step);
        };
        sf::Lock lock(m_connection.getMutex());
        m_scene.sendCommand(cmd);
    }
    break;
    case PacketID::PlayerSpawned:       
    {
        xy::ClientID clid;
        sf::Uint64 entID;
        sf::Vector2f position;
        packet >> clid >> entID >> position.x >> position.y;
        spawnPlayer(clid, entID, position);
    }
        break;
    case PacketID::PositionUpdate:
    {
        //sf::Uint8 count;
        //packet >> count;

        //sf::Uint64 id;
        //float x, y;

        //sf::Lock lock(m_connection.getMutex());
        //while(count--)
        //{
        //    packet >> id >> x >> y;

        //    //don't send to local player
        //    if (id == playerEntID) continue;

        //    xy::Command cmd;
        //    cmd.entityID = id;

        //    cmd.action = [x, y](xy::Entity& entity, float)
        //    {
        //        entity.getComponent<NetworkController>()->setDestination({ x, y });
        //    };
        //    m_scene.sendCommand(cmd);
        //}
    }
        break;
    case PacketID::PlayerUpdate:
    {
        sf::Uint8 count;
        packet >> count;

        sf::Uint64 entID;
        std::string name;
        float position;
        sf::Uint64 lastInput;

        while (count--)
        {
            packet >> entID >> name >> position >> lastInput;
            if (entID == playerEntID)
            {
                xy::Command cmd;
                cmd.entityID = entID;
                cmd.action = [position, lastInput](xy::Entity& entity, float)
                {
                    entity.getComponent<PlayerController>()->reconcile(position, lastInput);
                };

                sf::Lock lock(m_connection.getMutex());
                m_scene.sendCommand(cmd);
                break;
            }
        }
    }
    break;
    case PacketID::EntityDestroyed:
    {
        sf::Uint64 entid;
        packet >> entid;

        sf::Lock lock(m_connection.getMutex());
        auto result = std::find(m_spawnedIDs.begin(), m_spawnedIDs.end(), entid);
        if (result != m_spawnedIDs.end())
        {
            xy::Command cmd;
            cmd.entityID = entid;
            cmd.action = [](xy::Entity& entity, float)
            {
                entity.destroy();
            };
            m_scene.sendCommand(cmd);
            m_spawnedIDs.erase(result);
        }
    }
    break;
    }
}

namespace
{
    const sf::Vector2f paddleSize(20.f, 100.f);
}

void NetworkDemoState::spawnBall(sf::Uint64 id, sf::Vector2f position, sf::Vector2f velocity)
{
    if (std::find(m_spawnedIDs.begin(), m_spawnedIDs.end(), id) != m_spawnedIDs.end()) return;

    auto shape = xy::Component::create<xy::SfDrawableComponent<sf::RectangleShape>>(m_messageBus);
    shape->getDrawable().setSize({ 20.f, 20.f });
    shape->getDrawable().setOrigin({ 10.f, 10.f });

    //auto controller = xy::Component::create<NetworkController>(m_messageBus);

    auto ent = xy::Entity::create(m_messageBus);
    ent->setWorldPosition(position);
    ent->setUID(id);
    ent->addComponent(shape);
    //ent->addComponent(controller);

    auto controller = xy::Component::create<BallLogic>(m_messageBus);
    controller->setCollisionObjects(m_collisionWorld.getEntities());
    controller->setVelocity(velocity);
    ent->addComponent(controller);

    m_scene.addEntity(ent, xy::Scene::Layer::FrontMiddle);

    m_spawnedIDs.push_back(id);
}

void NetworkDemoState::spawnPlayer(xy::ClientID clid, sf::Uint64 entid, sf::Vector2f position)
{
    if (std::find(m_spawnedIDs.begin(), m_spawnedIDs.end(), entid) != m_spawnedIDs.end()) return;
    if (clid == m_connection.getClientID())
    {
        //spawn a local player
        auto drawable = xy::Component::create<xy::SfDrawableComponent<sf::RectangleShape>>(m_messageBus);
        drawable->getDrawable().setSize(paddleSize);
        drawable->getDrawable().setOrigin(paddleSize / 2.f);

        auto controller = xy::Component::create<PlayerController>(m_messageBus);

        auto entity = xy::Entity::create(m_messageBus);
        entity->setUID(entid);
        entity->setWorldPosition(position);
        entity->addComponent(drawable);
        entity->addComponent(controller);

        m_collisionWorld.addEntity(m_scene.addEntity(entity, xy::Scene::Layer::FrontMiddle));

        playerEntID = entid;
        
    }
    else
    {
        //spawn remote player
    }
    m_spawnedIDs.push_back(entid);
}