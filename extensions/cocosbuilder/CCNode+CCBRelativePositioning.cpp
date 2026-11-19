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

#include "cocosbuilder/CCBCompat.h"
#include "cocosbuilder/CCNode+CCBRelativePositioning.h"
#include "cocosbuilder/CCBReader.h"

using namespace ax;

namespace ax::ext {

AX_EXT_API Vec2 getAbsolutePosition(const Vec2 &pt, CCBReader::PositionType type, const Vec2 &containerSize, const std::string& /*propName*/)
{
    Vec2 absPt;
    if (type == CCBReader::PositionType::RELATIVE_BOTTOM_LEFT)
    {
        absPt = pt;
    }
    else if (type == CCBReader::PositionType::RELATIVE_TOP_LEFT)
    {
        absPt.x = pt.x;
        absPt.y = containerSize.y - pt.y;
    }
    else if (type == CCBReader::PositionType::RELATIVE_TOP_RIGHT)
    {
        absPt.x = containerSize.x - pt.x;
        absPt.y = containerSize.y - pt.y;
    }
    else if (type == CCBReader::PositionType::RELATIVE_BOTTOM_RIGHT)
    {
        absPt.x = containerSize.x - pt.x;
        absPt.y = pt.y;
    }
    else if (type == CCBReader::PositionType::PERCENT)
    {
        absPt.x = (int)(containerSize.x * pt.x / 100.0f);
        absPt.y = (int)(containerSize.y * pt.y / 100.0f);
    }
    else if (type == CCBReader::PositionType::MULTIPLY_RESOLUTION)
    {
        float resolutionScale = CCBReader::getResolutionScale();
        
        absPt.x = pt.x * resolutionScale;
        absPt.y = pt.y * resolutionScale;
    }
    
    return absPt;
}

AX_EXT_API void setRelativeScale(Node *pNode, float scaleX, float scaleY, CCBReader::ScaleType type, const std::string& /*propName*/)
{
    AXASSERT(pNode, "pNode should not be null");
    
    if (type == CCBReader::ScaleType::MULTIPLY_RESOLUTION)
    {
        float resolutionScale = CCBReader::getResolutionScale();
        
        scaleX *= resolutionScale;
        scaleY *= resolutionScale;
    }
    
    pNode->setScaleX(scaleX);
    pNode->setScaleY(scaleY);
}

}
