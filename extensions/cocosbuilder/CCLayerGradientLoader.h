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

#ifndef _CCB_CCLAYERGRADIENTLOADER_H_
#define _CCB_CCLAYERGRADIENTLOADER_H_

#include "cocosbuilder/CCBCompat.h"
#include "cocosbuilder/CCLayerLoader.h"

namespace ax::ext {

/* Forward declaration. */
class CCBReader;

class AX_EXT_API LayerGradientLoader : public LayerLoader {
public:
    /**
     * @js NA
     * @lua NA
     */
    virtual ~LayerGradientLoader() {};
    CCB_STATIC_NEW_AUTORELEASE_OBJECT_METHOD(LayerGradientLoader, loader);

protected:
    CCB_VIRTUAL_NEW_AUTORELEASE_CREATECCNODE_METHOD(ax::LayerGradient);

    virtual void onHandlePropTypeColor3(ax::Node * pNode, ax::Node * pParent, const char * pPropertyName, ax::Color32 pColor3B, CCBReader * ccbReader);
    virtual void onHandlePropTypeByte(ax::Node * pNode, ax::Node * pParent, const char * pPropertyName, unsigned char pByte, CCBReader * ccbReader);
    virtual void onHandlePropTypePoint(ax::Node * pNode, ax::Node * pParent, const char * pPropertyName, ax::Vec2 pPoint, CCBReader * ccbReader);
    virtual void onHandlePropTypeBlendFunc(ax::Node * pNode, ax::Node * pParent, const char * pPropertyName, ax::BlendFunc pBlendFunc, CCBReader * ccbReader);
};

}

#endif
