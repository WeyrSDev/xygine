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

#ifndef XY_AUDIO_MIXER_HPP_
#define XY_AUDIO_MIXER_HPP_

#include <xyginext/Config.hpp>
#include <SFML/Config.hpp>

#include <array>
#include <string>

namespace xy
{
    /*!
    \brief Multichannel Audio mixer class.
    The AudioMixer allows AudioEmitter components to group
    their outputs into one of 16 channels, each of which
    may have their volume adjusted, affecting all of the
    AudioEmitters currently routed through that channel.
    The AudioMixer also has a master volume channel which
    is applied to all subsequent channels. By default
    AudioEmitter components are assigned to channel 0. For
    these AudioMixer settings to be applied a scene must
    have an active AudioSystem.
    \see AudioEmitter::setChannel()
    */
    class XY_EXPORT_API AudioMixer final
    {
    public:
        /*!
        \brief Sets the master volume.
        Volume ranges from 0 - 1 for normal values
        but are clamped up to a value of 10 which,
        depending onthe active subsystem, attemps
        to amplify the volume.
        */
        static void setMasterVolume(float);

        /*!
        \brief Returns the current master volume
        */
        static float getMasterVolume();

        /*!
        \brief Sets the volume of the given channel.
        Volume ranges from 0 - 1 for normal values
        but are clamped up to a value of 10 which,
        depending onthe active subsystem, attemps
        to amplify the volume.
        \param vol The volume to which to set the channel
        \param channel ID (0 - 15) of the channel whose
        volume should be set.
        */
        static void setVolume(float vol, sf::Uint8 channel);

        /*!
        \brief Returns the current volume of the requested channel
        */
        static float getVolume(sf::Uint8);

        /*!
        \brief Sets a label for a channel.
        For example you might want to set channel 0 to 'Effects'
        and channel 1 to 'Music' for easy reference. Channel names
        will appear in the console mixer window.
        \param label String to use as channel label
        \param channel To to which to apply the label
        */
        static void setLabel(const std::string& label, sf::Uint8 channel);

        /*!
        \brief Returns the current label assigned to the requested channel.
        \param channel Number of the channel from which to retrieve the label
        */
        static const std::string& getLabel(sf::Uint8 channel);

        /*!
        \brief Maximum number of available channels.
        Channel IDs start at 0 and run to MaxChannels - 1
        */
        static constexpr std::size_t MaxChannels = 16;

    private:
        static std::array<std::string, MaxChannels> m_labels;
        static std::array<float, MaxChannels> m_channels;
        static float m_masterVol;

        friend class AudioSystem;
    };
}

#endif //XY_AUDIO_MIXER_HPP_