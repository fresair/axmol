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

#ifndef _CCB_CCNODELOADER_H_
#define _CCB_CCNODELOADER_H_

#include "cocosbuilder/CCBCompat.h"
#include "GUI/ControlExtension/Invocation.h"
#include "cocosbuilder/CCBReader.h"
#include "GUI/ControlExtension/Control.h"

namespace ax::ext {

#define PROPERTY_POSITION "position"
#define PROPERTY_CONTENTSIZE "contentSize"
#define PROPERTY_SKEW "skew"
#define PROPERTY_ANCHORPOINT "anchorPoint"
#define PROPERTY_SCALE "scale"
#define PROPERTY_ROTATION "rotation"
#define PROPERTY_ROTATIONX "rotationX"
#define PROPERTY_ROTATIONY "rotationY"
#define PROPERTY_TAG "tag"
#define PROPERTY_IGNOREANCHORPOINTFORPOSITION "ignoreAnchorPointForPosition"
#define PROPERTY_VISIBLE "visible"

#define ASSERT_FAIL_UNEXPECTED_PROPERTY(PROPERTY) CCB_LOG("Unexpected property: '%s'!\n", PROPERTY); assert(false)
#define ASSERT_FAIL_UNEXPECTED_PROPERTYTYPE(PROPERTYTYPE) CCB_LOG("Unexpected property type: '%d'!\n", PROPERTYTYPE); assert(false)

#define CCB_VIRTUAL_NEW_AUTORELEASE_CREATECCNODE_METHOD(T) virtual ax::Node * createNode(ax::Node * pParent, ax::ext::CCBReader * ccbReader) { \
    return T::create(); \
}

#define CCB_PURE_VIRTUAL_NEW_AUTORELEASE_CREATECCNODE_METHOD(T) virtual ax::Node * createNode(ax::Node * pParent, ax::ext::CCBReader * ccbReader) = 0

struct BlockData {
    ax::SEL_MenuHandler mSELMenuHandler;
    ax::Object * _target;
};

struct BlockControlData {
    ax::ext::Control::Handler mSELControlHandler;
    ax::Object * _target;
    ax::ext::Control::EventType mControlEvents;
};

/* Forward declaration. */
class CCBReader;

class AX_EXT_API NodeLoader : public ax::Object {
    public:
        /**
         * @js NA
         * @lua NA
         */
        NodeLoader();
        /**
         * @js NA
         * @lua NA
         */
        virtual ~NodeLoader();
        /**
         * @js NA
         * @lua NA
         */
        CCB_STATIC_NEW_AUTORELEASE_OBJECT_METHOD(NodeLoader, loader);
        /**
         * @js NA
         * @lua NA
         */
        virtual ax::Node * loadNode(ax::Node *, CCBReader * ccbReader);
        /**
         * @js NA
         * @lua NA
         */
        virtual void parseProperties(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        /**
         * @js NA
         * @lua NA
         */
        virtual ax::ValueMap& getCustomProperties();
    
    protected:
        CCB_VIRTUAL_NEW_AUTORELEASE_CREATECCNODE_METHOD(ax::Node);

        virtual ax::Vec2 parsePropTypePosition(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual ax::Vec2 parsePropTypePoint(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual ax::Vec2 parsePropTypePointLock(ax::Node * pNode,ax:: Node * pParent, CCBReader * ccbReader);
        virtual ax::Vec2 parsePropTypeSize(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual float * parsePropTypeScaleLock(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual float parsePropTypeFloat(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual float parsePropTypeDegrees(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual float parsePropTypeFloatScale(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual int parsePropTypeInteger(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual int parsePropTypeIntegerLabeled(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual float * parsePropTypeFloatVar(ax::Node * pNode,ax::Node * pParent, CCBReader * ccbReader);
        virtual bool parsePropTypeCheck(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual ax::SpriteFrame * parsePropTypeSpriteFrame(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual ax::Animation * parsePropTypeAnimation(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual ax::Texture2D * parsePropTypeTexture(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual unsigned char parsePropTypeByte(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual ax::Color32 parsePropTypeColor3(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader, const char *pPropertyName);
        virtual ax::Color * parsePropTypeColor4FVar(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual bool * parsePropTypeFlip(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual ax::BlendFunc parsePropTypeBlendFunc(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual std::string parsePropTypeFntFile(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual std::string parsePropTypeString(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual std::string parsePropTypeText(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual std::string parsePropTypeFontTTF(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual BlockData * parsePropTypeBlock(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual BlockControlData * parsePropTypeBlockControl(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual ax::Node * parsePropTypeCCBFile(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);
        virtual float * parsePropTypeFloatXY(ax::Node * pNode, ax::Node * pParent, CCBReader * ccbReader);


        virtual void onHandlePropTypePosition(ax::Node * pNode,ax:: Node * pParent, const char* pPropertyName, ax::Vec2 pPosition, CCBReader * ccbReader);
        virtual void onHandlePropTypePoint(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Vec2 pPoint, CCBReader * ccbReader);
        virtual void onHandlePropTypePointLock(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Vec2 pPointLock, CCBReader * ccbReader);
        virtual void onHandlePropTypeSize(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Vec2 pSize, CCBReader * ccbReader);
        virtual void onHandlePropTypeScaleLock(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, float * pScaleLock, CCBReader * ccbReader);
        virtual void onHandlePropTypeFloat(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, float pFloat, CCBReader * ccbReader);
        virtual void onHandlePropTypeDegrees(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, float pDegrees, CCBReader * ccbReader);
        virtual void onHandlePropTypeFloatScale(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, float pFloatScale, CCBReader * ccbReader);
        virtual void onHandlePropTypeInteger(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, int pInteger, CCBReader * ccbReader);
        virtual void onHandlePropTypeIntegerLabeled(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, int pIntegerLabeled, CCBReader * ccbReader);
        virtual void onHandlePropTypeFloatVar(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, float * pFoatVar, CCBReader * ccbReader);
        virtual void onHandlePropTypeFloatXY(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, float * pFoatVar, CCBReader * ccbReader);

        virtual void onHandlePropTypeCheck(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, bool pCheck, CCBReader * ccbReader);
        virtual void onHandlePropTypeSpriteFrame(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::SpriteFrame * pSpriteFrame, CCBReader * ccbReader);
        virtual void onHandlePropTypeAnimation(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Animation * pAnimation, CCBReader * ccbReader);
        virtual void onHandlePropTypeTexture(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Texture2D * pTexture2D, CCBReader * ccbReader);
        virtual void onHandlePropTypeByte(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, unsigned char pByte, CCBReader * ccbReader);
        virtual void onHandlePropTypeColor3(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Color32 pColor3B, CCBReader * ccbReader);
        virtual void onHandlePropTypeColor4FVar(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Color * pColor4FVar, CCBReader * ccbReader);
        virtual void onHandlePropTypeFlip(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, bool * pFlip, CCBReader * ccbReader);
        virtual void onHandlePropTypeBlendFunc(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::BlendFunc pBlendFunc, CCBReader * ccbReader);
        virtual void onHandlePropTypeFntFile(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, const char * pFntFile, CCBReader * ccbReader);
        virtual void onHandlePropTypeString(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, const char * pString, CCBReader * ccbReader);
        virtual void onHandlePropTypeText(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, const char * pText, CCBReader * ccbReader);
        virtual void onHandlePropTypeFontTTF(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, const char * pFontTTF, CCBReader * ccbReader);
        virtual void onHandlePropTypeBlock(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, BlockData * pBlockData, CCBReader * ccbReader);
        virtual void onHandlePropTypeBlockControl(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, BlockControlData * pBlockControlData, CCBReader * ccbReader);
        virtual void onHandlePropTypeCCBFile(ax::Node * pNode, ax::Node * pParent, const char* pPropertyName, ax::Node * pCCBFileNode, CCBReader * ccbReader);

protected:
        ax::ValueMap _customProperties;
};

}

#endif
