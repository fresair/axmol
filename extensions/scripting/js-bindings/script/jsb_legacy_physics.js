/*
 * Copyright (c) 2013-2016 Chukong Technologies Inc.
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

//
// Chipmunk defines
//

var cp = cp || {};

cp.Vect = function(x, y){
    this.x = x;
    this.y = y;
}
cp.Vect.prototype.add = function(v){
    this.x += v.x;
    this.y += v.y;
    return this;
}
cp.Vect.prototype.sub = function(v){
    this.x -= v.x;
    this.y -= v.y;
    return this;
}
cp.Vect.prototype.neg = function(){
    this.x = -this.x;
    this.y = -this.y;
    return this;
}
cp.Vect.prototype.mult = function(s){
    this.x *= s;
    this.y *= s;
    return this;
}
cp.Vect.prototype.rotate = function(v){
    this.x = this.x * v.x - this.y * v.y;
    this.y = this.x * v.y + this.y * v.x;
    return this;
}
cp.Vect.prototype.project = function(v){
    this.mult(cp.vdot(this, v) / cp.vlengthsq(v));
    return this;
}

cp.v = function(x, y){
    return new cp.Vect(x, y);
}
cp.vzero  = cp.v(0,0);

// Vector: Compatibility with Chipmunk-JS
cp.v.add = cp.vadd;
cp.v.clamp = cp.vclamp;
cp.v.cross = cp.vcross;
cp.v.dist = cp.vdist;
cp.v.distsq = cp.vdistsq;
cp.v.dot = cp.vdot;
cp.v.eql = cp.veql;
cp.v.forangle = cp.vforangle;
cp.v.len = cp.vlength;
cp.v.lengthsq = cp.vlengthsq;
cp.v.lerp = cp.vlerp;
cp.v.lerpconst = cp.vlerpconst;
cp.v.mult = cp.vmult;
cp.v.near = cp.vnear;
cp.v.neg = cp.vneg;
cp.v.normalize = cp.vnormalize;
cp.v.normalize_safe = cp.vnormalize_safe;
cp.v.perp = cp.vperp;
cp.v.project = cp.vproject;
cp.v.rotate = cp.vrotate;
cp.v.pvrperp = cp.vrperp;
cp.v.slerp = cp.vslerp;
cp.v.slerpconst = cp.vslerpconst;
cp.v.sub = cp.vsub;
cp.v.toangle = cp.vtoangle;
cp.v.unrotate = cp.vunrotate;
cp.v.str = function(v){
    return "(" + v.x.toFixed(3) + ", " + v.y.toFixed(3) + ")";
}

// XXX: renaming functions should be supported in JSB
cp.clamp01 = cp.fclamp01;


/// Initialize an offset box shaped polygon shape.
cp.BoxShape2 = function(body, box)
{
    var verts = [
        box.l, box.b,
        box.l, box.t,
        box.r, box.t,
        box.r, box.b
    ];

    return new cp.PolyShape(body, verts, cp.vzero);
};

/// Initialize a box shaped polygon shape.
cp.BoxShape = function(body, width, height)
{
    var hw = width/2;
    var hh = height/2;

    return cp.BoxShape2(body, new cp.BB(-hw, -hh, hw, hh));
};


/// Initialize an static body
cp.StaticBody = function()
{
    return new cp.Body(Infinity, Infinity);
};


// "Bounding Box" compatibility with Chipmunk-JS
cp.BB = function(l, b, r, t)
{
    this.l = l;
    this.b = b;
    this.r = r;
    this.t = t;
};

// helper function to create a BB
cp.bb = function(l, b, r, t) {
    return new cp.BB(l, b, r, t);
};

//
// BEGIN OF CHIPMUNK V6.2 COMPATIBILIY
// Define them before the properties
//

// Body
cp.Body.prototype.getVelAtWorldPoint = function(point) {
        return this.getVelocityAtWorldPoint(point);
}
cp.Body.prototype.getVelAtLocalPoint = function(point) {
        return this.getVelocityAtLocalPoint(point);
}
cp.Body.prototype.getVel = function() {
        return this.getVelocity();
}
cp.Body.prototype.setVel = function(velocity) {
        this.setVelocity(velocity);
}
cp.Body.prototype.getPos = function() {
        return this.getPosition();
}
cp.Body.prototype.setPos = function(pos) {
        this.setPosition(pos);
}
cp.Body.prototype.getRot = function() {
        return this.getRotation();
}
cp.Body.prototype.getAngVel = function() {
        return this.getAngularVelocity();
}
cp.Body.prototype.setAngVel = function(angularVelocity) {
        this.setAngularVelocity(angularVelocity);
}
cp.Body.prototype.local2World = function(point) {
        return this.localToWorld(point);
}
cp.Body.prototype.world2Local = function(point) {
        return this.worldToLocal(point);
}
cp.Body.prototype.applyImpulse = function(j,r) {
        this.applyImpulseAtWorldPoint(j,r);
}
cp.Body.prototype.applyForce = function(force, point) {
        this.applyForceAtWorldPoint(force, point);
}


// Shapes
cp.Shape.prototype.setLayers = function(layer) {
        var filter = this.getFilter();
        filter.mask = layer;
        this.setFilter(filter);
}
cp.Shape.prototype.getLayers = function() {
        var filter = this.getFilter();
        return filter.mask;
}
cp.Shape.prototype.setGroup = function(group) {
        var filter = this.getFilter();
        filter.group = group;
        this.setFilter(filter);
}

cp.Shape.prototype.getGroup = function() {
        var filter = this.getFilter();
        return filter.group;
}

cp.Shape.prototype.nearestPointQuery = function(p) {
        return this.pointQuery(p);
}
cp.PolyShape.prototype.getNumVerts = function() {
        return this.getCount();
}

// Space
cp.Space.prototype.addStaticShape = function(shape) {
        this.addShape(shape);
}

// Joints
cp.PinJoint.prototype.getAnchr1 = function() {
    return this.getAnchorA();
};

cp.PinJoint.prototype.getAnchr2 = function() {
    return this.getAnchorB();
};

cp.SlideJoint.prototype.getAnchr1 = function() {
    return this.getAnchorA();
};

cp.SlideJoint.prototype.getAnchr2 = function() {
    return this.getAnchorB();
};

cp.PivotJoint.prototype.getAnchr1 = function() {
    return this.getAnchorA();
};

cp.PivotJoint.prototype.getAnchr2 = function() {
    return this.getAnchorB();
};

cp.GrooveJoint.prototype.getAnchr2 = function() {
    return this.getAnchorB();
};

cp.DampedSpring.prototype.getAnchr1 = function() {
    return this.getAnchorA();
};

cp.DampedSpring.prototype.getAnchr2 = function() {
    return this.getAnchorB();
};

cp.Space.prototype.addStaticShape = function(shape) {
    this.addShape(shape);
};


// END OF CHIPMUNK V6.2 COMPATIBILIY


// Chipmunk v7.0 constants
cp.SHAPE_FILTER_ALL = {
        group:cp.NO_GROUP,
        categories:cp.ALL_CATEGORIES,
        mask:cp.ALL_CATEGORIES
};

cp.privateAPI = function() {
        cc.log("Private API not exposed in Chipmunk v7.0");
}

// helpers for the properties. see below, in the properties section
cp.Arbiter.prototype.getBodyA = function() {
        return this.getBodies()[0];
}
cp.Arbiter.prototype.getBodyB = function() {
        return this.getBodies()[1];
}
cp.Body.prototype.getVelocityX = function() {
        return this.getVelocity().x;
}
cp.Body.prototype.getVelocityY = function() {
        return this.getVelocity().y;
}
cp.Shape.prototype.getBBL = function() {
        return this.getBB().l;
}
cp.Shape.prototype.getBBB = function() {
        return this.getBB().b;
}
cp.Shape.prototype.getBBR = function() {
        return this.getBB().r;
}
cp.Shape.prototype.getBBT = function() {
        return this.getBB().t;
}


//
// Some properties
//
var _proto = cp.Base.prototype;
// "handle" needed in some cases
cc.defineGetterSetter(_proto, "handle", _proto.getHandle);

// Properties, for Chipmunk-JS compatibility
// Space properties
Object.defineProperties(cp.Space.prototype,
                {
                    "gravity" : {
                        get : function(){
                            return this.getGravity();
                        },
                        set : function(newValue){
                            this.setGravity(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "iterations" : {
                        get : function(){
                            return this.getIterations();
                        },
                        set : function(newValue){
                            this.setIterations(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "damping" : {
                        get : function(){
                            return this.getDamping();
                        },
                        set : function(newValue){
                            this.setDamping(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "staticBody" : {
                        get : function(){
                            return this.getStaticBody();
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "idleSpeedThreshold" : {
                        get : function(){
                            return this.getIdleSpeedThreshold();
                        },
                        set : function(newValue){
                            this.setIdleSpeedThreshold(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "sleepTimeThreshold": {
                        get : function(){
                            return this.getSleepTimeThreshold();
                        },
                        set : function(newValue){
                            this.setSleepTimeThreshold(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "collisionSlop": {
                        get : function(){
                            return this.getCollisionSlop();
                        },
                        set : function(newValue){
                            this.setCollisionSlop(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "collisionBias": {
                        get : function(){
                            return this.getCollisionBias();
                        },
                        set : function(newValue){
                            this.setCollisionBias(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "collisionPersistence": {
                        get : function(){
                            return this.getCollisionPersistence();
                        },
                        set : function(newValue){
                            this.setCollisionPersistence(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    },
                    "enableContactGraph": {
                        get : function(){
                            return this.getEnableContactGraph();
                        },
                        set : function(newValue){
                            this.setEnableContactGraph(newValue);
                        },
                        enumerable : true,
                        configurable : true
                    }
                });

// Body properties
_proto = cp.Body.prototype;
cc.defineGetterSetter(_proto, "a", _proto.getAngle, _proto.setAngle);
cc.defineGetterSetter(_proto, "w", _proto.getAngVel, _proto.setAngVel);
cc.defineGetterSetter(_proto, "p", _proto.getPos, _proto.setPos);
cc.defineGetterSetter(_proto, "v", _proto.getVel, _proto.setVel);
cc.defineGetterSetter(_proto, "f", _proto.getForce, _proto.setForce);
cc.defineGetterSetter(_proto, "t", _proto.getTorque, _proto.setTorque);
cc.defineGetterSetter(_proto, "v_limit", _proto.getVelLimit, _proto.setVelLimit);
cc.defineGetterSetter(_proto, "w_limit", _proto.getAngVelLimit, _proto.setAngVelLimit);
cc.defineGetterSetter(_proto, "space", _proto.getSpace);
cc.defineGetterSetter(_proto, "rot", _proto.getRot);
cc.defineGetterSetter(_proto, "m", _proto.getMass, _proto.setMass);
cc.defineGetterSetter(_proto, "i", _proto.getMoment, _proto.setMoment);
cc.defineGetterSetter(_proto, "vx", _proto.getVelocityX);
cc.defineGetterSetter(_proto, "vy", _proto.getVelocityY);
cc.defineGetterSetter(_proto, "m_inv", cp.privateAPI);
cc.defineGetterSetter(_proto, "i_inv", cp.privateAPI);


// Shape properties
_proto = cp.Shape.prototype;
cc.defineGetterSetter(_proto, "body", _proto.getBody, _proto.setBody);
cc.defineGetterSetter(_proto, "group", _proto.getGroup, _proto.setGroup);
cc.defineGetterSetter(_proto, "collision_type", _proto.getCollisionType, _proto.setCollisionType);
cc.defineGetterSetter(_proto, "layers", _proto.getLayers, _proto.setLayers);
cc.defineGetterSetter(_proto, "sensor", _proto.getSensor, _proto.setSensor);
cc.defineGetterSetter(_proto, "space", _proto.getSpace);
cc.defineGetterSetter(_proto, "surface_v", _proto.getSurfaceVelocity, _proto.setSurfaceVelocity);
cc.defineGetterSetter(_proto, "e", _proto.getElasticity, _proto.setElasticity);
cc.defineGetterSetter(_proto, "u", _proto.getFriction, _proto.setFriction);
cc.defineGetterSetter(_proto, "bb_l", _proto.getBBL, cp.privateAPI);
cc.defineGetterSetter(_proto, "bb_b", _proto.getBBB, cp.privateAPI);
cc.defineGetterSetter(_proto, "bb_r", _proto.getBBR, cp.privateAPI);
cc.defineGetterSetter(_proto, "bb_t", _proto.getBBT, cp.privateAPI);

_proto.cacheData = _proto.update;

//CircleShape properties
_proto = cp.CircleShape.prototype;
_proto.type  = "circle";
cc.defineGetterSetter(_proto, "r", _proto.getRadius);
cc.defineGetterSetter(_proto, "c", _proto.getOffset);

//SegmentShape properties
_proto = cp.SegmentShape.prototype;
_proto.type = "segment";
cc.defineGetterSetter(_proto, "a", _proto.getA);
cc.defineGetterSetter(_proto, "b", _proto.getB);
cc.defineGetterSetter(_proto, "n", _proto.getNormal);
cc.defineGetterSetter(_proto, "r", _proto.getRadius);
cc.defineGetterSetter(_proto, "a_tangent", cp.privateAPI);
cc.defineGetterSetter(_proto, "b_tangent", cp.privateAPI);

//PolyShape properties
_proto = cp.PolyShape.prototype;
_proto.type = "poly";
cc.defineGetterSetter(_proto, "verts", _proto.getVerts);

// Constraint properties
_proto = cp.Constraint.prototype;
cc.defineGetterSetter(_proto, "a", _proto.getA);
cc.defineGetterSetter(_proto, "b", _proto.getB);
cc.defineGetterSetter(_proto, "space", _proto.getSpace);
cc.defineGetterSetter(_proto, "maxForce", _proto.getMaxForce, _proto.setMaxForce);
cc.defineGetterSetter(_proto, "errorBias", _proto.getErrorBias, _proto.setErrorBias);
cc.defineGetterSetter(_proto, "maxBias", _proto.getMaxBias, _proto.setMaxBias);

// PinJoint properties
_proto = cp.PinJoint.prototype;
cc.defineGetterSetter(_proto, "anchr1", _proto.getAnchr1, _proto.setAnchr1);
cc.defineGetterSetter(_proto, "anchr2", _proto.getAnchr2, _proto.setAnchr2);
cc.defineGetterSetter(_proto, "dist", _proto.getDist, _proto.setDist);

//SlideJoint  properties
_proto = cp.SlideJoint.prototype;
cc.defineGetterSetter(_proto, "anchr1", _proto.getAnchr1, _proto.setAnchr1);
cc.defineGetterSetter(_proto, "anchr2", _proto.getAnchr2, _proto.setAnchr2);
cc.defineGetterSetter(_proto, "min", _proto.getMin, _proto.setMin);
cc.defineGetterSetter(_proto, "max", _proto.getMax, _proto.setMax);

//PivotJoint properties
_proto = cp.PivotJoint.prototype;
cc.defineGetterSetter(_proto, "anchr1", _proto.getAnchr1, _proto.setAnchr1);
cc.defineGetterSetter(_proto, "anchr2", _proto.getAnchr2, _proto.setAnchr2);

//GrooveJoint properties
_proto = cp.GrooveJoint.prototype;
cc.defineGetterSetter(_proto, "anchr2", _proto.getAnchr2, _proto.setAnchr2);
cc.defineGetterSetter(_proto, "grv_a", _proto.getGrooveA, _proto.setGrooveA);
cc.defineGetterSetter(_proto, "grv_b", _proto.getGrooveB, _proto.setGrooveB);

//DampedSpring properties
_proto = cp.DampedSpring.prototype;
cc.defineGetterSetter(_proto, "anchr1", _proto.getAnchr1, _proto.setAnchr1);
cc.defineGetterSetter(_proto, "anchr2", _proto.getAnchr2, _proto.setAnchr2);
cc.defineGetterSetter(_proto, "damping", _proto.getDamping, _proto.setDamping);
cc.defineGetterSetter(_proto, "restLength", _proto.getRestLength, _proto.setRestLength);
cc.defineGetterSetter(_proto, "stiffness", _proto.getStiffness, _proto.setStiffness);

//DampedRotarySpring properties
_proto = cp.DampedRotarySpring.prototype;
cc.defineGetterSetter(_proto, "restAngle", _proto.getRestAngle, _proto.setRestAngle);
cc.defineGetterSetter(_proto, "stiffness", _proto.getStiffness, _proto.setStiffness);
cc.defineGetterSetter(_proto, "damping", _proto.getDamping, _proto.setDamping);

//RotaryLimitJoint properties
_proto = cp.RotaryLimitJoint.prototype;
cc.defineGetterSetter(_proto, "min", _proto.getMin, _proto.setMin);
cc.defineGetterSetter(_proto, "max", _proto.getMax, _proto.setMax);

//RatchetJoint properties
_proto = cp.RatchetJoint.prototype;
cc.defineGetterSetter(_proto, "angle", _proto.getAngle, _proto.setAngle);
cc.defineGetterSetter(_proto, "phase", _proto.getPhase, _proto.setPhase);
cc.defineGetterSetter(_proto, "ratchet", _proto.getRatchet, _proto.setRatchet);

//GearJoint properties
_proto = cp.GearJoint.prototype;
cc.defineGetterSetter(_proto, "phase", _proto.getPhase, _proto.setPhase);
cc.defineGetterSetter(_proto, "ratio", _proto.getRatio, _proto.setRatio);

//SimpleMotor properties
_proto = cp.SimpleMotor.prototype;
cc.defineGetterSetter(_proto, "rate", _proto.getRate, _proto.setRate);

//Arbiter properties
_proto = cp.Arbiter.prototype;
cc.defineGetterSetter(_proto, "e", _proto.getElasticity, _proto.setElasticity);
cc.defineGetterSetter(_proto, "u", _proto.getFriction, _proto.setFriction);
cc.defineGetterSetter(_proto, "surface_vr", _proto.getSurfaceVelocity, _proto.setSurfaceVelocity);
// not auto-generated
cc.defineGetterSetter(_proto, "a", _proto.getPointA);
cc.defineGetterSetter(_proto, "b", _proto.getPointA);
cc.defineGetterSetter(_proto, "body_a", _proto.getBodyA);
cc.defineGetterSetter(_proto, "body_b", _proto.getBodyB);

_proto = null;

// Axmol keeps the actual Chipmunk 7.0.1 C solver behind the native cp peers.
// Match the two historical static-body spellings and v7 argument defaults.
cp.BodyStatic = cp.StaticBody;
cp.PinJoint.prototype.setAnchr1 = cp.PinJoint.prototype.setAnchorA;
cp.PinJoint.prototype.setAnchr2 = cp.PinJoint.prototype.setAnchorB;
cp.SlideJoint.prototype.setAnchr1 = cp.SlideJoint.prototype.setAnchorA;
cp.SlideJoint.prototype.setAnchr2 = cp.SlideJoint.prototype.setAnchorB;
cp.PivotJoint.prototype.setAnchr1 = cp.PivotJoint.prototype.setAnchorA;
cp.PivotJoint.prototype.setAnchr2 = cp.PivotJoint.prototype.setAnchorB;
cp.GrooveJoint.prototype.setAnchr2 = cp.GrooveJoint.prototype.setAnchorB;
cp.DampedSpring.prototype.setAnchr1 = cp.DampedSpring.prototype.setAnchorA;
cp.DampedSpring.prototype.setAnchr2 = cp.DampedSpring.prototype.setAnchorB;

// C-style APIs use the same checked peers as their object-oriented equivalents;
// the handle property does not expose an integer native address.
(function () {
    var aliases = {
        bodySetPosition: ['Body', 'setPosition'],
        shapeSetElasticity: ['Shape', 'setElasticity'],
        shapeSetFriction: ['Shape', 'setFriction'],
        shapeSetCollisionType: ['Shape', 'setCollisionType'],
        shapeGetCollisionType: ['Shape', 'getCollisionType'],
        spaceSetGravity: ['Space', 'setGravity'],
        spaceGetStaticBody: ['Space', 'getStaticBody'],
        spaceAddBody: ['Space', 'addBody'],
        spaceAddShape: ['Space', 'addShape'],
        spaceStep: ['Space', 'step'],
        spaceFree: ['Space', 'free'],
        spaceAddCollisionHandler: ['Space', 'addCollisionHandler'],
        spaceRemoveCollisionHandler: ['Space', 'removeCollisionHandler'],
        arbiterGetBodies: ['Arbiter', 'getBodies'],
        arbiterGetShapes: ['Arbiter', 'getShapes']
    };
    Object.keys(aliases).forEach(function (key) {
        var binding = aliases[key];
        cp[key] = function (peer) {
            return cp[binding[0]].prototype[binding[1]].apply(peer,
                Array.prototype.slice.call(arguments, 1));
        };
    });
})();

// Chipmunk-JS exposes inverse mass/moment as readonly fields. Read the
// current native values, including native static/kinematic infinity, so
// mass setters and shape-driven mass updates are reflected immediately.
cc.defineGetterSetter(cp.Body.prototype, 'm_inv', function () { return 1 / this.getMass(); });
cc.defineGetterSetter(cp.Body.prototype, 'i_inv', function () { return 1 / this.getMoment(); });

/* Current Jolt components implement the legacy Physics3D object contracts. */
(function () {
    if (!cc.Rigidbody3D) return;
    var types = {UNKNOWN:0,BOX:1,SPHERE:2,CYLINDER:3,CAPSULE:4,CONVEX:5,MESH:6,HEIGHT_FIELD:7,COMPOUND:8};
    jsb.Physics3DShape = function (type) {
        var a = Array.prototype.slice.call(arguments,1), shape;
        function copyVector(value) { return {x:value.x,y:value.y,z:value.z}; }
        // Current colliders have component ownership, so reusable legacy
        // shapes need a creation-time definition for each independent clone.
        // Keep the same values copied into the native shape, rather than live
        // caller arrays/vectors whose later mutation could change another body.
        if (type === types.BOX && a[0] && typeof a[0] === 'object') a[0] = copyVector(a[0]);
        else if ((type === types.CONVEX || type === types.MESH) && Array.isArray(a[0]))
            a[0] = a[0].map(copyVector);
        else if (type === types.HEIGHT_FIELD && Array.isArray(a[2])) a[2] = a[2].slice();
        if (type === types.BOX) shape = cc.BoxCollider3D.create(a[0]);
        else if (type === types.SPHERE) shape = cc.SphereCollider3D.create(a[0]);
        else if (type === types.CYLINDER) shape = cc.CylinderCollider3D.create(a[0]*0.5,a[1]);
        else if (type === types.CAPSULE) shape = cc.CapsuleCollider3D.create(a[0],a[1]);
        else if (type === types.CONVEX) shape = cc.ConvexCollider3D.create(a[0]);
        else if (type === types.MESH) {
            if (a[1] !== undefined && a[0].length !== a[1]*3) throw Error('Physics3D mesh triangle count differs from its vertices');
            shape = cc.MeshCollider3D.create(a[0]);
        }
        else if (type === types.HEIGHT_FIELD) {
            // Old useFloatData selects the numeric float samples used by its
            // terrain test. Jolt consumes the same float array directly.
            if (!a[6]) throw Error('Physics3D terrain requires float height samples');
            var flip = a[7], diamond = a.length >= 9 ? a[8] : false;
            shape = cc.HeightFieldCollider3D.create(a[0],a[1],a[2],a[3],a[4],a[5],!!flip,!!diamond);
        }
        else throw Error('Unsupported Physics3D shape type '+type);
        if (!shape) throw Error('Cannot create native Physics3D collider');
        shape.__legacyShapeDefinition={type:type,args:a};
        return shape;
    };
    jsb.Physics3DShape.ShapeType = types;
    cc.Collider3D.prototype.getShapeType = cc.Collider3D.prototype.getType;
    jsb.Physics3DRigidBodyDes = function () {
        this.mass=0; this.localInertia=cc.math.vec3(0,0,0); this.shape=null; this.disableSleep=false;
        this.originalTransform=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];
    };
    jsb.physics3DRigidBodyDes = function () { return new jsb.Physics3DRigidBodyDes(); };
    jsb.Physics3DRigidBody = function (description) {
        if (!description || !description.shape) throw TypeError('A rigid body requires its native collider descriptor');
        // A legacy shape is reusable across many rigid bodies. Current colliders
        // carry component ownership, so each body receives its own collider
        // with the same actual shape parameters.
        var definition=description.shape.__legacyShapeDefinition;
        var collider=definition ? jsb.Physics3DShape.apply(null,[definition.type].concat(definition.args)) : description.shape;
        var body=cc.Rigidbody3D.create(collider,description.mass || 0);
        if (!body) throw Error('Cannot create native Jolt body');
        body.__setKinematicZeroStepSync(true);
        body.setAllowSleeping(!description.disableSleep);
        if (description.localInertia && (description.localInertia.x || description.localInertia.y || description.localInertia.z)) body.setInertia(description.localInertia);
        body.__legacyOriginalTransform=description.originalTransform.slice();
        return body;
    };
    jsb.Physics3DRigidBody.prototype=cc.Rigidbody3D.prototype;
    jsb.Physics3DRigidBody.create=function (description) { return new jsb.Physics3DRigidBody(description); };
    jsb.Physics3DObject=cc.PhysicsActor;
    jsb.Physics3DObject.PhysicsObjType={UNKNOWN:0,RIGID_BODY:1};
    cc.PhysicsActor.prototype.getObjType=function () { return this.getActorType() === 1 ? 1 : 0; };
    var setKinematic=cc.Rigidbody3D.prototype.setKinematic;
    cc.Rigidbody3D.prototype.setKinematic=function (enabled) {
        if (arguments.length && !enabled)
            return this.setMotionType(this.getMass() > 0 ? cc.Rigidbody3D.MotionType.Dynamic : cc.Rigidbody3D.MotionType.Static);
        return setKinematic.call(this);
    };
    cc.Rigidbody3D.prototype.disableSleep=function (disabled) { this.setAllowSleeping(!disabled); };
    cc.Rigidbody3D.prototype.setCcdMotionThreshold=function (threshold) {
        this.setCollisionDetectionMode(threshold > 0 ? cc.Rigidbody3D.CollisionDetectionMode.Continuous : cc.Rigidbody3D.CollisionDetectionMode.Discrete);
        this.__legacyCcdThreshold=threshold;
    };
    cc.Rigidbody3D.prototype.setCcdSweptSphereRadius=function (radius) {
        if (!(radius >= 0)) throw RangeError('CCD radius must be nonnegative');
        // Jolt LinearCast performs CCD with the real collider instead of the
        // optional Bullet swept sphere; retain the requested compatibility value.
        this.__legacyCcdRadius=radius;
        if (radius > 0) this.setCollisionDetectionMode(cc.Rigidbody3D.CollisionDetectionMode.Continuous);
    };
    jsb.Physics3DComponent=function (body,offset,rotation) {
        if (!(body instanceof cc.Rigidbody3D)) throw TypeError('Physics3DComponent requires a native rigid body');
        if (offset) body.setTransformInPhysics(offset,rotation || cc.math.quaternion(0,0,0,1));
        body.__setKinematicZeroStepSync(true);
        return body;
    };
    jsb.Physics3DComponent.prototype=cc.Rigidbody3D.prototype;
    jsb.Physics3DComponent.PhysicsSyncFlag={NONE:0,NODE_TO_PHYSICS:1,PHYSICS_TO_NODE:2,NODE_AND_NODE:3,NODE_AND_PHYSICS:3};
    cc.Rigidbody3D.prototype.getPhysicsObj=function () { return this; };
    jsb.PhysicsSprite3D=function (file,description) {
        var node=jsb.Sprite3D.create(file), body=new jsb.Physics3DRigidBody(description);
        if (!node) throw Error('Cannot load PhysicsSprite3D mesh '+file);
        node.addComponent(body);
        var matrix=body.__legacyOriginalTransform;
        if (matrix) node.setNodeToParentTransform(matrix);
        node.getPhysicsObj=function () { return body; };
        node.setSyncFlag=function (flag) { body.setSyncFlag(flag); };
        node.getSyncFlag=function () { return body.getSyncFlag(); };
        node.syncNodeToPhysics=function () { body.syncNodeToPhysics(); };
        node.syncPhysicsToNode=function () { body.syncPhysicsToNode(); };
        return node;
    };
    jsb.PhysicsSprite3D.prototype=jsb.Sprite3D.prototype;
    jsb.PhysicsSprite3D.create=function (file,description) { return new jsb.PhysicsSprite3D(file,description); };
    cc.PhysicsWorld3D.prototype.setDebugDrawEnable=cc.PhysicsWorld3D.prototype.setDebugDrawEnabled;
    cc.Scene.prototype.setPhysics3DDebugCamera=cc.Scene.prototype.setDebugCamera;
    cc.Scene.prototype.setNavMeshDebugCamera=cc.Scene.prototype.setDebugCamera;
    if (cc.NavMesh) cc.NavMesh.prototype.setDebugDrawEnable=cc.NavMesh.prototype.setDebugDrawEnabled;

    function attached(joint,body) { joint.__legacyAttachedBody=body; joint.setCoordinateSpace(cc.Joint3D.CoordinateSpace.Local); return joint; }
    function worldDirection(body,axis) {
        var origin=body.local2World(cc.math.vec3(0,0,0)), end=body.local2World(axis);
        return cc.math.vec3(end.x-origin.x,end.y-origin.y,end.z-origin.z);
    }
    function frameJoint(type,a,b,fa,fb) {
        var other=b instanceof cc.Rigidbody3D ? b : null, matrix=other ? fa : b;
        return attached(type.__legacyCreateFrames(a,other,matrix,other ? fb : null),a);
    }
    jsb.Physics3DPointToPointConstraint=function (a,b,p,q) {
        var other=b instanceof cc.Rigidbody3D ? b : null, anchor=other ? p : b;
        var joint=attached(cc.PivotJoint3D.create(other),a); joint.setAnchor(anchor);
        joint.setConnectedAnchor(other ? q : a.local2World(anchor)); return joint;
    };
    jsb.Physics3DPointToPointConstraint.prototype=cc.PivotJoint3D.prototype;
    cc.PivotJoint3D.prototype.setPivotPointInB=cc.PivotJoint3D.prototype.setConnectedAnchor;
    jsb.Physics3DHingeConstraint=function (a,b,p,q,x,y) {
        var other=b instanceof cc.Rigidbody3D ? b : null, pivot=other ? p : b, axis=other ? x : p;
        var joint=attached(cc.HingeJoint3D.create(other),a); joint.setAnchor(pivot); joint.setAxis(axis);
        joint.setConnectedAnchor(other ? q : a.local2World(pivot)); joint.setConnectedAxis(other ? y : worldDirection(a,axis)); return joint;
    };
    jsb.Physics3DHingeConstraint.prototype=cc.HingeJoint3D.prototype;
    cc.HingeJoint3D.prototype.setLimit=function (lower,upper) { this.__setLegacyLimits(cc.radiansToDegrees(lower),cc.radiansToDegrees(upper)); };
    jsb.Physics3DSliderConstraint=function (a,b,fa,fb) {
        return frameJoint(cc.SliderJoint3D,a,b,fa,fb);
    };
    jsb.Physics3DSliderConstraint.prototype=cc.SliderJoint3D.prototype;
    cc.SliderJoint3D.prototype.setLowerLinLimit=function (lower) { this.__legacyLowerLinear=lower; this.__setLegacyLimits(lower,this.__legacyUpperLinear===undefined ? 0 : this.__legacyUpperLinear); };
    cc.SliderJoint3D.prototype.setUpperLinLimit=function (upper) { this.__legacyUpperLinear=upper; this.__setLegacyLimits(this.__legacyLowerLinear===undefined ? 0 : this.__legacyLowerLinear,upper); };
    jsb.Physics3DConeTwistConstraint=function (a,b,fa,fb) {
        return frameJoint(cc.ConeTwistJoint3D,a,b,fa,fb);
    };
    jsb.Physics3DConeTwistConstraint.prototype=cc.ConeTwistJoint3D.prototype;
    cc.ConeTwistJoint3D.prototype.setLimit=function (a,b,c) { this.__setLegacyLimits(cc.radiansToDegrees(a),cc.radiansToDegrees(b),cc.radiansToDegrees(c)*2); };
    jsb.Physics3D6DofConstraint=function (a,b,fa,fb) {
        return frameJoint(cc.SixDofJoint3D,a,b,fa,fb);
    };
    jsb.Physics3D6DofConstraint.prototype=cc.SixDofJoint3D.prototype;
    function sixLimit(name,angular,lower) {
        cc.SixDofJoint3D.prototype[name]=function (value) {
            var key=angular ? '__legacyAngular' : '__legacyLinear', data=this[key] || {lower:cc.math.vec3(0,0,0),upper:cc.math.vec3(0,0,0)};
            data[lower ? 'lower' : 'upper']=angular ? cc.math.vec3(cc.radiansToDegrees(value.x),cc.radiansToDegrees(value.y),cc.radiansToDegrees(value.z)) : value;
            this[key]=data; this[angular ? '__setLegacyAngularLimits' : '__setLegacyLinearLimits'](data.lower,data.upper);
        };
    }
    sixLimit('setLinearLowerLimit',false,true); sixLimit('setLinearUpperLimit',false,false);
    sixLimit('setAngularLowerLimit',true,true); sixLimit('setAngularUpperLimit',true,false);
    cc.PhysicsWorld3D.prototype.addPhysics3DConstraint=function (joint,disableCollision) {
        if (!joint.__legacyAttachedBody) throw Error('Constraint has no attached legacy body');
        return this.__addLegacyJoint(joint,joint.__legacyAttachedBody,!!disableCollision);
    };
})();

