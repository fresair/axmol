/****************************************************************************
 Copyright (c) 2013-2016 Chukong Technologies Inc.
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

(function(load, baseParser){

    var loadedPlist = {};

    var Parser = baseParser.extend({

        getNodeJson: function(json){
            return json["nodeTree"];
        },

        addSpriteFrame: function(plists, pngs, resourcePath){
            if(!plists || !pngs || plists.length !== pngs.length)
                return;
            for (var i = 0; i < plists.length; i++) {
                var plist = resourcePath + plists[i];
                if(!cc.loader.getRes(plist) && !loadedPlist[plist])
                    cc.log("%s need to be preloaded", plist);
                else
                    loadedPlist[plist] = true;
                cc.spriteFrameCache.addSpriteFrames(
                    plist,
                    resourcePath + pngs[i]
                );
            }
        },

        pretreatment: function(json, resourcePath, file){
            this.addSpriteFrame(json["textures"], json["texturesPng"], resourcePath);
        }

    });
    var parser = new Parser();

    parser.generalAttributes = function(node, options){
        var width         = options["width"]        !=null ? options["width"] : 0;
        var height        = options["height"]       !=null ? options["height"] : 0;
        var x             = options["x"]            !=null ? options["x"] : 0;
        var y             = options["y"]            !=null ? options["y"] : 0;
        var scalex        = options["scaleX"]       !=null ? options["scaleX"] : 1;
        var scaley        = options["scaleY"]       !=null ? options["scaleY"] : 1;
        var rotation      = options["rotation"]     !=null ? options["rotation"] : 0;
        var rotationSkewX = options["rotationSkewX"]!=null ? options["rotationSkewX"] : 0;
        var rotationSkewY = options["rotationSkewY"]!=null ? options["rotationSkewY"] : 0;
        var skewx         = options["skewX"]        !=null ? options["skewX"] : 0;
        var skewy         = options["skewY"]        !=null ? options["skewY"] : 0;
        var anchorx       = options["anchorPointX"] !=null ? options["anchorPointX"] : 0.5;
        var anchory       = options["anchorPointY"] !=null ? options["anchorPointY"] : 0.5;
        var alpha         = options["opacity"]      !=null ? options["opacity"] : 255;
        var red           = options["colorR"]       !=null ? options["colorR"] : 255;
        var green         = options["colorG"]       !=null ? options["colorG"] : 255;
        var blue          = options["colorB"]       !=null ? options["colorB"] : 255;
        var zorder        = options["ZOrder"] != null ? options["ZOrder"] : (options["zorder"] || 0);
        var tag           = options["tag"]          !=null ? options["tag"] : 0;
        var actionTag     = options["actionTag"]    !=null ? options["actionTag"] : 0;
        var visible       = options["visible"]      !=null ? options["visible"] : true;

        if(x != 0 || y != 0)
            node.setPosition(cc.p(x, y));
        if(scalex != 1)
            node.setScaleX(scalex);
        if(scaley != 1)
            node.setScaleY(scaley);
        if (rotation != 0)
            node.setRotation(rotation);
        if(rotationSkewX != 0)
            node.setRotationX(rotationSkewX);
        if(rotationSkewY != 0)
            node.setRotationY(rotationSkewY);
        if(skewx != 0)
            node.setSkewX(skewx);
        if(skewy != 0)
            node.setSkewY(skewy);
        if(anchorx != 0.5 || anchory != 0.5)
            node.setAnchorPoint(cc.p(anchorx, anchory));
        if(width != 0 || height != 0)
            node.setContentSize(cc.size(width, height));
        if(zorder != 0)
            node.setLocalZOrder(zorder);
        if(visible != true)
            node.setVisible(visible);

        if(alpha != 255)
        {
            node.setOpacity(alpha);
        }
        if(red != 255 || green != 255 || blue != 255)
        {
            node.setColor(cc.color(red, green, blue));
        }


        node.setTag(tag);
        node.setName(options["name"] || "");
        node.setUserObject(ccs.ActionTimelineData.create(actionTag));
        // Axmol resolves animation tags through ComExtensionData.
        var extensionData = new ccs.ComExtensionData();
        extensionData.setActionTag(actionTag);
        if (node.getComponent("ComExtensionData")) node.removeComponent("ComExtensionData");
        node.addComponent(extensionData);
    };

    parser.parseComponent = function(node, options){
        if(!options) return;
        for (var i = 0; i < options.length; ++i){
            var dic = options[i];
            var component = this.loadComponent(dic);
            if (component){
                node.addComponent(component);
            }
        }
    };

    parser.parseChild = function(widget, options, resourcePath){
        var children = options["children"] || [];
        for (var i = 0; i < children.length; i++) {
            var child = this.parseNode(children[i], resourcePath);
            if(child){
                if(widget instanceof ccui.PageView){
                    if(child instanceof ccui.Layout)
                        widget.addPage(child);
                } else {
                    if(widget instanceof ccui.ListView){
                        if(child instanceof ccui.Widget)
                            widget.pushBackCustomItem(child);
                    } else {
                        if(!(widget instanceof ccui.Layout) && child instanceof ccui.Widget) {
                            if(child.getPositionType() === ccui.Widget.POSITION_PERCENT) {
                                var position = child.getPositionPercent();
                                var anchor = widget.getAnchorPoint();
                                child.setPositionPercent(cc.p(position.x + anchor.x, position.y + anchor.y));
                            }
                            //To make up for the studio positioning error problem
                            var AnchorPointIn = widget.getAnchorPointInPoints();
                            child.setPosition(cc.p(child.getPositionX() + AnchorPointIn.x, child.getPositionY() + AnchorPointIn.y));
                        }
                        widget.addChild(child);
                    }
                }
            }
        }
    };

    parser.initNode = function(options){
        var node = new cc.Node();
        this.generalAttributes(node, options);
        return node;
    };
    parser.initSubGraph = function(options, resourcePath){
        var filePath = options["fileName"];

        var node;
        if (filePath && "" !== filePath){
            node = ccs.load((resourcePath || "") + filePath).node;
        }else{
            node = new ccs.Node();
        }
        this.generalAttributes(node, options);
        return node;
    };
    parser.initSprite = function(options, resourcePath){
        var path = options["fileName"];
        var sprite;
        if(path != null){
            var spriteFrame = cc.spriteFrameCache.getSpriteFrame(path);
            if(!spriteFrame){
                path = resourcePath + path;
                sprite = new ccs.Sprite(path);
            }else{
                sprite = ccs.Sprite.createWithSpriteFrame(spriteFrame);
            }

            if(!sprite){
                sprite = new cc.Sprite();
                cc.log("filePath is empty. Create a sprite with no texture");
            }
        }else{
            sprite = new ccs.Sprite();
        }
        this.generalAttributes(sprite, options);
        var flipX = options["flipX"];
        var flipY = options["flipY"];

        sprite.setFlippedX(!!flipX);
        sprite.setFlippedY(!!flipY);
        return sprite;
    };
    parser.initParticle = function(options, resourcePath){
        var filePath = options["plistFile"];
        var num = options["tmxFile"];
        var particle = new cc.ParticleSystemQuad(filePath);
        particle.setTotalParticles(num);
        this.generalAttributes(particle, options);
        return particle;
    };
    parser.initTMXTiledMap = function(options, resourcePath){
        var tmxFile = options["tmxFile"];
        var tmxString = options["tmxString"];
        //todo check path and resourcePath
        var path = options["resourcePath"];

        var tmx = null;
        if (tmxFile && "" !== tmxFile){
            tmx = new cc.TMXTiledMap(tmxFile);
        }else if (tmxString && "" !== tmxString && path && "" !== path){
            tmx = new cc.TMXTiledMap(tmxString, path);
        }
        return tmx;
    };
    var uiParser = load.getParser("ccui")["*"];
    parser.initWidget = function(options, resourcePath){
        var type = options["classname"];
        var widgetParser = uiParser.parsers[type];
        if(!widgetParser)
            throw new Error("Unsupported CocoStudio widget type: " + type);

        // Let the timeline parser attach children and animation tags once.
        var node = widgetParser.call(uiParser, {classname: type, options: options["options"], children: []}, resourcePath);
        options = options["options"] || {};
        if(node){
            var rotationSkewX = options["rotationSkewX"] || 0;
            var rotationSkewY = options["rotationSkewY"] || 0;
            var skewx         = options["skewX"] || 0;
            var skewy         = options["skewY"] || 0;
            if(rotationSkewX != 0)
                node.setRotationX(rotationSkewX);
            if(rotationSkewY != 0)
                node.setRotationY(rotationSkewY);
            if(skewx != 0)
                node.setSkewX(skewx);
            if(skewy != 0)
                node.setSkewY(skewy);

            var actionTag = options["actionTag"] || 0;
            node.setUserObject(ccs.ActionTimelineData.create(actionTag));
            var extensionData = new ccs.ComExtensionData();
            extensionData.setActionTag(actionTag);
            if (node.getComponent("ComExtensionData")) node.removeComponent("ComExtensionData");
            node.addComponent(extensionData);
        }
        return node;
    };

    var register = [
        {name: "Node", handle: parser.initNode},
        {name: "SubGraph", handle: parser.initSubGraph},
        {name: "Sprite", handle: parser.initSprite},
        {name: "Particle", handle: parser.initParticle},
        {name: "TMXTiledMap", handle: parser.initTMXTiledMap},

        {name: "Widget", handle: parser.initWidget},
        {name: "Panel", handle: parser.initWidget},
        {name: "Button", handle: parser.initWidget},
        {name: "CheckBox", handle: parser.initWidget},
        {name: "ImageView", handle: parser.initWidget},
        {name: "LabelAtlas", handle: parser.initWidget},
        {name: "LabelBMFont", handle: parser.initWidget},
        {name: "Label", handle: parser.initWidget},
        {name: "ListView", handle: parser.initWidget},
        {name: "LoadingBar", handle: parser.initWidget},
        {name: "PageView", handle: parser.initWidget},
        {name: "ScrollView", handle: parser.initWidget},
        {name: "Slider", handle: parser.initWidget},
        {name: "TextField", handle: parser.initWidget}
    ];

    register.forEach(function(item){
        parser.registerParser(item.name, function(options, resourcePath){
            var node = item.handle.call(this, item.handle === parser.initWidget ? options : options["options"], resourcePath);
            this.parseComponent(node, options["components"]);
            this.parseChild(node, options, resourcePath);
            return node;
        });
    });

    load.registerParser("timeline", "0.*", parser);
    load.registerParser("timeline", "1.*", parser);

})(ccs._load, ccs._parser);
