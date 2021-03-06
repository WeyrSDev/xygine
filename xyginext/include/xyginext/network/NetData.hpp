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

#ifndef XY_NET_DATA_HPP_
#define XY_NET_DATA_HPP_

#include <xyginext/Config.hpp>
#include <xyginext/core/Assert.hpp>
#include <SFML/Config.hpp>

#include <cstring>

struct _ENetPacket;
struct _ENetPeer;

namespace xy
{
    /*!
    \brief A peer represents a single, multichannel connection between
    a client and a host.
    */
    struct XY_EXPORT_API NetPeer final
    {
        std::string getAddress() const; //! <String containing the IPv4 address
        sf::Uint16 getPort() const; //! <Port number
        sf::Uint32 getID() const; //! <Unique ID
        sf::Uint32 getRoundTripTime() const; //! <Mean round trip time in milliseconds of a reliable packet

        enum class State
        {
            Disconnected,
            Connecting,
            AcknowledingConnect,
            PendingConnect,
            Succeeded,
            Connected,
            DisconnectLater,
            Disconnecting,
            AcknowledingDisconnect,
            Zombie
        };
        State getState() const; //! <Current state of the peer

        bool operator == (const NetPeer& other) const
        {
            return other.m_peer == this->m_peer;
        }

        operator bool() const { return m_peer != nullptr; }

    private:
        _ENetPeer* m_peer = nullptr;

        friend class NetClient;
        friend class NetHost;
    };

    /*!
    \brief Network event.
    These are used to poll NetHost and NetClient objects
    for network activity
    */
    struct XY_EXPORT_API NetEvent final
    {
        sf::Uint8 channel = 0; //! <channel event was received on
        enum
        {
            None,
            ClientConnect,
            ClientDisconnect,
            PacketReceived
        }type = None;

        /*!
        \brief Event packet.
        Contains packet data recieved by PacketRecieved event.
        Not valid for other event types.
        */
        struct XY_EXPORT_API Packet final
        {
            Packet();
            ~Packet();

            Packet(const Packet&) = delete;
            Packet(Packet&&) = delete;
            Packet& operator = (const Packet&) = delete;
            Packet& operator = (Packet&&) = delete;

            /*!
            \brief The unique ID this packet was tegged with when sent
            */
            sf::Uint32 getID() const;

            /*!
            \brief Used to retreive the data as a specific type.
            Trying to read data as an incorrect type will lead to
            undefined behaviour.
            */
            template <typename T>
            T as() const;

            /*!
            \brief Returns a pointer to the raw packet data
            */
            const void* getData() const;
            /*!
            \brief Returns the size of the data, in bytes
            */
            std::size_t getSize() const;

        private:
            _ENetPacket* m_packet;
            sf::Uint32 m_id;
            void setPacketData(_ENetPacket*);

            friend class NetClient;
            friend class NetHost;
        }packet;

        /*!
        \brief Contains the peer from which this event was transmitted
        */
        NetPeer peer;
    };

#include "NetData.inl"

    /*!
    \brief Reliability enum.
    These are used to flag sent packets with a requested reliability.
    */
    enum class NetFlag
    {
        Reliable = 0x1, //! <packet must be received by the remote connection, and resend attemps are made until delivered
        Unsequenced = 0x2, //! <packet will not be sequenced with other packets. Not supported on reliable packets
        Unreliable = 0x4 //! <packet will be fragments and sent unreliably if it exceeds MTU
    };
}

#endif //XY_NET_DATA_HPP_