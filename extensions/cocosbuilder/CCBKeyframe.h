/****************************************************************************
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 
 http://www.cocos2d-x.org
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#ifndef __CCB_KEYFRAME_H__
#define __CCB_KEYFRAME_H__

#include "cocosbuilder/CCBCompat.h"
#include "axmol/base/Object.h"
#include "axmol/base/Value.h"


namespace ax::ext {

class AX_EXT_API CCBKeyframe : public ax::Object
{
public:
    enum class EasingType
    {
        INSTANT,
        
        LINEAR,
        
        CUBIC_IN,
        CUBIC_OUT,
        CUBIC_INOUT,
        
        ELASTIC_IN,
        ELASTIC_OUT,
        ELASTIC_INOUT,
        
        BOUNCE_IN,
        BOUNCE_OUT,
        BOUNCE_INOUT,
        
        BACK_IN,
        BACK_OUT,
        BACK_INOUT,
    };
    /**
     * @js ctor
     */
    CCBKeyframe();
    /**
     * @js NA
     * @lua NA
     */
    ~CCBKeyframe();
    
    const ax::Value& getValue() const;
    void setValue(const ax::Value& value);
    
    ax::Object* getObject() const;
    void setObject(ax::Object* obj);
    
    float getTime();
    void setTime(float fTime);
    
    EasingType getEasingType();
    void setEasingType(EasingType easingType);
    
    float getEasingOpt();
    void setEasingOpt(float fEasingOpt);
    
private:
    ax::Value _value;
    ax::Object* _object;
    float _time;
    EasingType _easingType;
    float _easingOpt;
};

}

#endif // __CCB_KEYFRAME_H__
