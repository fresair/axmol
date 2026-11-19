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
 * Converted to c++ / cocos2d-x by Angus C
 */

#pragma once

#include "Control.h"
#include "ControlUtils.h"
#include "ControlHuePicker.h"
#include "ControlSaturationBrightnessPicker.h"
#include "extensions/ExtensionExport.h"

NS_AX_EXT_BEGIN

/**
 * @addtogroup GUI
 * @{
 * @addtogroup control_extension
 * @{
 */

class AX_EXT_API ControlColourPicker : public Control
{
public:
    static ControlColourPicker* create();
    /**
     * @lua new
     */
    ControlColourPicker();
    /**
     * @lua NA
     */
    virtual ~ControlColourPicker();

    bool init() override;

    void setColor(const Color32& colorValue) override;
    void setEnabled(bool bEnabled) override;

    /** Enables the v3 native picker contract and its actual child controls.
     * @script {ignore}
     */
    void setLegacyCompatibilityEnabled(bool enabled);
    /** @script {ignore} */
    bool isLegacyCompatibilityEnabled() const { return _legacyCompatibilityEnabled; }

    // virtual ~ControlColourPicker();
    void hueSliderValueChanged(Object* sender, Control::EventType controlEvent);
    void colourSliderValueChanged(Object* sender, Control::EventType controlEvent);

protected:
    void updateControlPicker();
    void updateHueAndControlPicker();
    bool onPointerDown(PointerEvent* event) override;

    HSV _hsv;

private:
    ControlSaturationBrightnessPicker* _colourPicker;

public:
    virtual ControlSaturationBrightnessPicker* getcolourPicker() const { return _colourPicker; }
    virtual void setcolourPicker(ControlSaturationBrightnessPicker* picker);

private:
    ControlHuePicker* _huePicker;

public:
    virtual ControlHuePicker* getHuePicker() const { return _huePicker; }
    virtual void setHuePicker(ControlHuePicker* picker);

    AX_SYNTHESIZE_RETAIN(Sprite*, _background, Background)

private:
    bool _legacyCompatibilityEnabled{false};
};

// end of GUI group
/// @}
/// @}

NS_AX_EXT_END
