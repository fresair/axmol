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

#ifndef __CCB_CCBANIMATION_MANAGER_H__
#define __CCB_CCBANIMATION_MANAGER_H__

#include "cocosbuilder/CCBCompat.h"
#include "axmol/base/Map.h"
#include "axmol/base/WeakPtr.h"
#include "axmol/2d/ActionInterval.h"
#include "axmol/2d/ActionInstant.h"
#include "axmol/2d/ActionEase.h"

#include "extensions/ExtensionMacros.h"
#include "cocosbuilder/CCBSequence.h"
#include "cocosbuilder/CCBKeyframe.h"
#include "cocosbuilder/CCBSequenceProperty.h"
#include "GUI/ControlExtension/Control.h"

namespace ax::ext {

class AX_EXT_API CCBAnimationManagerDelegate
{
public:
    virtual ~CCBAnimationManagerDelegate() {}
    virtual void completedAnimationSequenceNamed(const char *name) = 0;
};

class AX_EXT_API CCBAnimationManager : public ax::Object
{
public:
    bool _jsControlled;
    /**
     * @js ctor
     */
    CCBAnimationManager();
    /**
     * @js NA
     * @lua NA
     */
    ~CCBAnimationManager();


    ax::WeakPtr<ax::Object> _owner;
    
    virtual bool init();
    
    ax::Vector<CCBSequence*>& getSequences();
    void setSequences(const ax::Vector<CCBSequence*>& seq);

    
    int getAutoPlaySequenceId();
    void setAutoPlaySequenceId(int autoPlaySequenceId);
    
    ax::Node* getRootNode();
    void setRootNode(ax::Node* pRootNode); // weak reference    
    

    void addDocumentCallbackNode(ax::Node *node);
    void addDocumentCallbackName(const std::string& name);
    void addDocumentCallbackControlEvents(ax::ext::Control::EventType eventType);
    
    void addDocumentOutletNode(ax::Node *node);
    void addDocumentOutletName(const std::string& name);

    void setDocumentControllerName(const std::string &name);
    
    std::string getDocumentControllerName();
    ax::ValueVector& getDocumentCallbackNames();
    ax::Vector<ax::Node*>& getDocumentCallbackNodes();
    ax::ValueVector& getDocumentCallbackControlEvents();
    
    ax::ValueVector& getDocumentOutletNames();
    ax::Vector<ax::Node*>& getDocumentOutletNodes();
    std::string getLastCompletedSequenceName();
    
    ax::ValueVector& getKeyframeCallbacks();
    
    const ax::Vec2& getRootContainerSize();
    void setRootContainerSize(const ax::Vec2 &rootContainerSize);
    
    CCBAnimationManagerDelegate* getDelegate();
    void setDelegate(CCBAnimationManagerDelegate* pDelegate); // retain
    
    const char* getRunningSequenceName();
    
    const ax::Vec2& getContainerSize(ax::Node* pNode);
    
    void addNode(ax::Node *pNode, const std::unordered_map<int, ax::Map<std::string, CCBSequenceProperty*>>& seq);
    void setBaseValue(const ax::Value& value, ax::Node *pNode, const std::string& propName);
    void setObject(ax::Object* obj, ax::Node *pNode, const std::string& propName);
    
    void moveAnimationsFromNode(ax::Node* fromNode, ax::Node* toNode);

    /** @deprecated This interface will be deprecated sooner or later.*/
    [[deprecated("Use the corresponding current method")]] void runAnimations(const char *pName, float fTweenDuration);
    /** @deprecated This interface will be deprecated sooner or later.*/
    [[deprecated("Use the corresponding current method")]] void runAnimations(const char *pName);
    /** @deprecated This interface will be deprecated sooner or later.*/
    [[deprecated("Use the corresponding current method")]] void runAnimations(int nSeqId, float fTweenDuraiton);

    void runAnimationsForSequenceNamedTweenDuration(const char *pName, float fTweenDuration);
    void runAnimationsForSequenceNamed(const char *pName);
    void runAnimationsForSequenceIdTweenDuration(int nSeqId, float fTweenDuraiton);
    /**
     * when this function bound to js ,the second param are callfunc_selector
     * @lua NA
     */
    void setAnimationCompletedCallback(ax::Object *target, ax::SEL_CallFunc callbackFunc);

    void debug();
    /**
     * @js setCallFuncForJSCallbackNamed
     */
    void setCallFunc(ax::CallFunc *callFunc, const std::string &callbackNamed);

    ax::Sequence* actionForCallbackChannel(CCBSequenceProperty* channel);
    ax::Sequence* actionForSoundChannel(CCBSequenceProperty* channel);

	// return -1 if timeline not exist
    int getSequenceId(const char* pSequenceName);
    
    // get timeline duration
    float getSequenceDuration(const char* pSequenceName);
    
private:
    const ax::Value& getBaseValue(ax::Node *pNode, const std::string& propName);
    Object* getObject(ax::Node *pNode, const std::string& propName);
    
    CCBSequence* getSequence(int nSequenceId);
    ax::ActionInterval* getAction(CCBKeyframe *pKeyframe0, CCBKeyframe *pKeyframe1, const std::string& propName, ax::Node *pNode);
    void setAnimatedProperty(const std::string& propName, ax::Node *pNode, const ax::Value& value, Object* obj, float fTweenDuration);
    void setFirstFrame(ax::Node *pNode, CCBSequenceProperty *pSeqProp, float fTweenDuration);
    ax::ActionInterval* getEaseAction(ax::ActionInterval *pAction, CCBKeyframe::EasingType easingType, float fEasingOpt);
    void runAction(ax::Node *pNode, CCBSequenceProperty *pSeqProp, float fTweenDuration);
    void sequenceCompleted();
    
private:
    ax::Vector<CCBSequence*> _sequences;
    std::unordered_map<ax::Node*, std::unordered_map<int, ax::Map<std::string, CCBSequenceProperty*>>> _nodeSequences;
    std::unordered_map<ax::Node*, ax::WeakPtr<ax::Node>> _nodeLifetimes;
    std::unordered_map<ax::Node*, std::unordered_map<std::string, ax::Value>> _baseValues;
    std::unordered_map<ax::Node*, std::unordered_map<std::string, ax::Object*>> _objects;
    
    int _autoPlaySequenceId;
    
    ax::WeakPtr<ax::Node> _rootNode;
    
    ax::Vec2 _rootContainerSize;
    
    CCBAnimationManagerDelegate *_delegate;
    CCBSequence *_runningSequence;
    
    ax::ValueVector _documentOutletNames;
    ax::Vector<ax::Node*> _documentOutletNodes;
    ax::ValueVector _documentCallbackNames;
    ax::Vector<ax::Node*> _documentCallbackNodes;
    ax::ValueVector _documentCallbackControlEvents;
    ax::ValueVector _keyframeCallbacks;
    ax::Map<std::string, ax::CallFunc*> _keyframeCallFuncs;
    
    std::string _documentControllerName;
    std::string _lastCompletedSequenceName;
    
    ax::SEL_CallFunc _animationCompleteCallbackFunc;
    ax::Object *_target;
    
};

class AX_EXT_API CCBSetSpriteFrame : public ax::ActionInstant
{
public:
    /** creates a Place action with a position */
    static CCBSetSpriteFrame* create(ax::SpriteFrame *pSpriteFrame);
    /**
     * @js NA
     * @lua NA
     */
    ~CCBSetSpriteFrame();
    
    bool initWithSpriteFrame(ax::SpriteFrame *pSpriteFrame);

    // Overrides
    virtual void update(float time) override;
	virtual CCBSetSpriteFrame* clone() const override;
	virtual CCBSetSpriteFrame* reverse() const override;

private:
    ax::SpriteFrame *_spriteFrame;
};


class AX_EXT_API CCBSoundEffect : public ax::ActionInstant
{
public:
    static CCBSoundEffect* actionWithSoundFile(const std::string &file, float pitch, float pan, float gain);
    /**
     * @js NA
     * @lua NA
     */
    ~CCBSoundEffect();
    bool initWithSoundFile(const std::string &file, float pitch, float pan, float gain);

    // Overrides
    virtual void update(float time) override;
	virtual CCBSoundEffect* clone() const override;
	virtual CCBSoundEffect* reverse() const override;

private:
    std::string _soundFile;
    float _pitch, _pan, _gain;
};


class AX_EXT_API CCBRotateTo : public ax::ActionInterval
{
public:
    static CCBRotateTo* create(float fDuration, float fAngle);
    bool initWithDuration(float fDuration, float fAngle);

    // Override
    virtual void update(float time) override;
	virtual CCBRotateTo* clone() const override;
	virtual CCBRotateTo* reverse() const override;
    virtual void startWithTarget(ax::Node *pNode) override;

private:
    float _startAngle;
    float _dstAngle;
    float _diffAngle;
};


class AX_EXT_API CCBRotateXTo: public ax::ActionInterval
{
public:
    static CCBRotateXTo* create(float fDuration, float fAngle);
    bool initWithDuration(float fDuration, float fAngle);

    // Overrides
    virtual void startWithTarget(ax::Node *pNode) override;
	virtual CCBRotateXTo* clone() const override;
	virtual CCBRotateXTo* reverse() const override;
    virtual void update(float time) override;

private:
    float _startAngle;
    float _dstAngle;
    float _diffAngle;
};


class AX_EXT_API CCBRotateYTo: public ax::ActionInterval
{
public:
    static CCBRotateYTo* create(float fDuration, float fAngle);
    bool initWithDuration(float fDuration, float fAngle);

    // Override
    virtual void startWithTarget(ax::Node *pNode) override;
	virtual CCBRotateYTo* clone() const override;
	virtual CCBRotateYTo* reverse() const override;
    virtual void update(float time) override;

private:
    float _startAngle;
    float _dstAngle;
    float _diffAngle;
};


class AX_EXT_API CCBEaseInstant : public ax::ActionEase
{
public:
    static CCBEaseInstant* create(ax::ActionInterval *pAction);

	virtual CCBEaseInstant* clone() const override;
	virtual CCBEaseInstant* reverse() const override;
    virtual void update(float dt) override;
};


}

#endif // __CCB_CCBANIMATION_MANAGER_H__
