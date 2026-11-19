/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
(function () {
    // The v3 native bootstrap defines this enum in its 3D compatibility script.
    // These values also match ax::LightType and the native getLightType result.
    if (cc.BaseLight && !cc.LightType)
        cc.LightType = {DIRECTIONAL: 0, POINT: 1, SPOT: 2, AMBIENT: 3};
})();
(function () {
    if (!cc.Camera || typeof cc.Camera.__allocate !== 'function') return;
    var Camera = cc.Camera, prototype = Camera.prototype;
    var perspective = prototype.configurePerspective;
    var orthographic = prototype.configureOrthographic;
    if (!perspective || !orthographic || !prototype.setProjectionMatrix) return;

    Camera.Mode = {DEFAULT: 0, PERSPECTIVE: 1, ORTHOGRAPHIC: 2};
    prototype.initPerspective = function (fov, aspect, nearPlane, farPlane) {
        return perspective.call(this, fov, aspect, nearPlane, farPlane);
    };
    prototype.initOrthographic = function (width, height, nearPlane, farPlane) {
        if (!orthographic.call(this, width, height, nearPlane, farPlane)) return false;
        // v3 uses createOrthographicOffCenter(0, width, 0, height, near, far).
        // Axmol's native orthographic projection is centered; retain its native
        // mode/planes and shift the actual projection to the old lower-left origin.
        var projection = this.getProjectionMatrix();
        projection[12] = -1;
        projection[13] = -1;
        this.setProjectionMatrix(projection);
        return true;
    };
    prototype.initDefault = function () {
        var director = cc.director, size = director.getWinSize();
        var projection = director.getProjection();
        if (projection === cc.Director.PROJECTION_2D) {
            if (!this.initOrthographic(size.width, size.height, -1024, 1024)) return false;
            this.setPosition3D({x: 0, y: 0, z: 0});
            this.setRotation3D({x: 0, y: 0, z: 0});
        } else if (projection === cc.Director.PROJECTION_3D) {
            var zeye = director.getZEye();
            if (!this.initPerspective(60, size.width / size.height, 10, zeye + size.height / 2)) return false;
            this.setPosition3D({x: size.width / 2, y: size.height / 2, z: zeye});
            this.lookAt({x: size.width / 2, y: size.height / 2, z: 0}, {x: 0, y: 1, z: 0});
        } else {
            // The v3 native initDefault leaves a custom projection unchanged.
            cc.log('unrecognized projection');
        }
        return true;
    };
    prototype.ctor = function (mode, first, second, third, fourth) {
        if (arguments.length === 0 || (arguments.length === 1 && mode === Camera.Mode.DEFAULT)) {
            if (!this.initDefault()) return false;
            this.setDepth(0);
            return true;
        }
        if (arguments.length === 5 && mode === Camera.Mode.PERSPECTIVE)
            return this.initPerspective(first, second, third, fourth);
        if (arguments.length === 5 && mode === Camera.Mode.ORTHOGRAPHIC)
            return this.initOrthographic(first, second, third, fourth);
        throw new TypeError('Camera requires DEFAULT or a projection mode and four projection parameters');
    };
    prototype._ctor = prototype.ctor;
    Object.defineProperty(prototype, '__legacyCtor', {value: true, configurable: true});
    Camera.create = function () {
        var camera = Camera.__allocate();
        if (camera.ctor.apply(camera, arguments) === false)
            throw new Error('Native Camera initialization failed');
        return camera;
    };
    Camera.createPerspective = function (fov, aspect, nearPlane, farPlane) {
        return Camera.create(Camera.Mode.PERSPECTIVE, fov, aspect, nearPlane, farPlane);
    };
    Camera.createOrthographic = function (width, height, nearPlane, farPlane) {
        return Camera.create(Camera.Mode.ORTHOGRAPHIC, width, height, nearPlane, farPlane);
    };
})();
(function () {
    // The v3 native binding published cube textures under jsb. This alias
    // preserves the actual generated Axmol class and its six-image factory.
    if (cc.TextureCube && !jsb.TextureCube) jsb.TextureCube=cc.TextureCube;
    if (!cc.MeshRenderer) return;
    var nativeCull=cc.MeshRenderer.prototype.setCullFace;
    if (nativeCull) cc.MeshRenderer.prototype.setCullFace=function (face) {
        var mode=face;
        if (face===0x0404) mode=cc.rhi.CullMode.FRONT;
        else if (face===0x0405) mode=cc.rhi.CullMode.BACK;
        else if (face===0) mode=cc.rhi.CullMode.NONE;
        else if (face===0x0408) throw Error('FRONT_AND_BACK culling is unavailable in the current RHI');
        return nativeCull.call(this,mode);
    };
})();
