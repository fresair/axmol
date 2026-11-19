# CocosBuilder native reader

The native `.ccbi` reader, animation manager and node loaders were imported
from `cocos2d/cocos2d-x`, branch `v3`, commit
`e4b6a5ef8fcdc99a7ebea606312b67fe4c534b9f`, directory
`cocos/editor-support/cocosbuilder`. The original MIT copyright and license
notices are retained in each file.

The port uses `ax::Object`, the current `Vec2`/color types, current engine
actions and AudioEngine, `ax::ext::Control`/`ControlButton`/`ScrollView`, and
`ax::ui::Scale9Sprite`. Legacy serialized OpenGL blend factors are converted
to current RHI enum values when parsing the file. Deprecated `__Array` and
`__String` storage is replaced by a private native property-name collection.
It uses the engine's file, texture, animation and scheduler implementations.

The CMake target is `cocosbuilder`; public includes use
`cocosbuilder/CCBReader.h`. The JavaScript facade and controller/delegate
integration live in the separate `axjs` binding module.
