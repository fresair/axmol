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

#ifndef _CCB_CCBMEMBERVARIABLEASSIGNER_H_
#define _CCB_CCBMEMBERVARIABLEASSIGNER_H_


#include "cocosbuilder/CCBCompat.h"

namespace ax::ext {

#define CCB_MEMBERVARIABLEASSIGNER_GLUE(TARGET, MEMBERVARIABLENAME, MEMBERVARIABLETYPE, MEMBERVARIABLE) \
    if (pTarget == TARGET && 0 == strcmp(pMemberVariableName, (MEMBERVARIABLENAME))) { \
        MEMBERVARIABLETYPE pOldVar = MEMBERVARIABLE; \
        MEMBERVARIABLE = dynamic_cast<MEMBERVARIABLETYPE>(pNode); \
        AX_ASSERT(MEMBERVARIABLE); \
        if (pOldVar != MEMBERVARIABLE) { \
            AX_SAFE_RELEASE(pOldVar); \
            MEMBERVARIABLE->retain(); \
        } \
        return true; \
    }

#define CCB_MEMBERVARIABLEASSIGNER_GLUE_WEAK(TARGET, MEMBERVARIABLENAME, MEMBERVARIABLETYPE, MEMBERVARIABLE) \
    if (pTarget == TARGET && 0 == strcmp(pMemberVariableName, MEMBERVARIABLENAME)) { \
        MEMBERVARIABLE = dynamic_cast<MEMBERVARIABLETYPE>(pNode); \
        AX_ASSERT(MEMBERVARIABLE); \
        return true; \
    }

class AX_EXT_API CCBMemberVariableAssigner {
    public:
        /**
         * @js NA
         * @lua NA
         */
        virtual ~CCBMemberVariableAssigner() {};

        /**
         *  The callback function of assigning member variable.
         *  @note The member variable must be Node or its subclass.
         *  @param target               The custom class.
         *  @param memberVariableName   The name of the member variable.
         *  @param node                 The member variable.
         *  @return Whether the assignment was successful.
         */
        virtual bool onAssignCCBMemberVariable(ax::Object* target, const char* memberVariableName, ax::Node* node) = 0;

        /**
         *  The callback function of assigning custom properties.
         *  @note The member variable must be Integer, Float, Boolean or String.
         *  @param target               The custom class.
         *  @param memberVariableName   The name of the member variable.
         *  @param value                The value of the property.
         *  @return Whether the assignment was successful.
         */
        virtual bool onAssignCCBCustomProperty(ax::Object* target, const char* memberVariableName, const ax::Value& value) { return false; };
};

}

#endif

