/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
if (typeof LegacyComponentModuleLoads === 'undefined') {
    var LegacyComponentModuleLoads = 0;
    var LegacyComponentCtorCalls = 0;
}
LegacyComponentModuleLoads++;
LegacyScriptComponent = cc.ComponentJS.extend({
    total: 0,
    adds: 0,
    enters: 0,
    exits: 0,
    removes: 0,
    updates: 0,
    ctor: function () { LegacyComponentCtorCalls++; },
    onAdd: function () {
        if (!this.getOwner()) throw Error('ComponentJS onAdd owner');
        this.adds++;
        this._super();
    },
    onEnter: function (value) {
        if (value !== 1 || this.getOwner().getComponent(this.getName()) !== this || this.getScriptObject() !== this)
            throw Error('ComponentJS onEnter receiver/owner/legacy argument');
        this.enters++;
        this._super();
    },
    onExit: function (value) {
        if (value !== 1 || !this.getOwner()) throw Error('ComponentJS onExit owner/legacy argument');
        this.exits++;
        this._super();
    },
    onRemove: function () {
        if (!this.getOwner()) throw Error('ComponentJS onRemove owner before detach');
        this.removes++;
        this._super();
    },
    update: function (dt) {
        if (this.getOwner().getComponent(this.getName()) !== this) throw Error('ComponentJS update identity');
        this.total += dt;
        this.updates++;
        this._super(dt);
    }
});
