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
#include "cocosbuilder/CCLayerLoader.h"


using namespace ax;

#define PROPERTY_TOUCH_ENABLED "isTouchEnabled"
#define PROPERTY_ACCELEROMETER_ENABLED "isAccelerometerEnabled"
#define PROPERTY_MOUSE_ENABLED "isMouseEnabled"
#define PROPERTY_KEYBOARD_ENABLED "isKeyboardEnabled"

#if defined(__GNUC__) && ((__GNUC__ >= 4) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1)))
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif _MSC_VER >= 1400 //vs 2005 or higher
#pragma warning (push)
#pragma warning (disable: 4996)
#endif

using namespace ax;

namespace ax::ext {
  
void LayerLoader::onHandlePropTypeCheck(Node * pNode, Node * pParent, const char * pPropertyName, bool pCheck, CCBReader * ccbReader) {
    if(strcmp(pPropertyName, PROPERTY_TOUCH_ENABLED) == 0) {
        ccbReader->applyNodeProperty(pNode, "setTouchEnabled", Value(pCheck));
    } else if(strcmp(pPropertyName, PROPERTY_ACCELEROMETER_ENABLED) == 0) {
        ccbReader->applyNodeProperty(pNode, "setAccelerometerEnabled", Value(pCheck));
    } else if(strcmp(pPropertyName, PROPERTY_MOUSE_ENABLED) == 0) {
        ccbReader->applyNodeProperty(pNode, "setMouseEnabled", Value(pCheck));
    } else if(strcmp(pPropertyName, PROPERTY_KEYBOARD_ENABLED) == 0) {
        ccbReader->applyNodeProperty(pNode, "setKeyboardEnabled", Value(pCheck));
    } else {
        NodeLoader::onHandlePropTypeCheck(pNode, pParent, pPropertyName, pCheck, ccbReader);
    }
}

}

#if defined(__GNUC__) && ((__GNUC__ >= 4) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1)))
#pragma GCC diagnostic warning "-Wdeprecated-declarations"
#elif _MSC_VER >= 1400 //vs 2005 or higher
#pragma warning (pop)
#endif
