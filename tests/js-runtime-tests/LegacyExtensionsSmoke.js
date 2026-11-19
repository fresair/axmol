/* Copyright (c) 2026 Axmol contributors. SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications). */
(function (global) {
    function check(value, message) { if (!value) throw Error('Legacy extension: ' + message); }
    var control = new cc.Control();
    control.retain();
    var receiver = {count: 0, events: []};
    function action(sender, event) {
        check(this === receiver && sender === control, 'control receiver/wrapper identity');
        this.count++;
        this.events.push(event);
    }
    control.addTargetWithActionForControlEvents(receiver, action,
        cc.CONTROL_EVENT_TOUCH_DOWN | cc.CONTROL_EVENT_VALUECHANGED);
    jsb.garbageCollect();
    control.sendActionsForControlEvents(cc.CONTROL_EVENT_TOUCH_DOWN | cc.CONTROL_EVENT_VALUECHANGED);
    check(receiver.count === 2, 'native multi-event dispatch');
    control.removeTargetWithActionForControlEvents(receiver, action, cc.CONTROL_EVENT_VALUECHANGED);
    control.sendActionsForControlEvents(cc.CONTROL_EVENT_TOUCH_DOWN | cc.CONTROL_EVENT_VALUECHANGED);
    check(receiver.count === 3 && receiver.events[2] === cc.CONTROL_EVENT_TOUCH_DOWN, 'partial mask removal');
    control.removeTargetWithActionForControlEvents(null, null, 511);
    var selfCount = 0, addedCount = 0;
    function added() { addedCount++; }
    function cancelSelf(sender) {
        selfCount++;
        sender.removeTargetWithActionForControlEvents(receiver, cancelSelf, cc.CONTROL_EVENT_VALUECHANGED);
        sender.addTargetWithActionForControlEvents(receiver, added, cc.CONTROL_EVENT_VALUECHANGED);
    }
    control.addTargetWithActionForControlEvents(receiver, cancelSelf, cc.CONTROL_EVENT_VALUECHANGED);
    control.sendActionsForControlEvents(cc.CONTROL_EVENT_VALUECHANGED);
    check(selfCount === 1 && addedCount === 0, 'self cancellation / snapshot additions');
    control.sendActionsForControlEvents(cc.CONTROL_EVENT_VALUECHANGED);
    check(selfCount === 1 && addedCount === 1, 'new action next dispatch');
    control.enabled = false; control.selected = true; control.highlighted = true;
    check(!control.isEnabled() && control.isSelected() && control.isHighlighted(), 'native control properties');

    var label = global.__LegacyNativeControlLabel, background = global.__LegacyNativeControlBackground;
    var button = new cc.ControlButton(label, background, null, null, false);
    check(button.getTitleLabelForState(cc.CONTROL_STATE_NORMAL) === label &&
        button.getBackgroundSpriteForState(cc.CONTROL_STATE_NORMAL) === background,
        'ControlButton Styling signature uses its actual native label/background');
    check(!button.doesAdjustBackgroundImage() && !button.getAdjustBackgroundImage() &&
        !button.adjustBackgroundImage, 'explicit auto-size false survives null font arguments');
    button.adjustBackgroundImage = true;
    check(button.doesAdjustBackgroundImage(), 'legacy ControlButton property reaches native auto-size');

    var scroll = new cc.ScrollView(cc.size(120, 90));
    scroll.retain();
    scroll.getContainer().setContentSize(cc.size(400, 300));
    var scrolled = 0, zoomed = 0;
    var scrollDelegate = {
        scrollViewDidScroll: function (sender) { check(this === scrollDelegate && sender === scroll, 'scroll identity'); scrolled++; },
        scrollViewDidZoom: function (sender) { check(sender === scroll, 'zoom identity'); zoomed++; }
    };
    scroll.setDelegate(scrollDelegate);
    check(scroll.getDelegate() === scrollDelegate, 'getDelegate returns original JS object');
    scroll.width = 130; scroll.height = 100;
    check(scroll.width === 130 && scroll.height === 100, 'view dimensions');
    scroll.setContentOffset(cc.p(-25, -30));
    scroll.setZoomScale(1.25);
    check(scrolled > 0 && zoomed > 0, 'real native scroll and zoom events');

    var Cell = cc.TableViewCell.extend({ctor: function () { this._super(); this.marker = 'derived cell'; }});
    var created = 0, reused = 0, recycled = 0, touched = 0;
    var dataSource = {
        numberOfCellsInTableView: function (table) { check(this === dataSource, 'data-source receiver'); return 30; },
        tableCellSizeForIndex: function (table, index) { return cc.size(60, index === 2 ? 90 : 60); },
        tableCellAtIndex: function (table, index) {
            check(this === dataSource, 'cell data-source receiver');
            var cell = table.dequeueCell();
            if (cell) reused++; else { cell = new Cell(); created++; }
            check(cell instanceof Cell && cell.marker === 'derived cell', 'cell subclass survives reuse');
            cell.tag = index;
            return cell;
        }
    };
    var table = new cc.TableView(dataSource, cc.size(120, 120));
    table.retain();
    var tableDelegate = {
        tableCellWillRecycle: function (sender, cell) { check(this === tableDelegate && sender === table, 'recycle receiver'); recycled++; },
        tableCellTouched: function (sender, cell) { check(this === tableDelegate && sender === table && cell === table.cellAtIndex(cell.getIdx()), 'touch identities'); touched++; }
    };
    table.setDelegate(tableDelegate);
    check(table.getDataSource() === dataSource && table.getDelegate() === tableDelegate, 'table JS data-source / delegate getters');
    check(created > 0 && (table.cellAtIndex(0) !== null || table.cellAtIndex(29) !== null), 'real native table cells');
    table.setContentOffset(cc.p(0, -700));
    table.setContentOffset(cc.p(0, 0));
    table.reloadData();
    check(reused > 0 && recycled > 0, 'native table cell recycling');
    table.setVerticalFillOrder(cc.TABLEVIEW_FILL_TOPDOWN);
    check(table.getVerticalFillOrder() === cc.TABLEVIEW_FILL_TOPDOWN, 'native fill order');

    var box = global.__LegacyNativeEditBox;
    var edit = {begin: 0, end: 0, action: 0, changed: 0, returned: 0};
    var editDelegate = {
        editBoxEditingDidBegin: function (sender) { check(this === editDelegate && sender === box, 'edit begin identity'); edit.begin++; },
        editBoxTextChanged: function (sender, text) { check(sender === box && text === '中文😀', 'edit Unicode text'); edit.changed++; },
        editBoxEditingDidEnd: function (sender) { check(sender === box, 'legacy edit end'); edit.end++; },
        editBoxEditingDidEndWithAction: function (sender, action) { check(action === 3, 'edit native end action'); edit.action++; },
        editBoxReturn: function (sender) { check(sender === box, 'edit return'); edit.returned++; }
    };
    box.setDelegate(editDelegate);
    check(box.getDelegate() === editDelegate && cc.EditBox === ccui.EditBox && cc.Scale9Sprite === ccui.Scale9Sprite,
        'legacy EditBox/Scale9Sprite class aliases');
    cc.BuilderReader.registerController('LegacyProbeController', {ctor: function () { this.value = 7; }});
    check(new cc.BuilderReader._controllerClassCache.LegacyProbeController().value === 7, 'BuilderReader controller registration');
    global.LegacyExtensionsProbe = {control: control, scroll: scroll, table: table, edit: box,
        counts: function () { return {self: selfCount, added: addedCount, created: created, reused: reused,
            recycled: recycled, touched: touched, edit: edit}; }};
})(this);
