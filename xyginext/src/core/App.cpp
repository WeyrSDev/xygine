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

#include <xyginext/core/App.hpp>
#include <xyginext/core/Log.hpp>
#include <xyginext/core/Console.hpp>
#include <xyginext/core/ConfigFile.hpp>
#include <xyginext/core/FileSystem.hpp>
#include <xyginext/detail/Operators.hpp>
#include <xyginext/gui/GuiClient.hpp>

#include "../imgui/imgui.h"
#include "../imgui/imgui_sfml.h"
#include "../imgui/imgui_internal.h"

#include "../detail/glad.h"

#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/Shader.hpp>
#include <SFML/Graphics/Texture.hpp>

#include <algorithm>
#include <fstream>
#include <cstring>
#include <array>

using namespace std::placeholders;
using namespace xy;

namespace
{
    const float timePerFrame = 1.f / 60.f;
    float timeSinceLastUpdate = 0.f;

#ifndef XY_DEBUG
    const std::string windowTitle("xyginext game (Release Build) - F1: Console, F2: Show stats");
#else
    const std::string windowTitle("xyginext game (Debug Build) - F1: Console, F2: Show stats");
#endif //XY_DEBUG

    sf::Clock frameClock;

    sf::RenderWindow* renderWindow = nullptr;

    bool running = false;

    sf::Color clearColour(0, 0, 0, 255);

    App* appInstance = nullptr;

    const std::string settingsFile("settings.cfg");

#include "DefaultIcon.inl"
}

App::App()
    : m_videoSettings   (),
    m_renderWindow      (m_videoSettings.VideoMode, windowTitle, m_videoSettings.WindowStyle, m_videoSettings.ContextSettings),
    m_showStats         (false)
{
    m_windowIcon.create(16u, 16u, defaultIcon);

    renderWindow = &m_renderWindow;

    m_renderWindow.setVerticalSyncEnabled(m_videoSettings.VSync);
    m_renderWindow.setIcon(16, 16, m_windowIcon.getPixelsPtr());

    //store available modes and remove unusable
    m_videoSettings.AvailableVideoModes = sf::VideoMode::getFullscreenModes();
    m_videoSettings.AvailableVideoModes.erase(std::remove_if(m_videoSettings.AvailableVideoModes.begin(), m_videoSettings.AvailableVideoModes.end(),
        [](const sf::VideoMode& vm)
    {
        return (!vm.isValid() || vm.bitsPerPixel != 32);
    }), m_videoSettings.AvailableVideoModes.end());
    std::reverse(m_videoSettings.AvailableVideoModes.begin(), m_videoSettings.AvailableVideoModes.end());

    m_videoSettings.Title = windowTitle;

    //if we find a settings file apply those settings
    loadSettings();

    update = [this](float dt)
    {
        updateApp(dt);
    };
    eventHandler = std::bind(&App::handleEvent, this, _1);

    appInstance = this;

    if (!gladLoadGL())
    {
        Logger::log("Something went wrong loading OpenGL. Particles may be unavailable", Logger::Type::Error, Logger::Output::All);
    }
}

//public
void App::run()
{
    if (!sf::Shader::isAvailable())
    {
        Logger::log("Shaders reported as unavailable.", Logger::Type::Error, Logger::Output::File);
        return;
    }

    ImGui::SFML::Init(m_renderWindow);
    Console::init();
    initialise();

    running = true;
    frameClock.restart();
    while (running)
    {
        float elapsedTime = frameClock.restart().asSeconds();
        timeSinceLastUpdate += elapsedTime;
        
        doImgui();

        while (timeSinceLastUpdate > timePerFrame)
        {
            timeSinceLastUpdate -= timePerFrame;
            
            handleEvents();
            handleMessages();

            update(timePerFrame);                 
        }

        m_renderWindow.clear(clearColour);
        draw();
        ImGui::Render();
        m_renderWindow.display();
    }

    m_renderWindow.close();
    m_messageBus.disable(); //prevents spamming with loads of entity quit messages

    Console::finalise();
    finalise();
    ImGui::SFML::Shutdown();

    saveSettings();
}

void App::pause()
{
    update = [](float) {};
}

void App::resume()
{
    update = [this](float dt)
    {
        updateApp(dt);
    };
    frameClock.restart();
    timeSinceLastUpdate = 0.f;
}

const App::VideoSettings& App::getVideoSettings() const
{
    return m_videoSettings;
}

void App::applyVideoSettings(const VideoSettings& settings) 
{
    if (m_videoSettings == settings) return;

    auto availableModes = m_videoSettings.AvailableVideoModes;

    auto oldAA = settings.ContextSettings.antialiasingLevel;
    if (settings.WindowStyle != m_videoSettings.WindowStyle
        || settings.ContextSettings != m_videoSettings.ContextSettings)
    {
        m_renderWindow.create(settings.VideoMode, settings.Title, settings.WindowStyle, settings.ContextSettings);
    }
    else
    {
        m_renderWindow.setSize({ settings.VideoMode.width, settings.VideoMode.height });
        m_renderWindow.setTitle(settings.Title);

        sf::Vector2u windowPos = { sf::VideoMode::getDesktopMode().width / 2, sf::VideoMode::getDesktopMode().height / 2 };
        windowPos.x -= settings.VideoMode.width / 2;
        windowPos.y -= settings.VideoMode.height / 2;
        m_renderWindow.setPosition({ static_cast<sf::Int32>(windowPos.x), static_cast<sf::Int32>(windowPos.y) });
    }

    auto* msg = m_messageBus.post<Message::WindowEvent>(Message::WindowMessage);
    msg->type = Message::WindowEvent::Resized;
    msg->width = settings.VideoMode.width;
    msg->height = settings.VideoMode.height;

    //check if the AA level is the same as requested
    auto newAA = m_renderWindow.getSettings().antialiasingLevel;
    if (oldAA != newAA)
    {
        Logger::log("Requested Anti-aliasing level not available, using level: " + std::to_string(newAA), Logger::Type::Warning, Logger::Output::All);
    }

    m_renderWindow.setVerticalSyncEnabled(settings.VSync);
    //m_renderWindow.setMouseCursorVisible(false);
    //TODO test validity and restore old settings if possible
    m_videoSettings = settings;
    m_videoSettings.ContextSettings.antialiasingLevel = newAA; //so it's correct if requested
    m_videoSettings.AvailableVideoModes = availableModes;

    if (m_windowIcon.getPixelsPtr())
    {
        auto size = m_windowIcon.getSize();
        m_renderWindow.setIcon(size.x, size.y, m_windowIcon.getPixelsPtr());
    }
}

MessageBus& App::getMessageBus()
{
    return m_messageBus;
}

void App::quit()
{
    //XY_ASSERT(renderWindow, "no valid window instance");

    //renderWindow->close();
    running = false;
}

void App::setClearColour(sf::Color colour)
{
    clearColour = colour;
}

sf::Color App::getClearColour()
{
    return clearColour;
}

void App::setWindowTitle(const std::string& title)
{
    m_videoSettings.Title = title;
    m_renderWindow.setTitle(title);
}

void App::setWindowIcon(const std::string& path)
{
    if (m_windowIcon.loadFromFile(path))
    {
        auto size = m_windowIcon.getSize();
        XY_ASSERT(size.x == 16 && size.y == 16, "window icon must be 16x16 pixels");
        m_renderWindow.setIcon(size.x, size.y, m_windowIcon.getPixelsPtr());
    }
    else
    {
        xy::Logger::log("failed to open " + path, xy::Logger::Type::Error);
    }
}

sf::RenderWindow* App::getRenderWindow()
{
    //XY_ASSERT(renderWindow, "Window not created");
    return renderWindow;
}

void App::printStat(const std::string& name, const std::string& value)
{
    XY_ASSERT(appInstance, "hm");
    appInstance->m_debugLines.push_back(name + ":" + value);
}

App* App::getActiveInstance()
{
    //XY_ASSERT(appInstance, "No active app instance");
    return appInstance;
}

//protected
void App::initialise() {}

void App::finalise() {}

//private
void App::saveScreenshot()
{
    std::time_t time = std::time(nullptr);
    struct tm* timeInfo;
    timeInfo = std::localtime(&time);

    std::array<char, 40u> buffer;
    std::string fileName;

    strftime(buffer.data(), 40, "screenshot%d_%m_%y_%H_%M_%S.png", timeInfo);

    fileName.assign(buffer.data());

    sf::Texture t;
    t.create(m_renderWindow.getSize().x, m_renderWindow.getSize().y);
    t.update(m_renderWindow);
    sf::Image screenCap = t.copyToImage();
    if (!screenCap.saveToFile(fileName)) Logger::log("failed to save " + fileName, Logger::Type::Error, Logger::Output::File);
}

void App::handleEvents()
{
    sf::Event evt;

    while (m_renderWindow.pollEvent(evt))
    {        
        auto imguiConsumed = ImGui::SFML::ProcessEvent(evt);

        switch (evt.type)
        {
        case sf::Event::LostFocus:
            eventHandler = [](const sf::Event&) {};

            {
                auto* msg = m_messageBus.post<Message::WindowEvent>(Message::WindowMessage);
                msg->type = Message::WindowEvent::LostFocus;
                msg->width = m_renderWindow.getSize().x;
                msg->height = m_renderWindow.getSize().y;
            }
            continue;
        case sf::Event::GainedFocus:
            eventHandler = std::bind(&App::handleEvent, this, _1);
            frameClock.restart(); //prevent dumps of HUGE dt
            {
                auto* msg = m_messageBus.post<Message::WindowEvent>(Message::WindowMessage);
                msg->type = Message::WindowEvent::GainedFocus;
                msg->width = m_renderWindow.getSize().x;
                msg->height = m_renderWindow.getSize().y;
            }

            continue;
        case sf::Event::Closed:
            quit();
            return;
        case sf::Event::Resized:
        {
            auto* msg = m_messageBus.post<Message::WindowEvent>(Message::WindowMessage);
            msg->type = Message::WindowEvent::Resized;
            msg->width = evt.size.width;
            msg->height = evt.size.height;
        }
            break;
        default: break;
        }

        if (evt.type == sf::Event::KeyReleased)
        {
            switch (evt.key.code)
            {
            case sf::Keyboard::F1:
                Console::show();
                break;
            case sf::Keyboard::F2:
                m_showStats = !m_showStats;
                break;
            case sf::Keyboard::F5:
                saveScreenshot();
                break;
            default:break;
            }           
        }
        
        if(!imguiConsumed) eventHandler(evt);
    }   
}

void App::handleMessages()
{
    while (!m_messageBus.empty())
    {
        auto msg = m_messageBus.poll();

        handleMessage(msg);
    } 
}

void App::doImgui()
{
    ImGui::SFML::Update(false);

    Console::draw();

    for (auto& f : m_guiWindows) f.first();

    if (m_showStats)
    {
        ImGui::SetNextWindowSizeConstraints({ 360.f, 200.f }, { 400.f, 1000.f });
        ImGui::Begin("Stats:", &m_showStats);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::NewLine();

        //display any registered controls
        for (const auto& func : m_statusControls)
        {
            func.first();
        }

        //print any debug lines       
        for (const auto& p : m_debugLines)
        {
            ImGui::Text("%s", p.c_str());
        }

        ImGui::End();
    }
    m_debugLines.clear();
    m_debugLines.reserve(10);
}

void App::addStatusControl(const std::function<void()>& func, const GuiClient* c)
{
    XY_ASSERT(appInstance, "App not properly instanciated!");
    appInstance->m_statusControls.push_back(std::make_pair(func, c));
}

void App::removeStatusControls(const GuiClient* c)
{
    XY_ASSERT(appInstance, "App not properly instanciated!");

    appInstance->m_statusControls.erase(
        std::remove_if(std::begin(appInstance->m_statusControls), std::end(appInstance->m_statusControls),
            [c](const std::pair<std::function<void()>, const GuiClient*>& pair)
    {
        return pair.second == c;
    }), std::end(appInstance->m_statusControls));
}

void App::addWindow(const std::function<void()>& func, const GuiClient* c)
{
    XY_ASSERT(appInstance, "App not properly instanciated!");

    appInstance->m_guiWindows.push_back(std::make_pair(func, c));
}

void App::removeWindows(const GuiClient* c)
{
    XY_ASSERT(appInstance, "App not properly instanciated!");

    appInstance->m_guiWindows.erase(
        std::remove_if(std::begin(appInstance->m_guiWindows), std::end(appInstance->m_guiWindows),
            [c](const std::pair<std::function<void()>, const GuiClient*>& pair)
    {
        return pair.second == c;
    }), std::end(appInstance->m_guiWindows));
}

void App::loadSettings()
{
    ConfigFile settings;
    if (settings.loadFromFile(FileSystem::getConfigDirectory(APP_NAME) + settingsFile))
    {
        auto objects = settings.getObjects();
        auto vObj = std::find_if(objects.begin(), objects.end(),
            [](const ConfigObject& o)
        {
            return o.getName() == "video";
        });

        if (vObj != objects.end())
        {
            const auto& properties = vObj->getProperties();
            VideoSettings vSettings;
            for (const auto& p : properties)
            {
                if (p.getName() == "resolution")
                {
                    auto res = static_cast<sf::Vector2u>(p.getValue<sf::Vector2f>());
                    vSettings.VideoMode.width = res.x;
                    vSettings.VideoMode.height = res.y;
                }
                else if (p.getName() == "vsync")
                {
                    vSettings.VSync = p.getValue<bool>();
                }
                else if (p.getName() == "fullscreen")
                {
                    vSettings.WindowStyle = (p.getValue<bool>()) ? sf::Style::Fullscreen : sf::Style::Close;
                }
            }

            vSettings.Title = windowTitle;
            applyVideoSettings(vSettings);
        }
        

        //load audio settings and apply to mixer / master vol
        auto aObj = std::find_if(objects.begin(), objects.end(),
            [](const ConfigObject& o)
        {
            return o.getName() == "audio";
        });

        if (aObj != objects.end())
        {
            const auto& properties = aObj->getProperties();
            for (const auto& p : properties)
            {
                if (p.getName() == "master")
                {
                    AudioMixer::setMasterVolume(p.getValue<float>());
                }
                else
                {
                    auto name = p.getName();
                    auto found = name.find("channel");
                    if (found != std::string::npos)
                    {
                        auto ident = name.substr(found + 7);
                        try
                        {
                            auto channel = std::stoi(ident);
                            AudioMixer::setVolume(p.getValue<float>(), channel);
                        }
                        catch (...)
                        {
                            continue;
                        }
                    }
                }
            }
        }
    }
}

void App::saveSettings()
{
    ConfigFile settings("settings");

    auto* vObj = settings.addObject("video");
    vObj->addProperty("resolution", std::to_string(m_videoSettings.VideoMode.width) + "," + std::to_string(m_videoSettings.VideoMode.height));
    vObj->addProperty("vsync", m_videoSettings.VSync ? "true" : "false");
    vObj->addProperty("fullscreen", (m_videoSettings.WindowStyle & sf::Style::Fullscreen) ? "true" : "false");
    
    auto* aObj = settings.addObject("audio");
    aObj->addProperty("master", std::to_string(AudioMixer::getMasterVolume()));
    for (auto i = 0u; i < AudioMixer::MaxChannels; ++i)
    {
        aObj->addProperty("channel" + std::to_string(i), std::to_string(AudioMixer::getVolume(i)));
    }
    
    settings.save(FileSystem::getConfigDirectory(APP_NAME) + settingsFile);
}