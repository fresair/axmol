# jsb.reflection resolves this example class and its methods by their names.
# Add equivalent rules for your own reflected native entrypoints.
-keep class dev.axmol.app.JsBridge {
    public static *;
}
