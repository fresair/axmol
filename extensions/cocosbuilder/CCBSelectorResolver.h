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

#ifndef _CCB_CCBSELECTORRESOLVER_H_
#define _CCB_CCBSELECTORRESOLVER_H_

#include "cocosbuilder/CCBCompat.h"
#include "GUI/ControlExtension/Invocation.h"


namespace ax::ext {

#define CCB_SELECTORRESOLVER_CCMENUITEM_GLUE(TARGET, SELECTORNAME, METHOD) if(pTarget == TARGET && strcmp(pSelectorName, SELECTORNAME) == 0) { \
    return AX_MENU_SELECTOR(METHOD); \
}

#define CCB_SELECTORRESOLVER_CCCONTROL_GLUE(TARGET, SELECTORNAME, METHOD) if(pTarget == TARGET && strcmp(pSelectorName, SELECTORNAME) == 0) { \
    return cccontrol_selector(METHOD); \
}

#define CCB_SELECTORRESOLVER_CALLFUNC_GLUE(TARGET, SELECTORNAME, METHOD) if(pTarget == TARGET && strcmp(pSelectorName, SELECTORNAME) == 0) { \
    return AX_CALLFUNCN_SELECTOR(METHOD); \
}

class AX_EXT_API CCBSelectorResolver {
    public:
    /**
     * @js NA
     * @lua NA
     */
    virtual ~CCBSelectorResolver() {};
    virtual ax::SEL_MenuHandler onResolveCCBCCMenuItemSelector(ax::Object * pTarget, const char* pSelectorName) = 0;
    virtual ax::SEL_CallFuncN onResolveCCBCCCallFuncSelector(ax::Object * pTarget, const char* pSelectorName) { return NULL; };
    virtual ax::ext::Control::Handler onResolveCCBCCControlSelector(ax::Object * pTarget, const char* pSelectorName) = 0;
};


class AX_EXT_API CCBScriptOwnerProtocol {
public:
    /**
     * @js NA
     * @lua NA
     */
    virtual ~CCBScriptOwnerProtocol() {};
    virtual CCBSelectorResolver * createNew() = 0;
};

}

#endif
