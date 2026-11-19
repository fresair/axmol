/****************************************************************************
 Copyright (c) 2019-present Simdsoft Limited.
 Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).

 https://axmol.dev/

 SPDX-License-Identifier: MIT
 ****************************************************************************/
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace Axmol.LuaBindings;

/// <summary>
/// SpiderMonkey 33.1 backend. Collection and overload metadata are shared with
/// Lua; every generated entry is a checked JSNative calling the native API.
/// Ownership, identity, conversion, and roots stay in JsRuntime.h.
/// </summary>
internal static class JsCppEmitter
{
    internal sealed record Source(string FileName, string Contents);
    private static string Registration(GenerationRequest request) =>
        string.IsNullOrWhiteSpace(request.RegistrationName) ? request.Module : request.RegistrationName;
    private static string Id(string value) => Regex.Replace(value, @"[^A-Za-z0-9_]", "_");
    private static string Escape(string value) => value.Replace("\\", "\\\\").Replace("\"", "\\\"");
    private static readonly HashSet<string> ValueTypes = new(StringComparer.Ordinal)
    {
        "void", "bool", "char", "signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int",
        "long", "unsigned long", "long long", "unsigned long long", "float", "double", "long double", "size_t", "ssize_t",
        "ptrdiff_t", "intptr_t", "uintptr_t", "int8_t", "uint8_t", "int16_t", "uint16_t", "int32_t", "uint32_t",
        "int64_t", "uint64_t", "std::string", "std::string_view", "ax::Vec2", "ax::Vec3", "ax::Vec4", "ax::Size",
        "ax::Rect", "ax::Color32", "ax::Color", "ax::BlendFunc", "ax::AffineTransform", "ax::Mat4", "ax::Value",
        "ax::Quat", "ax::Quaternion", "ax::Viewport", "ax::PhysicsMaterial2D", "ax::NavMeshAgentParam", "ax::OffMeshLinkData",
        "ax::AABB", "ax::OBB", "ax::Ray", "ax::Terrain::DetailMap", "ax::Terrain::TerrainData", "ax::PhysicsMaterial"
    };

    public static BindingClass[] SelectSupported(GenerationRequest request, IReadOnlyList<BindingClass> classes,
        IReadOnlyList<BindingEnum> enums, IEnumerable<string> knownEnums, ICollection<GenerationDiagnostic> diagnostics)
    {
        var namedTypes = classes.Select(x => x.QualifiedName).ToHashSet(StringComparer.Ordinal);
        var enumTypes = enums.Select(x => x.QualifiedName).Concat(knownEnums).ToHashSet(StringComparer.Ordinal);
        bool Supported(string source)
        {
            if (Regex.IsMatch(source, @"^\s*(?:const\s+char|char\s+const)\s*\*\s*$")) return true;
            var type = Regex.Replace(source, @"\bconst\b|\bvolatile\b", "").Trim().TrimEnd('&').Trim();
            if (ValueTypes.Contains(type) || ValueTypes.Contains(type.StartsWith("std::", StringComparison.Ordinal) ? type[5..] : type))
                return true;
            if (type.Contains("std::function<", StringComparison.Ordinal))
            {
                var start = type.IndexOf('<');
                var inner = type[(start + 1)..type.LastIndexOf('>')].Trim();
                var paren = inner.IndexOf('(');
                return paren > 0 && Supported(inner[..paren]) &&
                    SplitArguments(inner[(paren + 1)..inner.LastIndexOf(')')]).All(Supported);
            }
            foreach (var container in new[] { "std::vector", "std::set", "std::unordered_set", "std::map", "std::unordered_map", "ax::Vector", "ax::Map", "std::pair", "std::array", "std::span", "tsl::robin_map" })
            {
                if (!type.StartsWith(container + "<", StringComparison.Ordinal))
                    continue;
                var arguments = SplitArguments(type[(type.IndexOf('<') + 1)..type.LastIndexOf('>')]);
                var count = container is "std::map" or "std::unordered_map" or "ax::Map" or "std::pair" or "tsl::robin_map" ? 2 : 1;
                return arguments.Count >= count && arguments.Take(count).All(Supported);
            }
            // An exported class has a checked type registry and a native lifetime
            // contract. Erased pointers, unbound records and scalar out-params
            // require a documented manual adapter rather than a best-effort cast.
            if (enumTypes.Contains(type)) return true;
            if (!type.EndsWith('*')) return false;
            type = type.TrimEnd('*').Trim();
            if (namedTypes.Contains(type))
                return true;
            var shortName = type[(type.LastIndexOf("::", StringComparison.Ordinal) + 2)..];
            var cppNamespace = type.Contains("::", StringComparison.Ordinal) ? type[..type.LastIndexOf("::", StringComparison.Ordinal)] : "";
            return request.BindingClassSelections.Any(selection =>
                selection.NativeNamespaces.Contains(cppNamespace, StringComparer.Ordinal) &&
                selection.ClassPatterns.Any(pattern => PatternMatches(shortName, pattern)));
        }
        var result = new List<BindingClass>();
        bool SupportedParameter(string source)
        {
            if (!Supported(source)) return false;
            if (!source.Contains("std::span<", StringComparison.Ordinal)) return true;
            var type = source.Trim();
            // Only a top-level span has dedicated owning argument storage.
            // Container/span nesting and mutable/volatile span references
            // require an explicit adapter rather than Converter<span>::read.
            if (Regex.IsMatch(type, @"\bvolatile\b", RegexOptions.CultureInvariant)) return false;
            var normalized = Regex.Replace(type, @"^const\s+|\s+const(?=\s*&?\s*$)", "").Trim();
            if (!normalized.StartsWith("std::span<", StringComparison.Ordinal)) return false;
            if (type.EndsWith('&') && !Regex.IsMatch(type, @"^const\s+std::span<|>\s*const\s*&$", RegexOptions.CultureInvariant)) return false;
            var element = type[(type.IndexOf('<') + 1)..type.LastIndexOf('>')];
            // Argument<string_view> owns its bytes, but vector<string_view>
            // cannot read JS strings safely. Keep these element views manual.
            return !element.Contains("std::span<", StringComparison.Ordinal) &&
                !element.Contains("std::string_view", StringComparison.Ordinal) &&
                !Regex.IsMatch(element, @"\bchar\s*\*|\bchar\s+const\s*\*", RegexOptions.CultureInvariant);
        }
        foreach (var binding in classes)
        {
            var constructors = binding.Constructors.Where(x => x.ParameterTypes.All(SupportedParameter)).ToArray();
            var methods = binding.Methods.Where(x => !x.IsVariadic &&
                !x.ReturnType.Contains("std::function", StringComparison.Ordinal) &&
                Supported(x.ReturnType) && x.ParameterTypes.All(SupportedParameter)).ToArray();
            var fields = binding.Fields.Where(x => Supported(x.FieldType)).ToArray();
            var omitted = binding.Methods.Count - methods.Length + binding.Constructors.Count - constructors.Length + binding.Fields.Count - fields.Length;
            if (omitted > 0)
                diagnostics.Add(new GenerationDiagnostic
                {
                    Severity = "warning", Code = "AXJS001",
                    Message = $"{binding.QualifiedName}: {omitted} signatures require manual conversion/lifetime adapters; see the JS API manifest."
                });
            result.Add(new BindingClass
            {
                NativeName = binding.NativeName, QualifiedName = binding.QualifiedName,
                LuaName = binding.LuaName, LuaClassName = binding.LuaClassName, IsAbstract = binding.IsAbstract,
                Constructors = constructors, Bases = binding.Bases, Methods = methods, Fields = fields
            });
        }
        return result.ToArray();
    }

    private static bool PatternMatches(string name, string pattern)
    {
        if (pattern == "*") return true;
        var match = Regex.Match(name, pattern, RegexOptions.CultureInvariant);
        return match.Success && match.Index == 0 && match.Length == name.Length;
    }

    private static IReadOnlyList<string> SplitArguments(string value)
    {
        if (string.IsNullOrWhiteSpace(value) || value.Trim() == "void") return Array.Empty<string>();
        var result = new List<string>();
        var depth = 0;
        var start = 0;
        for (var i = 0; i < value.Length; ++i)
        {
            if (value[i] is '<' or '(' or '[') ++depth;
            else if (value[i] is '>' or ')' or ']') --depth;
            else if (value[i] == ',' && depth == 0)
            {
                result.Add(value[start..i].Trim()); start = i + 1;
            }
        }
        result.Add(value[start..].Trim());
        return result;
    }

    private static void Preamble(StringBuilder text)
    {
        text.AppendLine("/****************************************************************************");
        text.AppendLine(" Copyright (c) 2019-present Simdsoft Limited.");
        text.AppendLine(" Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).");
        text.AppendLine(" SPDX-License-Identifier: MIT");
        text.AppendLine(" ****************************************************************************/");
        text.AppendLine("// Generated by Axmol bindings generator (SpiderMonkey 33.1). Do not edit.");
    }

    public static string EmitHeader(GenerationRequest request)
    {
        var text = new StringBuilder(); Preamble(text);
        text.AppendLine("#pragma once");
        text.AppendLine("#include \"js-bindings/runtime/JsRuntime.h\"");
        text.AppendLine("namespace axjs {");
        text.Append("bool register_all_").Append(Registration(request)).AppendLine("(JSContext* cx, JS::HandleObject global);");
        text.AppendLine("}");
        return text.ToString();
    }

    public static IReadOnlyList<Source> EmitSources(GenerationRequest request, IReadOnlyList<BindingClass> classes,
        IReadOnlyList<BindingEnum> enums)
    {
        var count = Math.Min(Math.Max(1, request.CppChunkCount), Math.Max(1, classes.Count));
        var chunks = Enumerable.Range(0, count).Select(_ => new List<BindingClass>()).ToArray();
        // Keep classes in stable contiguous shards. Dispatcher registration order
        // is topological, independent of shard layout and alphabetical names.
        for (var i = 0; i < classes.Count; ++i) chunks[i * count / classes.Count].Add(classes[i]);
        var sources = new List<Source> { new($"axjs_{request.Module}_gen.cpp", Dispatcher(request, classes, enums)) };
        for (var i = 0; i < chunks.Length; ++i)
            sources.Add(new Source($"axjs_{request.Module}_gen_{i}.cpp", Chunk(request, chunks[i], enums)));
        return sources;
    }

    private static string Dispatcher(GenerationRequest request, IReadOnlyList<BindingClass> classes, IReadOnlyList<BindingEnum> enums)
    {
        var text = new StringBuilder(); Preamble(text);
        text.Append("#include \"axjs_").Append(request.Module).AppendLine("_gen.h\"");
        text.AppendLine("namespace axjs::detail {");
        foreach (var binding in classes)
            text.Append("bool register_").Append(Registration(request)).Append('_').Append(Id(binding.QualifiedName))
                .AppendLine("(JSContext*, JS::HandleObject);");
        text.AppendLine("}");
        text.AppendLine("namespace axjs {");
        text.Append("bool register_all_").Append(Registration(request)).AppendLine("(JSContext* cx, JS::HandleObject global)");
        text.AppendLine("{");
        OpenGuard(text, request);
        foreach (var binding in Topological(classes))
            text.Append("    if (!detail::register_").Append(Registration(request)).Append('_').Append(Id(binding.QualifiedName))
                .AppendLine("(cx, global)) return false;");
        // Standalone enums are emitted here instead of every shard. Class enums
        // are attached to their class constructors in their registration entry.
        foreach (var enumeration in enums.Where(x => x.OwnerQualifiedName is null))
        {
            text.AppendLine("    {");
            text.AppendLine("        JS::RootedObject module(cx);");
            text.Append("        if (!getOrCreateNamespace(cx, global, \"").Append(Escape(request.LuaNamespace)).AppendLine("\", &module)) return false;");
            EmitEnum(text, enumeration, "module", "        ");
            text.AppendLine("    }");
        }
        CloseGuard(text, request);
        text.AppendLine("    return true;");
        text.AppendLine("}"); text.AppendLine("}");
        return text.ToString();
    }

    private static IReadOnlyList<BindingClass> Topological(IReadOnlyList<BindingClass> classes)
    {
        var index = classes.ToDictionary(x => x.QualifiedName, StringComparer.Ordinal);
        var seen = new HashSet<string>(StringComparer.Ordinal);
        var result = new List<BindingClass>();
        void Visit(BindingClass type)
        {
            if (!seen.Add(type.QualifiedName)) return;
            foreach (var baseName in type.Bases)
                if (index.TryGetValue(baseName, out var parent)) Visit(parent);
            result.Add(type);
        }
        foreach (var type in classes) Visit(type);
        return result;
    }

    private static string Chunk(GenerationRequest request, IReadOnlyList<BindingClass> classes, IReadOnlyList<BindingEnum> enums)
    {
        var text = new StringBuilder(); Preamble(text);
        text.AppendLine("#include \"js-bindings/runtime/JsRuntime.h\"");
        OpenGuard(text, request);
        foreach (var header in request.Headers)
            text.Append("#include \"").Append(Escape(Path.GetRelativePath(request.RepositoryRoot, header).Replace('\\', '/'))).AppendLine("\"");
        text.AppendLine("namespace axjs::detail {");
        text.AppendLine("template<class T> static bool allocateIfDefaultConstructible(JSContext* cx, JS::CallArgs& args)");
        text.AppendLine("{");
        text.AppendLine("    if constexpr (std::is_default_constructible_v<T> && std::is_destructible_v<T>)");
        text.AppendLine("        return constructUninitialized<T>(cx, args);");
        text.AppendLine("    else { JS_ReportError(cx, \"Native type has no public default constructor\"); return false; }");
        text.AppendLine("}");
        foreach (var binding in classes) EmitClass(text, request, binding, enums);
        text.AppendLine("}");
        CloseGuard(text, request);
        return text.ToString();
    }

    private static void EmitClass(StringBuilder text, GenerationRequest request, BindingClass binding, IReadOnlyList<BindingEnum> enums)
    {
        var id = Id(binding.QualifiedName);
        var instance = binding.Methods.Where(x => !x.IsStatic).GroupBy(x => x.LuaName).ToArray();
        var statics = binding.Methods.Where(x => x.IsStatic).GroupBy(x => x.LuaName).ToArray();
        foreach (var methods in instance.Concat(statics))
            EmitMethod(text, binding, methods.ToArray(), id + (methods.First().IsStatic ? "_static_" : "_method_") + Id(methods.Key));
        EmitConstructor(text, binding, id + "_constructor");
        if (!binding.IsAbstract)
        {
            text.Append("static bool ").Append(id).AppendLine("_allocate(JSContext* cx, unsigned argc, JS::Value* vp)");
            text.AppendLine("{");
            text.AppendLine("    auto args = JS::CallArgsFromVp(argc, vp);");
            text.Append("    return allocateIfDefaultConstructible<").Append(binding.QualifiedName).AppendLine(">(cx, args);");
            text.AppendLine("}");
        }
        foreach (var field in binding.Fields) EmitField(text, binding, field, id + "_field_" + Id(field.NativeName));

        text.Append("static JSClass ").Append(id).AppendLine("_class = [] { JSClass type{};");
        text.Append("    type.name = \"").Append(Escape(binding.LuaClassName)).AppendLine("\";");
        text.AppendLine("    type.flags = JSCLASS_HAS_PRIVATE;");
        text.AppendLine("    type.addProperty = JS_PropertyStub; type.delProperty = JS_DeletePropertyStub;");
        text.AppendLine("    type.getProperty = JS_PropertyStub; type.setProperty = JS_StrictPropertyStub;");
        text.AppendLine("    type.enumerate = JS_EnumerateStub; type.resolve = JS_ResolveStub;");
        text.AppendLine("    type.convert = JS_ConvertStub; type.finalize = axjs::finalizeNative;");
        text.AppendLine("    return type; }();");
        EmitFunctions(text, id + "_methods", instance, id + "_method_");
        EmitFunctions(text, id + "_statics", statics, id + "_static_");
        text.Append("static const JSPropertySpec ").Append(id).AppendLine("_properties[] = {");
        foreach (var field in binding.Fields)
            text.Append("    JS_PSGS(\"").Append(Escape(field.LuaName)).Append("\", ").Append(id).Append("_field_").Append(Id(field.NativeName))
                .Append("_get, ").Append(id).Append("_field_").Append(Id(field.NativeName)).AppendLine("_set, JSPROP_ENUMERATE),");
        text.AppendLine("    JS_PS_END"); text.AppendLine("};");
        text.Append("bool register_").Append(Registration(request)).Append('_').Append(id).AppendLine("(JSContext* cx, JS::HandleObject global)");
        text.AppendLine("{");
        text.AppendLine("    JS::RootedObject module(cx);");
        text.Append("    if (!getOrCreateNamespace(cx, global, \"").Append(Escape(request.LuaNamespace)).AppendLine("\", &module)) return false;");
        var parent = binding.Bases.FirstOrDefault(baseName => IsSelectedBase(request, baseName));
        if (parent is null)
            text.AppendLine("    JS::RootedObject parent(cx);");
        else
            text.Append("    JS::RootedObject parent(cx, getPrototype<").Append(parent).AppendLine(">(cx));");
        text.Append("    JS::RootedObject prototype(cx, JS_InitClass(cx, module, parent, &").Append(id).Append("_class, ")
            .Append(id).Append("_constructor, ").Append(binding.Constructors.Count > 0 ? binding.Constructors.Min(x => MinArgs(x.ParameterTypes, x.DefaultValues)) : 0)
            .Append(", ").Append(id).Append("_properties, ").Append(id).Append("_methods, nullptr, ").Append(id).AppendLine("_statics));");
        text.AppendLine("    if (!prototype) return false;");
        text.Append("    if (!registerType<").Append(binding.QualifiedName);
        if (parent is not null) text.Append(", ").Append(parent);
        text.Append(">(cx, prototype, &").Append(id).AppendLine("_class)) return false;");
        if (!binding.IsAbstract)
        {
            // C++ traits also cover implicit public default constructors while
            // rejecting protected, deleted and non-default construction. The
            // internal allocator supports .extend() before the user's ctor.
            text.Append("    if constexpr (std::is_default_constructible_v<").Append(binding.QualifiedName)
                .Append("> && std::is_destructible_v<").Append(binding.QualifiedName).AppendLine(">) {");
            text.Append("        JS::RootedObject constructor(cx, getClassObject<").Append(binding.QualifiedName).AppendLine(">(cx));");
            text.Append("        if (!constructor || !JS_DefineFunction(cx, constructor, \"__allocate\", ").Append(id)
                .AppendLine("_allocate, 0, 0)) return false;");
            text.AppendLine("    }");
        }
        foreach (var enumeration in enums.Where(x => x.OwnerQualifiedName == binding.QualifiedName))
        {
            text.AppendLine("    {");
            text.Append("        JS::RootedObject constructor(cx, getClassObject<").Append(binding.QualifiedName).AppendLine(">(cx));");
            text.AppendLine("        if (!constructor) return false;");
            EmitEnum(text, enumeration, "constructor", "        ");
            text.AppendLine("    }");
        }
        text.AppendLine("    return true;"); text.AppendLine("}"); text.AppendLine();
    }

    private static bool IsSelectedBase(GenerationRequest request, string baseName)
    {
        var separator = baseName.LastIndexOf("::", StringComparison.Ordinal);
        var name = separator < 0 ? baseName : baseName[(separator + 2)..];
        var cppNamespace = separator < 0 ? "" : baseName[..separator];
        return request.NativeNamespaces.Contains(cppNamespace, StringComparer.Ordinal) &&
               request.ClassPatterns.Any(pattern => PatternMatches(name, pattern)) ||
               request.BindingClassSelections.Any(selection => selection.NativeNamespaces.Contains(cppNamespace, StringComparer.Ordinal) &&
                   selection.ClassPatterns.Any(pattern => PatternMatches(name, pattern)));
    }

    private static void EmitFunctions(StringBuilder text, string name, IEnumerable<IGrouping<string, BindingMethod>> groups, string prefix)
    {
        text.Append("static const JSFunctionSpec ").Append(name).AppendLine("[] = {");
        foreach (var group in groups)
            text.Append("    JS_FN(\"").Append(Escape(group.Key)).Append("\", ").Append(prefix).Append(Id(group.Key)).Append(", ")
                .Append(group.Min(x => MinArgs(x.ParameterTypes, x.DefaultValues))).AppendLine(", JSPROP_ENUMERATE),");
        text.AppendLine("    JS_FS_END"); text.AppendLine("};");
    }

    private static int MinArgs(IReadOnlyList<string> types, IReadOnlyList<string?> defaults)
    {
        var min = types.Count;
        while (min > 0 && defaults.Count >= min && defaults[min - 1] is not null) --min;
        return min;
    }

    private static string Pointer(BindingMethod method) => "static_cast<" + method.ReturnType +
        (method.IsStatic ? " (*)" : " (" + method.DeclaringType + "::*)") + "(" + string.Join(", ", method.ParameterTypes) + ")" +
        (!method.IsStatic && method.IsConst ? " const" : "") + ">(&" + method.DeclaringType + "::" + method.NativeName + ")";

    private static bool BooleanType(string type) => Regex.Replace(type, @"\bconst\b|\bvolatile\b|&", "").Trim() == "bool";
    private static bool HasBoolean(IReadOnlyList<string> types, int count) => types.Take(count).Any(BooleanType);
    private static string BooleanGuard(IReadOnlyList<string> types, int count) =>
        string.Join(" && ", Enumerable.Range(0, count).Where(i => BooleanType(types[i])).Select(i => $"args[{i}].isBoolean()"));
    private static bool PointReturn(BindingClass binding, BindingMethod method) =>
        !method.IsStatic && method.LuaName == "getPosition" &&
        (binding.QualifiedName == "ax::Node" || binding.Bases.Contains("ax::Node", StringComparer.Ordinal)) &&
        Regex.Replace(method.ReturnType, @"\bconst\b|\bvolatile\b|&", "").Trim() == "ax::Vec2";

    private static void EmitMethod(StringBuilder text, BindingClass binding, IReadOnlyList<BindingMethod> methods, string name)
    {
        text.Append("static bool ").Append(name).AppendLine("(JSContext* cx, unsigned argc, JS::Value* vp)");
        text.AppendLine("{"); text.AppendLine("    auto args = JS::CallArgsFromVp(argc, vp);");
        // A numeric argument may select an enum/number overload while a real
        // JS boolean selects its boolean overload. If neither strict candidate
        // matches, keep the original Cocos JS truthiness conversion for bool.
        var orderedMethods = methods[0].LuaName == "setPosition" && methods.Count > 1
            ? methods.OrderByDescending(method => method.ParameterTypes.Count == 2).ToArray()
            : methods;
        var strictFirst = orderedMethods.Count > 1 && orderedMethods.Any(method => HasBoolean(method.ParameterTypes, method.ParameterTypes.Count));
        for (var pass = 0; pass < (strictFirst ? 2 : 1); ++pass)
        foreach (var method in orderedMethods)
        {
            if (pass == 1 && !HasBoolean(method.ParameterTypes, method.ParameterTypes.Count)) continue;
            var strictBoolean = strictFirst && pass == 0;
            var inheritSubclass = !method.IsStatic && method.NativeName is "clone" or "reverse";
            var pointReturn = PointReturn(binding, method);
            text.Append("    if (");
            if (strictBoolean && HasBoolean(method.ParameterTypes, method.ParameterTypes.Count))
                text.Append("args.length() == ").Append(method.ParameterTypes.Count).Append(" && ")
                    .Append(BooleanGuard(method.ParameterTypes, method.ParameterTypes.Count)).Append(" && ");
            text.Append(pointReturn ? "invokePoint<" : "invoke<").Append(Pointer(method)).Append(">(cx, args)) return ")
                .AppendLine(inheritSubclass ? "inheritNativeSubclass(cx, args.thisv(), args.rval());" : "true;");
            text.AppendLine("    if (JS_IsExceptionPending(cx)) return false;");
            var min = MinArgs(method.ParameterTypes, method.DefaultValues);
            for (var count = method.ParameterTypes.Count - 1; count >= min; --count)
            {
                if (pass == 1 && !HasBoolean(method.ParameterTypes, count)) continue;
                text.Append("    if (args.length() == ").Append(count);
                if (strictBoolean && HasBoolean(method.ParameterTypes, count))
                    text.Append(" && ").Append(BooleanGuard(method.ParameterTypes, count));
                text.AppendLine(") {");
                Arguments(text, method.ParameterTypes, count, "        ");
                var callback = method.ParameterTypes.Take(count).Any(type => type.Contains("std::function", StringComparison.Ordinal));
                if (callback)
                    text.AppendLine("        JS::RootedObject receiver(cx, args.thisv().isObject() ? &args.thisv().toObject() : engine(cx)->getGlobalObject());");
                text.Append("        if (").Append(ReadArguments(count, callback ? "receiver" : null)).AppendLine(") {");
                if (!method.IsStatic)
                {
                    text.Append("            ").Append(method.DeclaringType).AppendLine("* self = nullptr;");
                    text.Append("            if (!toNative<").Append(method.DeclaringType).AppendLine(">(cx, args.thisv(), &self) || !self) {");
                    text.AppendLine("                if (!JS_IsExceptionPending(cx)) JS_ReportError(cx, \"Invalid native receiver\");");
                    text.AppendLine("                return false;");
                    text.AppendLine("            }");
                }
                var values = Enumerable.Range(0, method.ParameterTypes.Count)
                    .Select(i => i < count ? $"arg{i}.get()" : method.DefaultValues[i]!);
                var call = method.IsStatic ? "(" + Pointer(method) + ")" : "(self->*" + Pointer(method) + ")";
                EmitReturn(text, method.ReturnType, call + "(" + string.Join(", ", values) + ")", "            ", inheritSubclass, pointReturn);
                text.AppendLine("        }"); text.AppendLine("        if (JS_IsExceptionPending(cx)) return false;"); text.AppendLine("    }");
            }
        }
        Error(text, binding.LuaName + "." + methods[0].LuaName);
        text.AppendLine("}");
    }

    private static void Arguments(StringBuilder text, IReadOnlyList<string> types, int count, string indent)
    {
        for (var i = 0; i < count; ++i)
            text.Append(indent).Append("Argument<").Append(types[i]).Append("> arg").Append(i).AppendLine(";");
    }
    private static string ReadArguments(int count, string? receiver = null) => count == 0 ? "true" :
        string.Join(" && ", Enumerable.Range(0, count).Select(i => $"arg{i}.read(cx, args[{i}]{(receiver is null ? "" : ", " + receiver)})"));

    private static string ConstructorDefault(string type, string value)
    {
        if ((value.Trim() is "NULL" or "0") && type.TrimEnd().EndsWith('*'))
            return $"static_cast<{type}>(nullptr)";
        return value;
    }
    private static void EmitReturn(StringBuilder text, string type, string call, string indent, bool inheritSubclass = false, bool pointReturn = false)
    {
        if (type == "void")
        {
            text.Append(indent).Append(call).AppendLine(";");
            text.Append(indent).AppendLine("args.rval().setUndefined(); return true;");
        }
        else if (pointReturn)
        {
            text.Append(indent).Append("decltype(auto) result = ").Append(call).AppendLine(";");
            text.Append(indent).AppendLine("return toJsPoint(cx, result, args.rval());");
        }
        else
        {
            text.Append(indent).Append("decltype(auto) result = ").Append(call).AppendLine(";");
            if (inheritSubclass)
            {
                text.Append(indent).AppendLine("if (!toJsValueWithOwner(cx, result, args.rval(), args.thisv())) return false;");
                text.Append(indent).AppendLine("return inheritNativeSubclass(cx, args.thisv(), args.rval());");
            }
            else text.Append(indent).AppendLine("return toJsValueWithOwner(cx, result, args.rval(), args.thisv());");
        }
    }

    private static void EmitConstructor(StringBuilder text, BindingClass binding, string name)
    {
        text.Append("static bool ").Append(name).AppendLine("(JSContext* cx, unsigned argc, JS::Value* vp)");
        text.AppendLine("{"); text.AppendLine("    auto args = JS::CallArgsFromVp(argc, vp);");
        var strictFirst = binding.Constructors.Count > 1 && binding.Constructors.Any(constructor => HasBoolean(constructor.ParameterTypes, constructor.ParameterTypes.Count));
        for (var pass = 0; pass < (strictFirst ? 2 : 1); ++pass)
        foreach (var constructor in binding.Constructors)
        {
            for (var count = constructor.ParameterTypes.Count; count >= MinArgs(constructor.ParameterTypes, constructor.DefaultValues); --count)
            {
                if (pass == 1 && !HasBoolean(constructor.ParameterTypes, count)) continue;
                text.Append("    if (args.length() == ").Append(count);
                if (strictFirst && pass == 0 && HasBoolean(constructor.ParameterTypes, count))
                    text.Append(" && ").Append(BooleanGuard(constructor.ParameterTypes, count));
                text.AppendLine(") {");
                Arguments(text, constructor.ParameterTypes, count, "        ");
                text.Append("        if (").Append(ReadArguments(count)).Append(") return construct<").Append(binding.QualifiedName).Append(">(cx, args");
                var values = Enumerable.Range(0, constructor.ParameterTypes.Count)
                    .Select(i => i < count ? $"arg{i}.get()" : ConstructorDefault(constructor.ParameterTypes[i], constructor.DefaultValues[i]!));
                foreach (var value in values) text.Append(", ").Append(value);
                text.AppendLine(");");
                text.AppendLine("        if (JS_IsExceptionPending(cx)) return false;"); text.AppendLine("    }");
            }
        }
        Error(text, binding.LuaName + ".constructor"); text.AppendLine("}");
    }

    private static void Error(StringBuilder text, string name)
    {
        text.Append("    JS_ReportError(cx, \"%s: no overload matches the supplied arguments\", \"").Append(Escape(name)).AppendLine("\");");
        text.AppendLine("    return false;");
    }

    private static void EmitField(StringBuilder text, BindingClass binding, BindingField field, string name)
    {
        var callback = field.FieldType.Contains("std::function", StringComparison.Ordinal);
        foreach (var setter in new[] { false, true })
        {
            text.Append("static bool ").Append(name).Append(setter ? "_set" : "_get").AppendLine("(JSContext* cx, unsigned argc, JS::Value* vp)");
            text.AppendLine("{"); text.AppendLine("    auto args = JS::CallArgsFromVp(argc, vp);");
            text.Append("    ").Append(binding.QualifiedName).AppendLine("* self = nullptr;");
            text.Append("    if (!toNative<").Append(binding.QualifiedName).AppendLine(">(cx, args.thisv(), &self) || !self) {");
            text.AppendLine("        if (!JS_IsExceptionPending(cx)) JS_ReportError(cx, \"Invalid native receiver\");");
            text.AppendLine("        return false;");
            text.AppendLine("    }");
            if (callback) text.AppendLine("    JS::RootedObject wrapper(cx, &args.thisv().toObject());");
            if (setter)
            {
                text.Append("    Argument<").Append(field.FieldType).AppendLine("> value;");
                text.Append("    if (args.length() != 1 || !value.read(cx, args[0]").Append(callback ? ", wrapper" : "").AppendLine(")) {");
                text.AppendLine("        if (!JS_IsExceptionPending(cx)) JS_ReportError(cx, \"Invalid property assignment\");");
                text.AppendLine("        return false;"); text.AppendLine("    }");
                if (callback)
                    text.Append("    if (!JS_DefineProperty(cx, wrapper, \"__axjs_callback_").Append(Escape(field.NativeName)).AppendLine("\", args[0], 0)) return false;");
                text.Append("    self->").Append(field.NativeName).AppendLine(" = value.get();");
                text.AppendLine("    args.rval().setUndefined(); return true;");
            }
            else if (callback)
                text.Append("    return JS_GetProperty(cx, wrapper, \"__axjs_callback_").Append(Escape(field.NativeName)).AppendLine("\", args.rval());");
            else text.Append("    return toJsValueWithOwner(cx, self->").Append(field.NativeName).AppendLine(", args.rval(), args.thisv());");
            text.AppendLine("}");
        }
    }

    private static void EmitEnum(StringBuilder text, BindingEnum enumeration, string owner, string indent)
    {
        text.Append(indent).AppendLine("JS::RootedObject values(cx, JS_NewObject(cx, nullptr, JS::NullPtr(), JS::NullPtr()));");
        text.Append(indent).AppendLine("if (!values) return false;");
        text.Append(indent).AppendLine("JS::RootedValue constant(cx);");
        foreach (var value in enumeration.Values)
        {
            var number = value.IsUnsigned ? value.UnsignedValue.ToString(System.Globalization.CultureInfo.InvariantCulture) :
                value.SignedValue.ToString(System.Globalization.CultureInfo.InvariantCulture);
            text.Append(indent).Append("constant.setNumber(static_cast<double>(").Append(number).Append(value.IsUnsigned ? "ULL" : "LL").AppendLine("));");
            text.Append(indent).Append("if (!JS_DefineProperty(cx, values, \"").Append(Escape(value.Name))
                .AppendLine("\", constant, JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)) return false;");
        }
        text.Append(indent).Append("JS::RootedValue value(cx, JS::ObjectValue(*values));").AppendLine();
        text.Append(indent).Append("if (!JS_DefineProperty(cx, ").Append(owner).Append(", \"").Append(Escape(enumeration.NativeName))
            .AppendLine("\", value, JSPROP_ENUMERATE | JSPROP_READONLY | JSPROP_PERMANENT)) return false;");
    }

    private static void OpenGuard(StringBuilder text, GenerationRequest request)
    {
        if (!string.IsNullOrWhiteSpace(request.ConditionalExpression)) text.Append("#if ").AppendLine(request.ConditionalExpression);
    }
    private static void CloseGuard(StringBuilder text, GenerationRequest request)
    {
        if (!string.IsNullOrWhiteSpace(request.ConditionalExpression)) text.AppendLine("#endif");
    }

    public static string EmitManifest(GenerationRequest request, IReadOnlyList<BindingClass> classes,
        IReadOnlyList<BindingEnum> enums, IReadOnlyList<BindingClass> collected)
    {
        var manifest = new
        {
            formatVersion = 1, language = "js", engine = "SpiderMonkey 33.1", module = request.Module,
            registrationName = Registration(request), targetNamespace = request.LuaNamespace,
            conditionalExpression = request.ConditionalExpression,
            classes = classes.Select(binding => new
            {
                nativeName = binding.NativeName, qualifiedName = binding.QualifiedName, jsName = binding.LuaClassName,
                isAbstract = binding.IsAbstract, constructors = binding.Constructors, bases = binding.Bases,
                methods = binding.Methods.Select(method => new
                {
                    nativeName = method.NativeName, jsName = method.LuaName, declaringType = method.DeclaringType,
                    returnType = method.ReturnType, parameterTypes = method.ParameterTypes,
                    defaultValues = method.DefaultValues, isStatic = method.IsStatic, isConst = method.IsConst
                }),
                fields = binding.Fields.Select(field => new { nativeName = field.NativeName, jsName = field.LuaName, fieldType = field.FieldType }),
                requiresManualAdapter = collected.First(x => x.QualifiedName == binding.QualifiedName).Methods
                    .Except(binding.Methods).Select(method => new { nativeName = method.NativeName, returnType = method.ReturnType, parameterTypes = method.ParameterTypes })
            }),
            enums
        };
        return JsonSerializer.Serialize(manifest, new JsonSerializerOptions(JsonSerializerDefaults.Web) { WriteIndented = true }) + "\n";
    }

    public static void WriteAggregate(string outputDirectory, IReadOnlyList<GenerationResult> results)
    {
        var header = new StringBuilder(); Preamble(header);
        header.AppendLine("#pragma once\n#include \"js-bindings/runtime/JsRuntime.h\"\nnamespace axjs {\nbool registerAllGenerated(JSContext* cx, JS::HandleObject global);\n}");
        var cpp = new StringBuilder(); Preamble(cpp);
        cpp.AppendLine("#include \"axjs_register_gen.h\"");
        foreach (var result in results) cpp.Append("#include \"axjs_").Append(result.Module).AppendLine("_gen.h\"");
        cpp.AppendLine("namespace axjs {\nbool registerAllGenerated(JSContext* cx, JS::HandleObject global)\n{");
        var preferred = new[] { "base", "ui", "audio", "network", "extension", "sceneext", "sceneio", "spine", "assetsmanager" };
        foreach (var result in results.OrderBy(result => Array.IndexOf(preferred, result.Module) is var index && index >= 0 ? index : int.MaxValue))
            cpp.Append("    if (!register_all_ax_").Append(result.Module).AppendLine("(cx, global)) return false;");
        cpp.AppendLine("    return true;\n}\n}");
        Directory.CreateDirectory(outputDirectory);
        AtomicWrite(Path.Combine(outputDirectory, "axjs_register_gen.h"), header.ToString());
        AtomicWrite(Path.Combine(outputDirectory, "axjs_register_gen.cpp"), cpp.ToString());
    }

    private static void AtomicWrite(string path, string text)
    {
        var temporary = path + ".tmp";
        File.WriteAllText(temporary, text, new UTF8Encoding(false));
        File.Move(temporary, path, true);
    }
}
