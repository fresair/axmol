/*
 * Copyright (c) 2015-2016 Chukong Technologies Inc.
 * Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 * Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */


/* Original native async/path/loader helpers; see legacy-helpers-provenance.json. */
//+++++++++++++++++++++++++something about async begin+++++++++++++++++++++++++++++++
/**
 * Async Pool class, a helper of cc.async
 * @param {Object|Array} srcObj
 * @param {Number} limit the limit of parallel number
 * @param {function} iterator
 * @param {function} onEnd
 * @param {object} target
 * @constructor
 */
cc.AsyncPool = function(srcObj, limit, iterator, onEnd, target){
    var self = this;
    self._srcObj = srcObj;
    self._limit = limit;
    self._pool = [];
    self._iterator = iterator;
    self._iteratorTarget = target;
    self._onEnd = onEnd;
    self._onEndTarget = target;
    self._results = srcObj instanceof Array ? [] : {};
    self._isErr = false;

    cc.each(srcObj, function(value, index){
        self._pool.push({index : index, value : value});
    });

    self.size = self._pool.length;
    self.finishedSize = 0;
    self._workingSize = 0;

    self._limit = self._limit || self.size;

    self.onIterator = function(iterator, target){
        self._iterator = iterator;
        self._iteratorTarget = target;
    };

    self.onEnd = function(endCb, endCbTarget){
        self._onEnd = endCb;
        self._onEndTarget = endCbTarget;
    };

    self._handleItem = function(){
        var self = this;
        if(self._pool.length == 0)
            return;                                                         //return directly if the array's length = 0
        if(self._workingSize >= self._limit)
            return;                                                         //return directly if the working size great equal limit number
        var item = self._pool.shift();
        var value = item.value, index = item.index;
        self._workingSize++;
        self._iterator.call(self._iteratorTarget, value, index, function(err){
            if(self._isErr)
                return;

            self.finishedSize++;
            self._workingSize--;
            if(err) {
                self._isErr = true;
                if(self._onEnd)
                    self._onEnd.call(self._onEndTarget, err);
                return
            }

            var arr = Array.prototype.slice.call(arguments, 1);
            self._results[this.index] = arr[0];
            if (self.finishedSize === self.size) {
                if (self._onEnd)
                    self._onEnd.call(self._onEndTarget, null, self._results);
                return;
            }
            self._handleItem();
        }.bind(item), self);
    };

    self.flow = function(){
        var self = this;
        if(self._pool.length === 0) {
            if(self._onEnd)
                self._onEnd.call(self._onEndTarget, null, []);
            return;
        }
        for(var i = 0; i < self._limit; i++)
            self._handleItem();
    }
};

/**
 * @class
 */
cc.async = /** @lends cc.async# */{
    /**
     * Do tasks series.
     * @param {Array|Object} tasks
     * @param {function} [cb] callback
     * @param {Object} [target]
     * @return {cc.AsyncPool}
     */
    series : function(tasks, cb, target){
        var asyncPool = new cc.AsyncPool(tasks, 1, function(func, index, cb1){
            func.call(target, cb1);
        }, cb, target);
        asyncPool.flow();
        return asyncPool;
    },

    /**
     * Do tasks parallel.
     * @param {Array|Object} tasks
     * @param {function} cb callback
     * @param {Object} [target]
     * @return {cc.AsyncPool}
     */
    parallel : function(tasks, cb, target){
        var asyncPool = new cc.AsyncPool(tasks, 0, function(func, index, cb1){
            func.call(target, cb1);
        }, cb, target);
        asyncPool.flow();
        return asyncPool;
    },

    /**
     * Do tasks waterfall.
     * @param {Array|Object} tasks
     * @param {function} cb callback
     * @param {Object} [target]
     * @return {cc.AsyncPool}
     */
    waterfall : function(tasks, cb, target){
        var args = [];
        var lastResults = [null];//the array to store the last results
        var asyncPool = new cc.AsyncPool(tasks, 1,
            function (func, index, cb1) {
                args.push(function (err) {
                    args = Array.prototype.slice.call(arguments, 1);
                    if(tasks.length - 1 === index) lastResults = lastResults.concat(args);//while the last task
                    cb1.apply(null, arguments);
                });
                func.apply(target, args);
            }, function (err) {
                if (!cb)
                    return;
                if (err)
                    return cb.call(target, err);
                cb.apply(target, lastResults);
            });
        asyncPool.flow();
        return asyncPool;
    },

    /**
     * Do tasks by iterator.
     * @param {Array|Object} tasks
     * @param {function|Object} iterator
     * @param {function} [callback]
     * @param {Object} [target]
     * @return {cc.AsyncPool}
     */
    map : function(tasks, iterator, callback, target){
        var locIterator = iterator;
        if(typeof(iterator) === "object"){
            callback = iterator.cb;
            target = iterator.iteratorTarget;
            locIterator = iterator.iterator;
        }
        var asyncPool = new cc.AsyncPool(tasks, 0, locIterator, callback, target);
        asyncPool.flow();
        return asyncPool;
    },

    /**
     * Do tasks by iterator limit.
     * @param {Array|Object} tasks
     * @param {Number} limit
     * @param {function} iterator
     * @param {function} cb callback
     * @param {Object} [target]
     */
    mapLimit : function(tasks, limit, iterator, cb, target){
        var asyncPool = new cc.AsyncPool(tasks, limit, iterator, cb, target);
        asyncPool.flow();
        return asyncPool;
    }
};
//+++++++++++++++++++++++++something about async end+++++++++++++++++++++++++++++++++

//+++++++++++++++++++++++++something about path begin++++++++++++++++++++++++++++++++
/**
 * @class
 */
cc.path = /** @lends cc.path# */{
    normalizeRE: /[^\.\/]+\/\.\.\//,

    /**
     * Join strings to be a path.
     * @example
     cc.path.join("a", "b.png");//-->"a/b.png"
     cc.path.join("a", "b", "c.png");//-->"a/b/c.png"
     cc.path.join("a", "b");//-->"a/b"
     cc.path.join("a", "b", "/");//-->"a/b/"
     cc.path.join("a", "b/", "/");//-->"a/b/"
     * @returns {string}
     */
    join: function () {
        var l = arguments.length;
        var result = "";
        for (var i = 0; i < l; i++) {
            result = (result + (result === "" ? "" : "/") + arguments[i]).replace(/(\/|\\\\)$/, "");
        }
        return result;
    },

    /**
     * Get the ext name of a path.
     * @example
     cc.path.extname("a/b.png");//-->".png"
     cc.path.extname("a/b.png?a=1&b=2");//-->".png"
     cc.path.extname("a/b");//-->null
     cc.path.extname("a/b?a=1&b=2");//-->null
     * @param {string} pathStr
     * @returns {*}
     */
    extname: function (pathStr) {
        var temp = /(\.[^\.\/\?\\]*)(\?.*)?$/.exec(pathStr);
        return temp ? temp[1] : null;
    },

    /**
     * Get the main name of a file name
     * @param {string} fileName
     * @returns {string}
     */
    mainFileName: function(fileName){
        if(fileName){
           var idx = fileName.lastIndexOf(".");
            if(idx !== -1)
               return fileName.substring(0,idx);
        }
        return fileName;
    },

    /**
     * Get the file name of a file path.
     * @example
     cc.path.basename("a/b.png");//-->"b.png"
     cc.path.basename("a/b.png?a=1&b=2");//-->"b.png"
     cc.path.basename("a/b.png", ".png");//-->"b"
     cc.path.basename("a/b.png?a=1&b=2", ".png");//-->"b"
     cc.path.basename("a/b.png", ".txt");//-->"b.png"
     * @param {string} pathStr
     * @param {string} [extname]
     * @returns {*}
     */
    basename: function (pathStr, extname) {
        var index = pathStr.indexOf("?");
        if (index > 0) pathStr = pathStr.substring(0, index);
        var reg = /(\/|\\\\)([^(\/|\\\\)]+)$/g;
        var result = reg.exec(pathStr.replace(/(\/|\\\\)$/, ""));
        if (!result) return null;
        var baseName = result[2];
        if (extname && pathStr.substring(pathStr.length - extname.length).toLowerCase() === extname.toLowerCase())
            return baseName.substring(0, baseName.length - extname.length);
        return baseName;
    },

    /**
     * Get dirname of a file path.
     * @example
     * unix
     cc.path.driname("a/b/c.png");//-->"a/b"
     cc.path.driname("a/b/c.png?a=1&b=2");//-->"a/b"
     cc.path.dirname("a/b/");//-->"a/b"
     cc.path.dirname("c.png");//-->""
     * windows
     cc.path.driname("a\\b\\c.png");//-->"a\b"
     cc.path.driname("a\\b\\c.png?a=1&b=2");//-->"a\b"
     * @param {string} pathStr
     * @returns {*}
     */
    dirname: function (pathStr) {
        return pathStr.replace(/((.*)(\/|\\|\\\\))?(.*?\..*$)?/, '$2');
    },

    /**
     * Change extname of a file path.
     * @example
     cc.path.changeExtname("a/b.png", ".plist");//-->"a/b.plist"
     cc.path.changeExtname("a/b.png?a=1&b=2", ".plist");//-->"a/b.plist?a=1&b=2"
     * @param {string} pathStr
     * @param {string} [extname]
     * @returns {string}
     */
    changeExtname: function (pathStr, extname) {
        extname = extname || "";
        var index = pathStr.indexOf("?");
        var tempStr = "";
        if (index > 0) {
            tempStr = pathStr.substring(index);
            pathStr = pathStr.substring(0, index);
        }
        index = pathStr.lastIndexOf(".");
        if (index < 0) return pathStr + extname + tempStr;
        return pathStr.substring(0, index) + extname + tempStr;
    },
    /**
     * Change file name of a file path.
     * @example
     cc.path.changeBasename("a/b/c.plist", "b.plist");//-->"a/b/b.plist"
     cc.path.changeBasename("a/b/c.plist?a=1&b=2", "b.plist");//-->"a/b/b.plist?a=1&b=2"
     cc.path.changeBasename("a/b/c.plist", ".png");//-->"a/b/c.png"
     cc.path.changeBasename("a/b/c.plist", "b");//-->"a/b/b"
     cc.path.changeBasename("a/b/c.plist", "b", true);//-->"a/b/b.plist"
     * @param {String} pathStr
     * @param {String} basename
     * @param {Boolean} [isSameExt]
     * @returns {string}
     */
    changeBasename: function (pathStr, basename, isSameExt) {
        if (basename.indexOf(".") === 0) return this.changeExtname(pathStr, basename);
        var index = pathStr.indexOf("?");
        var tempStr = "";
        var ext = isSameExt ? this.extname(pathStr) : "";
        if (index > 0) {
            tempStr = pathStr.substring(index);
            pathStr = pathStr.substring(0, index);
        }
        index = pathStr.lastIndexOf("/");
        index = index <= 0 ? 0 : index + 1;
        return pathStr.substring(0, index) + basename + ext + tempStr;
    },
    //todo make public after verification
    _normalize: function(url){
        var oldUrl = url = String(url);

        //removing all ../
        do {
            oldUrl = url;
            url = url.replace(this.normalizeRE, "");
        } while(oldUrl.length !== url.length);
        return url;
    }
};
//+++++++++++++++++++++++++something about path end++++++++++++++++++++++++++++++++

//+++++++++++++++++++++++++something about loader start+++++++++++++++++++++++++++
cc.loader = {
    _resPath : "",
    _audioPath : "",
    _register : {},//register of loaders
    cache : {},//cache for data loaded
    _langPathCache : {},//cache for lang path
    
    /**
     * Get XMLHttpRequest.
     * @returns {XMLHttpRequest}
     */
    getXMLHttpRequest : function () {
        return new XMLHttpRequest();
    },
    
    
    //@MODE_BEGIN DEV
    
    _jsCache : {},//cache for js
    
    _getArgs4Js: function (args) {
        var a0 = args[0], a1 = args[1], a2 = args[2], results = ["", null, null];

        if (args.length === 1) {
            results[1] = a0 instanceof Array ? a0 : [a0];
        } else if (args.length === 2) {
            if (typeof a1 === "function") {
                results[1] = a0 instanceof Array ? a0 : [a0];
                results[2] = a1;
            } else {
                results[0] = a0 || "";
                results[1] = a1 instanceof Array ? a1 : [a1];
            }
        } else if (args.length === 3) {
            results[0] = a0 || "";
            results[1] = a1 instanceof Array ? a1 : [a1];
            results[2] = a2;
        } else throw new Error("arguments error to load js!");
        return results;
    },

    /**
     * Load js files.
     * @param {?string=} baseDir   The pre path for jsList.
     * @param {array.<string>} jsList    List of js path.
     * @param {function} cb        Callback function
     *
     *      If the arguments.length == 2, then the baseDir turns to be "".
     * @returns {*}
     */
    loadJs: function (baseDir, jsList, cb) {
        var self = this, localJsCache = self._jsCache,
            args = self._getArgs4Js(arguments);
        baseDir = args[0];
        jsList = args[1];
        cb = args[2];
        var ccPath = cc.path;
        for(var i = 0, li = jsList.length; i < li; ++i){
            require(ccPath.join(baseDir, jsList[i]));
        }
        if(cb) cb();
    },
    /**
     * Load js width loading image.
     *
     * @param {string} [baseDir]
     * @param {array} jsList
     * @param {function} [cb]
     */
    loadJsWithImg : function(baseDir, jsList, cb){
        this.loadJs.apply(this, arguments);
    },
    
    //@MODE_END DEV
    
    /**
     * Load a single resource as txt.
     * @param {!string} url
     * @param {function} cb arguments are : err, txt
     */
    loadTxt : function(url, cb){
        cb(null, jsb.fileUtils.getStringFromFile(url));
    },
    
    loadJson : function(url, cb){
        this.loadTxt(url, function(err, txt){
            try{
                err ? cb(err) : cb(null, JSON.parse(txt));
            }catch(e){
                throw e;
                cb("load json [" + url + "] failed : " + e);
            }
        });
    },
    
    /**
     * Load a single image.
     * @param {!string} url
     * @param {object} [option]
     * @param {function} cb
     * @returns {Image}
     */
    loadImg: function (url, option, cb){
        var l = arguments.length;
        if(l == 2) cb = option;

        var cachedTex = cc.textureCache.getTextureForKey(url);
        if (cachedTex) {
            cb && cb(null, cachedTex);
        }
        else if (url.match(jsb.urlRegExp)) {
            jsb.loadRemoteImg(url, function(succeed, tex) {
                if (succeed) {
                    cb && cb(null, tex);
                }
                else {
                    cb && cb("Load image failed");
                }
            });
        }
        else {
            cc.textureCache._addImageAsync(url, function (tex){
                if (tex instanceof cc.Texture2D)
                    cb && cb(null, tex);
                else cb && cb("Load image failed");
            });
        }
    },
    /**
     * Load binary data by url.
     * @param {String} url
     * @param {Function} cb
     */
    loadBinary : function(url, cb){
        cb(null, jsb.fileUtils.getDataFromFile(url));
    },
    loadBinarySync : function(url){
        return jsb.fileUtils.getDataFromFile(url);
    },
    
    /**
     * Iterator function to load res
     * @param {object} item
     * @param {number} index
     * @param {function} [cb]
     * @returns {*}
     * @private
     */
    _loadResIterator: function (item, index, cb) {
        var self = this, url = null;
        var type = item.type;
        if (type) {
            type = "." + type.toLowerCase();
            url = item.src ? item.src : item.name + type;
        } else {
            url = item;
            type = cc.path.extname(url);
        }

        var obj = self.cache[url];
        if (obj)
            return cb(null, obj);
        var loader = null;
        if (type) {
            loader = self._register[type.toLowerCase()];
        }
        if (!loader) {
            cc.error("loader for [" + type + "] not exists!");
            return cb();
        }
        var basePath = loader.getBasePath ? loader.getBasePath() : self.resPath;
        var realUrl = self.getUrl(basePath, url);

        loader.load(realUrl, url, item, function (err, data) {
            if (err) {
                cc.log(err);
                self.cache[url] = null;
                delete self.cache[url];
                cb();
            } else {
                self.cache[url] = data;
                cb(null, data);
            }
        });
    },
    
    /**
     * Get url with basePath.
     * @param {string} basePath
     * @param {string} [url]
     * @returns {*}
     */
    getUrl: function (basePath, url) {
        var self = this, langPathCache = self._langPathCache, path = cc.path;
        if (basePath !== undefined && url === undefined) {
            url = basePath;
            var type = path.extname(url);
            type = type ? type.toLowerCase() : "";
            var loader = self._register[type];
            if (!loader)
                basePath = self.resPath;
            else
                basePath = loader.getBasePath ? loader.getBasePath() : self.resPath;
        }
        url = cc.path.join(basePath || "", url);
        if (url.match(/[\/(\\\\)]lang[\/(\\\\)]/i)) {
            if (langPathCache[url])
                return langPathCache[url];
            var extname = path.extname(url) || "";
            url = langPathCache[url] = url.substring(0, url.length - extname.length) + "_" + cc.sys.language + extname;
        }
        return url;
    },

    /**
     * Load resources then call the callback.
     * @param {string} resources
     * @param {function} [option] callback or trigger
     * @param {function|Object} [loadCallback]
     * @return {cc.AsyncPool}
     */
    load : function(resources, option, loadCallback){
        var self = this;
        var len = arguments.length;
        if(len === 0)
            throw new Error("arguments error!");

        if(len === 3){
            if(typeof option === "function"){
                if(typeof loadCallback === "function")
                    option = {trigger : option, cb : loadCallback };
                else
                    option = { cb : option, cbTarget : loadCallback};
            }
        }else if(len === 2){
            if(typeof option === "function")
                option = {cb : option};
        }else if(len === 1){
            option = {};
        }

        if(!(resources instanceof Array))
            resources = [resources];
        var asyncPool = new cc.AsyncPool(
            resources, 0,
            function (value, index, AsyncPoolCallback, aPool) {
                self._loadResIterator(value, index, function (err) {
                    var arr = Array.prototype.slice.call(arguments, 1);
                    if (option.trigger)
                        option.trigger.call(option.triggerTarget, arr[0], aPool.size, aPool.finishedSize);   //call trigger
                    AsyncPoolCallback(err, arr[0]);
                });
            },
            option.cb, option.cbTarget);
        asyncPool.flow();
        return asyncPool;
    },

    /**
     * <p>
     *     Loads alias map from the contents of a filename.                                        <br/>
     *                                                                                                                 <br/>
     *     @note The plist file name should follow the format below:                                                   <br/>
     *     <?xml version="1.0" encoding="UTF-8"?>                                                                      <br/>
     *         <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">  <br/>
     *             <plist version="1.0">                                                                               <br/>
     *                 <dict>                                                                                          <br/>
     *                     <key>filenames</key>                                                                        <br/>
     *                     <dict>                                                                                      <br/>
     *                         <key>sounds/click.wav</key>                                                             <br/>
     *                         <string>sounds/click.caf</string>                                                       <br/>
     *                         <key>sounds/endgame.wav</key>                                                           <br/>
     *                         <string>sounds/endgame.caf</string>                                                     <br/>
     *                         <key>sounds/gem-0.wav</key>                                                             <br/>
     *                         <string>sounds/gem-0.caf</string>                                                       <br/>
     *                     </dict>                                                                                     <br/>
     *                     <key>metadata</key>                                                                         <br/>
     *                     <dict>                                                                                      <br/>
     *                         <key>version</key>                                                                      <br/>
     *                         <integer>1</integer>                                                                    <br/>
     *                     </dict>                                                                                     <br/>
     *                 </dict>                                                                                         <br/>
     *              </plist>                                                                                           <br/>
     * </p>
     * @param {String} filename  The plist file name.
     * @param {Function} cb     callback
     */
    loadAliases : function(url, cb){
        jsb.fileUtils.loadFilenameLookup(url);
        if(cb) cb();
    },

    /**
     * Register a resource loader into loader.
     * @param {string} extNames
     * @param {function} loader
     */
    register: function (extNames, loader) {
        if (!extNames || !loader) return;
        var self = this;
        if (typeof extNames === "string")
            return this._register[extNames.trim().toLowerCase()] = loader;
        for (var i = 0, li = extNames.length; i < li; i++) {
            self._register["." + extNames[i].trim().toLowerCase()] = loader;
        }
    },

    /**
     * Get resource data by url.
     * @param url
     * @returns {*}
     */
    getRes : function(url){
        var cached = this.cache[url];
        if (cached)
            return cached;
        var type = cc.path.extname(url);
        if (!type) return cc.log("cc.loader.getRes: Invalid url");
        var loader = this._register[type.toLowerCase()];
        if (!loader) return cc.log("cc.loader.getRes: loader for [" + type + "] not exists!");
        var basePath = loader.getBasePath ? loader.getBasePath() : this.resPath;
        var realUrl = this.getUrl(basePath, url);
        return loader.load(realUrl, url);
    },

    /**
     * Release the cache of resource by url.
     * @param url
     */
    release: function (url) {
        var cache = this.cache;
        delete cache[url];
    },

    /**
     * Resource cache of all resources.
     */
    releaseAll: function () {
        var locCache = this.cache;
        for (var key in locCache)
            delete locCache[key];
    }
};
cc.defineGetterSetter(cc.loader, "resPath", function(){
    return this._resPath;
}, function(resPath){
    this._resPath = resPath || "";
    jsb.fileUtils.addSearchPath(this._resPath);
});
cc.defineGetterSetter(cc.loader, "audioPath", function(){
    return this._audioPath;
}, function(audioPath){
    this._audioPath = audioPath || "";
    jsb.fileUtils.addSearchPath(this._audioPath);
});

//+++++++++++++++++++++++++something about loader end+++++++++++++++++++++++++++++

/**
 * A string tool to construct a string with format string.
 * for example:
 *      cc.formatStr("a: %d, b: %b", a, b);
 *      cc.formatStr(a, b, c);
 * @returns {String}
 */
cc.formatStr = function(){
    var args = arguments;
    var l = args.length;
    if (l < 1) return '';
    var REGEXP_NUM_OR_STR = /(%d)|(%s)/;

    var i = 1;
    var str = args[0];
    var hasSubstitution = typeof str === 'string' && REGEXP_NUM_OR_STR.test(str);
    if (hasSubstitution) {
        var REGEXP_STR = /%s/;
        for (; i < l; ++i) {
            var arg = args[i];
            var regExpToTest = typeof arg === 'number' ? REGEXP_NUM_OR_STR : REGEXP_STR;
            if (regExpToTest.test(str))
                str = str.replace(regExpToTest, arg);
            else
                str += ' ' + arg;
        }
    }
    else {
        if (l > 1) {
            for (; i < l; ++i) {
                str += ' ' + args[i];
            }
        }
        else {
            str = '' + str;
        }
    }
    return str;
};

