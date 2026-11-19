# CocoStudio compatibility fixtures

`StudioSmoke.js` runs with the compatibility bootstrap inside SpiderMonkey 33.1.
It checks native node classes, wrapper identity, nested projects, animation tags,
interpolation, frame events, exported UI callbacks, errors and native CSB loading.
The JSON fixtures cover the 1.x scene/UI/timeline and 2.x timeline formats;
`timeline.csb` uses Axmol's CocoStudio FlatBuffers schema.

Select `AXMOL_JS_STUDIO_SMOKE=1` to run the fixture script. UI Layout constructors
require an initialized graphics device; the app creates a RenderView first.
The optional graphics CTest entry is `js-studio-smoke`. A successful load or a
host-side check alone does not establish rendered UI or interaction acceptance.
