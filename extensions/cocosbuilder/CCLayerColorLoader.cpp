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
#include "cocosbuilder/CCLayerColorLoader.h"

using namespace ax;

namespace ax::ext {

#define PROPERTY_COLOR "color"
#define PROPERTY_OPACITY "opacity"
#define PROPERTY_BLENDFUNC "blendFunc"

void LayerColorLoader::onHandlePropTypeColor3(Node * pNode, Node * pParent, const char * pPropertyName, Color32 pColor3B, CCBReader * ccbReader) {
    if(strcmp(pPropertyName, PROPERTY_COLOR) == 0) {
        ccbSetColorRGB(pNode, pColor3B);
    } else {
        LayerLoader::onHandlePropTypeColor3(pNode, pParent, pPropertyName, pColor3B, ccbReader);
    }
}

void LayerColorLoader::onHandlePropTypeByte(Node * pNode, Node * pParent, const char * pPropertyName, unsigned char pByte, CCBReader * ccbReader) {
    if(strcmp(pPropertyName, PROPERTY_OPACITY) == 0) {
        ((LayerColor *)pNode)->setOpacity(pByte);
    } else {
        LayerLoader::onHandlePropTypeByte(pNode, pParent, pPropertyName, pByte, ccbReader);
    }
}

void LayerColorLoader::onHandlePropTypeBlendFunc(Node * pNode, Node * pParent, const char * pPropertyName, BlendFunc pBlendFunc, CCBReader * ccbReader) {
    if(strcmp(pPropertyName, PROPERTY_BLENDFUNC) == 0) {
        ((LayerColor *)pNode)->setBlendFunc(pBlendFunc);
    } else {
        LayerLoader::onHandlePropTypeBlendFunc(pNode, pParent, pPropertyName, pBlendFunc, ccbReader);
    }
}

}
