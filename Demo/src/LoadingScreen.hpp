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

#ifndef  DEMO_LOADING_SCREEN_HPP_
#define DEMO_LOADING_SCREEN_HPP_

#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>

class LoadingScreen final : public sf::Drawable
{
public:
    LoadingScreen();

    void update(float);

private:
    sf::Texture m_texture;
    std::size_t m_frame;
    sf::Sprite m_sprite;

    void draw(sf::RenderTarget&, sf::RenderStates) const override;
};


#endif //DEMO_LOADING_SCREEN_HPP_
