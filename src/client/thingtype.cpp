/*
 * Copyright (c) 2010-2017 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "thingtype.h"
#include "spritemanager.h"
#include "game.h"
#include "lightview.h"

#include <framework/graphics/graphics.h>
#include <framework/graphics/texture.h>
#include <framework/graphics/image.h>
#include <framework/graphics/texturemanager.h>
#include <framework/core/filestream.h>
#include <framework/otml/otml.h>

ThingType::ThingType()
{
    m_category = ThingInvalidCategory;
    m_id = 0;
    m_null = true;
    m_exactSize = 0;
    m_realSize = 0;
    m_animator = nullptr;
    m_numPatternX = m_numPatternY = m_numPatternZ = 0;
    m_animationPhases = 0;
    m_layers = 0;
    m_elevation = 0;
    m_opacity = 1.0f;
}

void ThingType::serialize(const FileStreamPtr& fin)
{
    for(int i = 0; i < ThingLastAttr; ++i) {
        if(!hasAttr((ThingAttr)i))
            continue;

        int attr = i;        
        if(attr == ThingAttrChargeable)
            attr = ThingAttrWritable;
        else if(attr >= ThingAttrWritable)
            attr += 1;

        fin->addU8(attr);
        switch(attr) {
            case ThingAttrDisplacement: {
                fin->addU16(m_displacement.x);
                fin->addU16(m_displacement.y);
                break;
            }
            case ThingAttrLight: {
                Light light = m_attribs.get<Light>(attr);
                fin->addU16(light.intensity);
                fin->addU16(light.color);
                break;
            }
            case ThingAttrMarket: {
                MarketData market = m_attribs.get<MarketData>(attr);
                fin->addU16(market.category);
                fin->addU16(market.tradeAs);
                fin->addU16(market.showAs);
                fin->addString(market.name);
                fin->addU16(market.restrictProfession);
                fin->addU16(market.requiredLevel);
                break;
            }
            case ThingAttrUsable:
            case ThingAttrElevation:
            case ThingAttrGround:
            case ThingAttrWritable:
            case ThingAttrWritableOnce:
            case ThingAttrMinimapColor:
            case ThingAttrCloth:
            case ThingAttrLensHelp:
                fin->addU16(m_attribs.get<uint16_t>(attr));
                break;
            default:
                break;
        };
    }
    fin->addU8(ThingLastAttr);

    fin->addU8(m_size.width());
    fin->addU8(m_size.height());

    if(m_size.width() > 1 || m_size.height() > 1)
        fin->addU8(m_realSize);

    fin->addU8(m_layers);
    fin->addU8(m_numPatternX);
    fin->addU8(m_numPatternY);
    fin->addU8(m_numPatternZ);
    fin->addU8(m_animationPhases);

    if(m_animationPhases > 1 && m_animator != nullptr)  {
        m_animator->serialize(fin);
    }

    for(unsigned int i = 0; i < m_spritesIndex.size(); i++) {
        fin->addU32(m_spritesIndex[i]);
    }
}

void ThingType::unserialize(uint16_t clientId, ThingCategory category, const FileStreamPtr& fin)
{
    m_null = false;
    m_id = clientId;
    m_category = category;

    int count = 0, attr = -1;
    bool done = false;
    for(int i = 0 ; i < ThingLastAttr;++i) {
        count++;
        attr = fin->getU8();
        if(attr == ThingLastAttr) {
            done = true;
            break;
        }

        if(attr == 16)
            attr = ThingAttrNoMoveAnimation;
        else if(attr > 16)
            attr -= 1;

        switch(attr) {
            case ThingAttrDisplacement: {
                m_displacement.x = fin->getU16();
                m_displacement.y = fin->getU16();
                m_attribs.set(attr, true);
                break;
            }
            case ThingAttrLight: {
                Light light;
                light.intensity = fin->getU16();
                light.color = fin->getU16();
                m_attribs.set(attr, light);
                break;
            }
            case ThingAttrMarket: {
                MarketData market;
                market.category = fin->getU16();
                market.tradeAs = fin->getU16();
                market.showAs = fin->getU16();
                market.name = fin->getString();
                market.restrictProfession = fin->getU16();
                market.requiredLevel = fin->getU16();
                m_attribs.set(attr, market);
                break;
            }
            case ThingAttrElevation: {
                m_elevation = fin->getU16();
                m_attribs.set(attr, m_elevation);
                break;
            }
            case ThingAttrUsable:
            case ThingAttrGround:
            case ThingAttrWritable:
            case ThingAttrWritableOnce:
            case ThingAttrMinimapColor:
            case ThingAttrCloth:
            case ThingAttrLensHelp:
                m_attribs.set(attr, fin->getU16());
                break;
            default:
                m_attribs.set(attr, true);
                break;
        };
    }

    if(!done)
        stdext::throw_exception(stdext::format("corrupt data (id: %d, category: %d, count: %d, lastAttr: %d)",
            m_id, m_category, count, attr));

    bool hasFrameGroups = (category == ThingCategoryCreature);
    uint8_t groupCount = hasFrameGroups ? fin->getU8() : 1;

    m_animationPhases = 0;
    int totalSpritesCount = 0;

    for(int i = 0; i < groupCount; ++i) {
        uint8_t frameGroupType = FrameGroupDefault;
        if(hasFrameGroups)
            frameGroupType = fin->getU8();

        uint8_t width = fin->getU8();
        uint8_t height = fin->getU8();
        m_size = Size(width, height);
        if(width > 1 || height > 1) {
            m_realSize = fin->getU8();
            m_exactSize = std::min<int>(m_realSize, std::max<int>(width * 32, height * 32));
        }
        else
            m_exactSize = 32;

        m_layers = fin->getU8();
        m_numPatternX = fin->getU8();
        m_numPatternY = fin->getU8();
        m_numPatternZ = fin->getU8();
        
        int groupAnimationsPhases = fin->getU8();
        m_animationPhases += groupAnimationsPhases;

        if(groupAnimationsPhases > 1) {
            m_animator = AnimatorPtr(new Animator);
            m_animator->unserialize(groupAnimationsPhases, fin);
        }

        int totalSprites = m_size.area() * m_layers * m_numPatternX * m_numPatternY * m_numPatternZ * groupAnimationsPhases;

        if((totalSpritesCount+totalSprites) > 4096)
            stdext::throw_exception("a thing type has more than 4096 sprites");

        m_spritesIndex.resize((totalSpritesCount+totalSprites));
        for(int i = totalSpritesCount; i < (totalSpritesCount+totalSprites); i++)
            m_spritesIndex[i] = fin->getU32();

        totalSpritesCount += totalSprites;
    }

    m_textures.resize(m_animationPhases);
    m_texturesFramesRects.resize(m_animationPhases);
    m_texturesFramesOriginRects.resize(m_animationPhases);
    m_texturesFramesOffsets.resize(m_animationPhases);
}

void ThingType::exportImage(std::string fileName)
{
    if(m_null)
        stdext::throw_exception("cannot export null thingtype");

    if(m_spritesIndex.size() == 0)
        stdext::throw_exception("cannot export thingtype without sprites");

    ImagePtr image(new Image(Size(32 * m_size.width() * m_layers * m_numPatternX, 32 * m_size.height() * m_animationPhases * m_numPatternY * m_numPatternZ)));
    for(int z = 0; z < m_numPatternZ; ++z) {
        for(int y = 0; y < m_numPatternY; ++y) {
            for(int x = 0; x < m_numPatternX; ++x) {
                for(int l = 0; l < m_layers; ++l) {
                    for(int a = 0; a < m_animationPhases; ++a) {
                        for(int w = 0; w < m_size.width(); ++w) {
                            for(int h = 0; h < m_size.height(); ++h) {
                                image->blit(Point(32 * (m_size.width() - w - 1 + m_size.width() * x + m_size.width() * m_numPatternX * l),
                                                  32 * (m_size.height() - h - 1 + m_size.height() * y + m_size.height() * m_numPatternY * a + m_size.height() * m_numPatternY * m_animationPhases * z)),
                                            g_sprites.getSpriteImage(m_spritesIndex[getSpriteIndex(w, h, l, x, y, z, a)]));
                            }
                        }
                    }
                }
            }
        }
    }

    image->savePNG(fileName);
}

void ThingType::unserializeOtml(const OTMLNodePtr& node)
{
    for(const OTMLNodePtr& node2 : node->children()) {
        if(node2->tag() == "opacity")
            m_opacity = node2->value<float>();
        else if(node2->tag() == "name-displacement")
            m_name_displacement = node2->value<Point>();
        else if(node2->tag() == "notprewalkable")
            m_attribs.set(ThingAttrNotPreWalkable, node2->value<bool>());
        else if(node2->tag() == "image")
            m_customImage = node2->value();
        else if(node2->tag() == "full-ground") {
            if(node2->value<bool>())
                m_attribs.set(ThingAttrFullGround, true);
            else
                m_attribs.remove(ThingAttrFullGround);
        }
    }
}

void ThingType::draw(const Point& dest, float scaleFactor, int layer, int xPattern, int yPattern, int zPattern, int animationPhase, LightView *lightView)
{
    if(m_null)
        return;

    if(animationPhase >= m_animationPhases)
        return;

    const TexturePtr& texture = getTexture(animationPhase); // texture might not exists, neither its rects.
    if(!texture)
        return;

    unsigned int frameIndex = getTextureIndex(layer, xPattern, yPattern, zPattern);
    if(frameIndex >= m_texturesFramesRects[animationPhase].size())
        return;

    Point textureOffset;
    Rect textureRect;

    if(scaleFactor != 1.0f) {
        textureRect = m_texturesFramesOriginRects[animationPhase][frameIndex];
    } else {
        textureOffset = m_texturesFramesOffsets[animationPhase][frameIndex];
        textureRect = m_texturesFramesRects[animationPhase][frameIndex];
    }

    Rect screenRect(dest + (textureOffset - m_displacement - (m_size.toPoint() - Point(1, 1)) * 32) * scaleFactor,
                    textureRect.size() * scaleFactor);

    bool useOpacity = m_opacity < 1.0f;

    if(useOpacity)
        g_painter->setColor(Color(1.0f,1.0f,1.0f,m_opacity));

    g_painter->drawTexturedRect(screenRect, texture, textureRect);

    if(useOpacity)
        g_painter->setColor(Color::white);

    if(lightView && hasLight()) {
        Light light = getLight();
        if(light.intensity > 0)
            lightView->addLightSource(screenRect.center(), scaleFactor, light);
    }
}

const TexturePtr& ThingType::getTexture(int animationPhase)
{
    TexturePtr& animationPhaseTexture = m_textures[animationPhase];
    if(!animationPhaseTexture) {
        bool useCustomImage = false;
        if(animationPhase == 0 && !m_customImage.empty())
            useCustomImage = true;

        // we don't need layers in common items, they will be pre-drawn
        int textureLayers = 1;
        int numLayers = m_layers;
        if(m_category == ThingCategoryCreature && numLayers >= 2) {
             // 5 layers: outfit base, red mask, green mask, blue mask, yellow mask
            textureLayers = 5;
            numLayers = 5;
        }

        int indexSize = textureLayers * m_numPatternX * m_numPatternY * m_numPatternZ;
        Size textureSize = getBestTextureDimension(m_size.width(), m_size.height(), indexSize);
        ImagePtr fullImage;

        if(useCustomImage)
            fullImage = Image::load(m_customImage);
        else
            fullImage = ImagePtr(new Image(textureSize * Otc::TILE_PIXELS));

        m_texturesFramesRects[animationPhase].resize(indexSize);
        m_texturesFramesOriginRects[animationPhase].resize(indexSize);
        m_texturesFramesOffsets[animationPhase].resize(indexSize);

        for(int z = 0; z < m_numPatternZ; ++z) {
            for(int y = 0; y < m_numPatternY; ++y) {
                for(int x = 0; x < m_numPatternX; ++x) {
                    for(int l = 0; l < numLayers; ++l) {
                        bool spriteMask = (m_category == ThingCategoryCreature && l > 0);
                        int frameIndex = getTextureIndex(l % textureLayers, x, y, z);
                        Point framePos = Point(frameIndex % (textureSize.width() / m_size.width()) * m_size.width(),
                                               frameIndex / (textureSize.width() / m_size.width()) * m_size.height()) * Otc::TILE_PIXELS;

                        if(!useCustomImage) {
                            for(int h = 0; h < m_size.height(); ++h) {
                                for(int w = 0; w < m_size.width(); ++w) {
                                    unsigned int spriteIndex = getSpriteIndex(w, h, spriteMask ? 1 : l, x, y, z, animationPhase);
                                    ImagePtr spriteImage = g_sprites.getSpriteImage(m_spritesIndex[spriteIndex]);
                                    if(spriteImage) {
                                        if(spriteMask) {
                                            static Color maskColors[] = { Color::red, Color::green, Color::blue, Color::yellow };
                                            spriteImage->overwriteMask(maskColors[l - 1]);
                                        }
                                        Point spritePos = Point(m_size.width()  - w - 1,
                                                                m_size.height() - h - 1) * Otc::TILE_PIXELS;

                                        fullImage->blit(framePos + spritePos, spriteImage);
                                    }
                                }
                            }
                        }

                        Rect drawRect(framePos + Point(m_size.width(), m_size.height()) * Otc::TILE_PIXELS - Point(1,1), framePos);
                        for(int x = framePos.x; x < framePos.x + m_size.width() * Otc::TILE_PIXELS; ++x) {
                            for(int y = framePos.y; y < framePos.y + m_size.height() * Otc::TILE_PIXELS; ++y) {
                                uint8_t *p = fullImage->getPixel(x,y);
                                if(p[3] != 0x00) {
                                    drawRect.setTop   (std::min<int>(y, (int)drawRect.top()));
                                    drawRect.setLeft  (std::min<int>(x, (int)drawRect.left()));
                                    drawRect.setBottom(std::max<int>(y, (int)drawRect.bottom()));
                                    drawRect.setRight (std::max<int>(x, (int)drawRect.right()));
                                }
                            }
                        }

                        m_texturesFramesRects[animationPhase][frameIndex] = drawRect;
                        m_texturesFramesOriginRects[animationPhase][frameIndex] = Rect(framePos, Size(m_size.width(), m_size.height()) * Otc::TILE_PIXELS);
                        m_textures[animationPhase] = animationPhaseTexture;
                        m_texturesFramesOffsets[animationPhase][frameIndex] = drawRect.topLeft() - framePos;
                    }
                }
            }
        }
        animationPhaseTexture = TexturePtr(new Texture(fullImage, true));
        animationPhaseTexture->setSmooth(true);
    }
    return animationPhaseTexture;
}

Size ThingType::getBestTextureDimension(int w, int h, int count)
{
    const int MAX = 32;

    int k = 1;
    while(k < w)
        k<<=1;
    w = k;

    k = 1;
    while(k < h)
        k<<=1;
    h = k;

    int numSprites = w*h*count;
    assert(numSprites <= MAX*MAX);
    assert(w <= MAX);
    assert(h <= MAX);

    Size bestDimension = Size(MAX, MAX);
    for(int i=w;i<=MAX;i<<=1) {
        for(int j=h;j<=MAX;j<<=1) {
            Size candidateDimension = Size(i, j);
            if(candidateDimension.area() < numSprites)
                continue;
            if((candidateDimension.area() < bestDimension.area()) ||
               (candidateDimension.area() == bestDimension.area() && candidateDimension.width() + candidateDimension.height() < bestDimension.width() + bestDimension.height()))
                bestDimension = candidateDimension;
        }
    }

    return bestDimension;
}

unsigned int ThingType::getSpriteIndex(int w, int h, int l, int x, int y, int z, int a) {
    unsigned int index =
        ((((((a % m_animationPhases)
        * m_numPatternZ + z)
        * m_numPatternY + y)
        * m_numPatternX + x)
        * m_layers + l)
        * m_size.height() + h)
        * m_size.width() + w;
    assert(index < m_spritesIndex.size());
    return index;
}

unsigned int ThingType::getTextureIndex(int l, int x, int y, int z) {
    return ((l * m_numPatternZ + z)
               * m_numPatternY + y)
               * m_numPatternX + x;
}

int ThingType::getExactSize(int layer, int xPattern, int yPattern, int zPattern, int animationPhase)
{
    if(m_null)
        return 0;

    getTexture(animationPhase); // we must calculate it anyway.
    int frameIndex = getTextureIndex(layer, xPattern, yPattern, zPattern);
    Size size = m_texturesFramesOriginRects[animationPhase][frameIndex].size() - m_texturesFramesOffsets[animationPhase][frameIndex].toSize();
    return std::max<int>(size.width(), size.height());
}

void ThingType::setPathable(bool var)
{
    if(var == true)
        m_attribs.remove(ThingAttrNotPathable);
    else
        m_attribs.set(ThingAttrNotPathable, true);
}