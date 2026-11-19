# SpiderMonkey 33.1 debug actor protocol

`script/jsb_debugger.js` runs in a separate SpiderMonkey compartment containing
the real `Debugger` object. Game frames pause synchronously in a native nested
event loop. Network threads only exchange packets; all inspection, evaluation
and resume operations execute on the owning VM thread.

The server retains the Cocos2d-x v3 remote protocol: `<UTF-8 byte count>:<JSON>`,
port 5086 by default. The root, tab and thread actors support `listTabs`,
`attach`, `detach`, `sources`, `frames`, `interrupt`, `resume`, `reconfigure`,
`clientEvaluate` and `setBreakpoint`. Source actors support `source` and
`setBreakpoint`; breakpoint actors support `delete`; environment actors expose
`bindings`; object actors expose `prototypeAndProperties`. Resume limits are
`step`, `next`, and `finish`. Object/frame/environment actors expire on resume.

Axmol additionally accepts a `_requestId` which is echoed in replies, and
emits `breakpointResolved` when a breakpoint is installed in a newly compiled
script. The VS Code DAP adapter uses these extensions. A legacy client can omit
the ID and use the actor request/reply sequence. Full Firefox browser actors,
DOM actors, and browser console APIs are not supplied.

Disconnect resumes a stopped game, removes all breakpoint and step hooks, and
clears pause references. The listener remains available for a fresh connection.
VM shutdown joins the IO thread before removing the persistent debugger global
and restores the previous VM interrupt callback. A new VM gets a fresh server.

The implementation derives its actor names and packet shapes from the MIT
Cocos2d-x v3 debugger. It uses SpiderMonkey's own Debugger API rather than
redistributing Mozilla's browser actor implementation. See
[`tools/js-debugger`](../../../../../tools/js-debugger/README.md) for installation,
startup waits, port forwarding, and debugger configuration.
