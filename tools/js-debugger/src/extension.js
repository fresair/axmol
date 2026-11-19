// Copyright (c) 2026 Axmol Engine contributors. SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
'use strict';
const vscode = require('vscode');
const path = require('node:path');

exports.activate = function activate(context) {
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('axmol-js', {
        createDebugAdapterDescriptor() {
            return new vscode.DebugAdapterExecutable(process.execPath,
                [path.join(context.extensionPath, 'src', 'adapter.js')],
                {env: {ELECTRON_RUN_AS_NODE: '1'}});
        }
    }));
};
exports.deactivate = function deactivate() {};
