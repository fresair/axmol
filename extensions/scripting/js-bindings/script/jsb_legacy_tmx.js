/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
/* Legacy contracts: cocos2d-x/v3 jsb_create_apis.js, jsb_property_apis.js,
 * jsb_property_impls.js and jsb_cocos2d.js at e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f. */
(function (global) {
    var cc=global.cc;
    var Layer=cc.FastTMXLayer,Map=cc.FastTMXTiledMap;
    // Scripts can be refreshed independently of an installed SDK. Install
    // these adapters only after the native manual module is registered.
    if(!Layer || !Map || typeof Layer.prototype.getTileFlagsAt!=='function' ||
        typeof Layer.prototype.releaseMap!=='function')return;
    cc.TMXLayer=Layer;
    cc.TMXTiledMap=Map;
    if(cc.TMXLayer.prototype.__legacyTMX)return;
    Object.defineProperty(cc.TMXLayer.prototype,'__legacyTMX',{value:true,configurable:true});

    cc.TMX_ORIENTATION_ORTHO=0;cc.TMX_ORIENTATION_HEX=1;cc.TMX_ORIENTATION_ISO=2;
    cc.TMX_TILE_HORIZONTAL_FLAG=0x80000000;cc.TMX_TILE_VERTICAL_FLAG=0x40000000;
    cc.TMX_TILE_DIAGONAL_FLAG=0x20000000;
    cc.TMX_TILE_FLIPPED_ALL=0xe0000000;cc.TMX_TILE_FLIPPED_MASK=0x1fffffff;
    cc.TMXLayerInfo.ATTRIB_NONE=1;cc.TMXLayerInfo.ATTRIB_BASE64=2;
    cc.TMXLayerInfo.ATTRIB_GZIP=4;cc.TMXLayerInfo.ATTRIB_ZLIB=8;
    cc.TMXLayer.TMX_ORIENTATION_ORTHO=0;cc.TMXLayer.TMX_ORIENTATION_HEX=1;cc.TMXLayer.TMX_ORIENTATION_ISO=2;

    function ctor(Type,fn) {
        Type.prototype.ctor=fn;
        Object.defineProperty(Type.prototype,'__legacyCtor',{value:true,configurable:true});
        Type.create=Type;
    }
    function mapCtor(source,resourcePath) {
        if(resourcePath!==undefined)return this.initWithXML(source,resourcePath);
        if(source!==undefined)return this.initWithTMXFile(source);
        return true;
    }
    ctor(cc.TMXTiledMap,mapCtor);
    ctor(cc.TMXMapInfo,mapCtor);
    ctor(cc.TMXLayer,function (tileset,layer,map) {
        return map===undefined ? true : this.initWithTilesetInfo(tileset,layer,map);
    });
    cc.tmxTiledMap=cc.TMXTiledMap;
    cc.tmxLayer=cc.TMXLayer;

    function positionMethod(name) {
        var native=cc.TMXLayer.prototype[name];
        cc.TMXLayer.prototype['_'+name]=native;
        cc.TMXLayer.prototype[name]=function (x,y) {
            return native.call(this,y===undefined ? x : cc.p(x,y));
        };
    }
    ['getTileAt','getTileFlagsAt','removeTileAt','getPositionAt'].forEach(positionMethod);
    var nativeGID=cc.TMXLayer.prototype.getTileGIDAt;
    cc.TMXLayer.prototype._getTileGIDAt=nativeGID;
    cc.TMXLayer.prototype.getTileGIDAt=function (x,y,flagsOutput) {
        if(typeof x==='object')
            return y===undefined ? nativeGID.call(this,x) : nativeGID.call(this,x,y);
        var point=cc.p(x,y);
        return flagsOutput===undefined ? nativeGID.call(this,point) : nativeGID.call(this,point,flagsOutput);
    };
    var nativeSet=cc.TMXLayer.prototype.setTileGID;
    cc.TMXLayer.prototype._setTileGID=nativeSet;
    cc.TMXLayer.prototype.setTileGID=function (gid,pointOrX,flagsOrY,flags) {
        if(typeof pointOrX==='object')
            return flagsOrY===undefined ? nativeSet.call(this,gid,pointOrX) : nativeSet.call(this,gid,pointOrX,flagsOrY);
        var point=cc.p(pointOrX,flagsOrY);
        return flags===undefined ? nativeSet.call(this,gid,point) : nativeSet.call(this,gid,point,flags);
    };
    cc.TMXLayer.prototype.getPositonAt=cc.TMXLayer.prototype.getPositionAt;
    cc.TMXLayer.prototype.tileFlagsAt=cc.TMXLayer.prototype.getTileFlagsAt;
    cc.TMXLayer.prototype.getTileset=cc.TMXLayer.prototype.getTileSet;
    cc.TMXLayer.prototype.setTileset=cc.TMXLayer.prototype.setTileSet;
    cc.TMXTiledMap.prototype.allLayers=function () { return this.getLayers(); };

    function property(Type,name,getter,setter) {
        var proto=Type.prototype;
        cc.defineGetterSetter(proto,name,proto[getter],setter ? proto[setter] : undefined);
    }
    function dimension(Type,name,getter,setter,component) {
        var suffix=name.charAt(0).toUpperCase()+name.slice(1),proto=Type.prototype;
        proto['_get'+suffix]=function () { return this[getter]()[component]; };
        proto['_set'+suffix]=function (value) {
            var size=this[getter]();size[component]=value;
            this[setter](cc.size(size.width,size.height));
        };
        property(Type,name,'_get'+suffix,'_set'+suffix);
    }
    if(typeof cc.TMXLayer.prototype.getTexture==='function')
        property(cc.TMXLayer,'texture','getTexture');
    property(cc.TMXLayer,'tiles','getTiles');
    property(cc.TMXLayer,'tileset','getTileset','setTileset');
    property(cc.TMXLayer,'layerName','getLayerName','setLayerName');
    property(cc.TMXLayer,'layerOrientation','getLayerOrientation','setLayerOrientation');
    property(cc.TMXLayer,'properties','getProperties','setProperties');
    dimension(cc.TMXLayer,'layerWidth','getLayerSize','setLayerSize','width');
    dimension(cc.TMXLayer,'layerHeight','getLayerSize','setLayerSize','height');
    dimension(cc.TMXLayer,'tileWidth','getMapTileSize','setMapTileSize','width');
    dimension(cc.TMXLayer,'tileHeight','getMapTileSize','setMapTileSize','height');
    property(cc.TMXTiledMap,'properties','getProperties','setProperties');
    property(cc.TMXTiledMap,'mapOrientation','getMapOrientation','setMapOrientation');
    property(cc.TMXTiledMap,'objectGroups','getObjectGroups','setObjectGroups');
    dimension(cc.TMXTiledMap,'mapWidth','getMapSize','setMapSize','width');
    dimension(cc.TMXTiledMap,'mapHeight','getMapSize','setMapSize','height');
    dimension(cc.TMXTiledMap,'tileWidth','getTileSize','setTileSize','width');
    dimension(cc.TMXTiledMap,'tileHeight','getTileSize','setTileSize','height');
})(this);
