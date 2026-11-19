/*
 * Copyright (c) 2012 cocos2d-x.org
 * https://axmol.dev/
 *
 * Copyright 2012 Stewart Hamilton-Arrandale.
 * http://creativewax.co.uk
 *
 * Modified by Yannick Loriot.
 * http://yannickloriot.com
 *
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
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
 *
 * converted to c++ / cocos2d-x by Angus C
 */

#include "ControlColourPicker.h"
#include "axmol/2d/SpriteFrameCache.h"
#include "axmol/2d/SpriteBatchNode.h"
#include "axmol/platform/FileUtils.h"

NS_AX_EXT_BEGIN

ControlColourPicker::ControlColourPicker() : _colourPicker(nullptr), _huePicker(nullptr), _background(nullptr) {}

ControlColourPicker::~ControlColourPicker()
{
    AX_SAFE_RELEASE(_background);
    AX_SAFE_RELEASE(_huePicker);
    AX_SAFE_RELEASE(_colourPicker);
}

bool ControlColourPicker::init()
{
    if (Control::init())
    {
        // Older Cocos2d-x projects ship the same atlas with the CC prefix.
        // Prefer the current names when present; do not mask corrupt assets.
        auto* files = FileUtils::getInstance();
        const char* plist = "extensions/ControlColourPickerSpriteSheet.plist";
        const char* image = "extensions/ControlColourPickerSpriteSheet.png";
        if (!files->isFileExist(plist))
            plist = "extensions/CCControlColourPickerSpriteSheet.plist";
        if (!files->isFileExist(image))
            image = "extensions/CCControlColourPickerSpriteSheet.png";
        if (!files->isFileExist(plist) || !files->isFileExist(image))
            return false;

        // Create the sprite batch node
        SpriteBatchNode* spriteSheet = SpriteBatchNode::create(image);
        if (!spriteSheet || !spriteSheet->getTexture())
            return false;
        // Both the atlas frames and the batch must use this actual texture.
        SpriteFrameCache::getInstance()->addSpriteFramesWithFile(plist, spriteSheet->getTexture());
        addChild(spriteSheet);

        // MIPMAP
        //        ccTexParams params  = {GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_REPEAT, GL_REPEAT};
        /* Comment next line to avoid something like mosaic in 'ControlExtensionTest',
           especially the display of 'huePickerBackground.png' when in 800*480 window size with 480*320 design
           resolution and hd(960*640) resources.
    */
        //        spriteSheet->getTexture()->setAliasTexParameters();
        //         spriteSheet->getTexture()->setSamplerDescriptor(&params);

        // Init default color
        _hsv.h = 0;
        _hsv.s = 0;
        _hsv.v = 0;

        // Add image
        _background = ControlUtils::addSpriteToTargetWithPosAndAnchor("menuColourPanelBackground.png", spriteSheet,
                                                                      Vec2::zero, Vec2(0.5f, 0.5f));
        if (!_background)
            return false;
        AX_SAFE_RETAIN(_background);

        Vec2 backgroundPointZero = _background->getPosition() - Vec2(_background->getContentSize().width / 2,
                                                                     _background->getContentSize().height / 2);

        // Setup panels
        float hueShift    = 8;
        float colourShift = 28;

        _huePicker = new ControlHuePicker();
        if (!_huePicker->initWithTargetAndPos(
                spriteSheet, Vec2(backgroundPointZero.x + hueShift, backgroundPointZero.y + hueShift)))
            return false;
        _colourPicker = new ControlSaturationBrightnessPicker();
        if (!_colourPicker->initWithTargetAndPos(
                spriteSheet, Vec2(backgroundPointZero.x + colourShift, backgroundPointZero.y + colourShift)))
            return false;

        // Setup events
        _huePicker->addTargetWithActionForControlEvents(
            this, cccontrol_selector(ControlColourPicker::hueSliderValueChanged), Control::EventType::VALUE_CHANGED);
        _colourPicker->addTargetWithActionForControlEvents(
            this, cccontrol_selector(ControlColourPicker::colourSliderValueChanged), Control::EventType::VALUE_CHANGED);

        // Set defaults
        setLegacyCompatibilityEnabled(_legacyCompatibilityEnabled);
        updateHueAndControlPicker();
        addChild(_huePicker);
        addChild(_colourPicker);

        // Set content size
        setContentSize(_background->getContentSize());
        return true;
    }
    else
        return false;
}

ControlColourPicker* ControlColourPicker::create()
{
    ControlColourPicker* pRet = new ControlColourPicker();
    if (pRet->init())
    {
        pRet->autorelease();
        return pRet;
    }
    delete pRet;
    return nullptr;
}

void ControlColourPicker::setColor(const Color32& color)
{
    // FIXME: fixed me if not correct
    Control::setColor(color);

    RGBA rgba;
    rgba.r = color.r / 255.0f;
    rgba.g = color.g / 255.0f;
    rgba.b = color.b / 255.0f;
    rgba.a = color.a / 255.0f;

    _hsv = ControlUtils::HSVfromRGB(rgba);
    updateHueAndControlPicker();
}

void ControlColourPicker::setEnabled(bool enabled)
{
    Control::setEnabled(enabled);
    if (_huePicker != nullptr)
    {
        _huePicker->setEnabled(enabled);
    }
    if (_colourPicker)
    {
        _colourPicker->setEnabled(enabled);
    }
}

void ControlColourPicker::setLegacyCompatibilityEnabled(bool enabled)
{
    _legacyCompatibilityEnabled = enabled;
    if (_huePicker)
        _huePicker->setLegacyCompatibilityEnabled(enabled);
    if (_colourPicker)
        _colourPicker->setLegacyCompatibilityEnabled(enabled);
}

void ControlColourPicker::setcolourPicker(ControlSaturationBrightnessPicker* picker)
{
    if (_colourPicker != picker)
    {
        AX_SAFE_RETAIN(picker);
        AX_SAFE_RELEASE(_colourPicker);
        _colourPicker = picker;
    }
    if (_legacyCompatibilityEnabled && _colourPicker)
        _colourPicker->setLegacyCompatibilityEnabled(true);
}

void ControlColourPicker::setHuePicker(ControlHuePicker* picker)
{
    if (_huePicker != picker)
    {
        AX_SAFE_RETAIN(picker);
        AX_SAFE_RELEASE(_huePicker);
        _huePicker = picker;
    }
    if (_legacyCompatibilityEnabled && _huePicker)
        _huePicker->setLegacyCompatibilityEnabled(true);
}

// need two events to prevent an infinite loop! (can't update huePicker when the huePicker triggers the callback due to
// Control::EventType::VALUE_CHANGED)
void ControlColourPicker::updateControlPicker()
{
    _huePicker->setHue(_hsv.h);
    _colourPicker->updateWithHSV(_hsv);
}

void ControlColourPicker::updateHueAndControlPicker()
{
    _huePicker->setHue(_hsv.h);
    _colourPicker->updateWithHSV(_hsv);
    _colourPicker->updateDraggerWithHSV(_hsv);
}

void ControlColourPicker::hueSliderValueChanged(Object* sender, Control::EventType /*controlEvent*/)
{
    _hsv.h = ((ControlHuePicker*)sender)->getHue();

    // Update the value
    RGBA rgba = ControlUtils::RGBfromHSV(_hsv);
    // FIXME: fixed me if not correct
    if (_legacyCompatibilityEnabled)
        Control::setColor(Color32(static_cast<uint8_t>(rgba.r * 255.0f), static_cast<uint8_t>(rgba.g * 255.0f),
                                 static_cast<uint8_t>(rgba.b * 255.0f), getOpacity()));
    else
        Control::setColor(Color32(rgba));

    // Send Control callback
    sendActionsForControlEvents(Control::EventType::VALUE_CHANGED);
    updateControlPicker();
}

void ControlColourPicker::colourSliderValueChanged(Object* sender, Control::EventType /*controlEvent*/)
{
    _hsv.s = ((ControlSaturationBrightnessPicker*)sender)->getSaturation();
    _hsv.v = ((ControlSaturationBrightnessPicker*)sender)->getBrightness();

    // Update the value
    RGBA rgba = ControlUtils::RGBfromHSV(_hsv);
    // FIXME: fixed me if not correct
    if (_legacyCompatibilityEnabled)
        Control::setColor(Color32(static_cast<uint8_t>(rgba.r * 255.0f), static_cast<uint8_t>(rgba.g * 255.0f),
                                 static_cast<uint8_t>(rgba.b * 255.0f), getOpacity()));
    else
        Control::setColor(Color32(rgba));

    // Send Control callback
    sendActionsForControlEvents(Control::EventType::VALUE_CHANGED);
}

// ignore all touches, handled by children
bool ControlColourPicker::onPointerDown(PointerEvent* /*event*/)
{
    return false;
}

NS_AX_EXT_END
