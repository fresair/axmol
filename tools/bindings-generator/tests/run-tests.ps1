# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
# Uses the same managed collector as genbindings without SDK/platform setup.
param(
    [Parameter(Mandatory)] [string]$LibClangRoot,
    [string]$GeneratorAssembly,
    [string]$InteropAssembly
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$generatorRoot = Join-Path $repositoryRoot 'tools/bindings-generator'
if (-not $GeneratorAssembly) { $GeneratorAssembly = Join-Path $generatorRoot 'bin/Release/net8.0/Axmol.LuaBindings.Generator.dll' }
if (-not $InteropAssembly) { $InteropAssembly = Join-Path (Split-Path $GeneratorAssembly -Parent) 'ClangSharp.Interop.dll' }
Add-Type -Path $InteropAssembly
Add-Type -Path $GeneratorAssembly
$libraryName = if ($IsMacOS) { 'libclang.dylib' } elseif ($IsWindows) { 'libclang.dll' } else { 'libclang.so' }
[Axmol.LuaBindings.BindingGenerator]::ConfigureLibClang((Join-Path $LibClangRoot $libraryName))
$fixturePath = Join-Path $PSScriptRoot 'fixture-entry.h'
$outputRoot = Join-Path ([IO.Path]::GetTempPath()) "axmol-binding-generator-tests-$PID"
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
try {
    foreach ($language in @('lua', 'js')) {
        $output = Join-Path $outputRoot $language
        $request = [Axmol.LuaBindings.GenerationRequest]::new()
        $request.RepositoryRoot = $repositoryRoot
        $request.OutputDirectory = $output
        $request.Module = 'fixture'
        $request.Language = $language
        $request.Headers = [string[]]@($fixturePath)
        $request.ClangArguments = [string[]]@('-x', 'c++', '-std=c++23', '-isystem', $PSScriptRoot)
        $request.NativeNamespaces = [string[]]@('ax')
        $request.ClassPatterns = [string[]]@('GeneratorBase', 'GeneratorNode')
        $request.LuaNamespace = if ($language -eq 'js') { 'cc' } else { 'ax' }
        $request.LuaTypeNamespace = $request.LuaNamespace
        $fields = [Axmol.LuaBindings.BindingFieldRule]::new()
        $fields.ClassPattern = 'GeneratorNode'
        $fields.FieldPatterns = [string[]]@('count', 'callback')
        $request.FieldRules = [Axmol.LuaBindings.BindingFieldRule[]]@($fields)
        $result = [Axmol.LuaBindings.BindingGenerator]::Generate($request)
        Assert (@($result.Diagnostics | Where-Object Severity -eq 'error').Count -eq 0) "${language}: fixture generation had errors"
        Assert ($result.Classes.Count -eq 2) "${language}: class collection is incomplete"
        Assert ($result.Enums.Count -eq 1) "${language}: nested enum was not collected"
        $before = @{}
        foreach ($file in $result.GeneratedFiles) { $before[$file] = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash }
        [void][Axmol.LuaBindings.BindingGenerator]::Generate($request)
        foreach ($file in $result.GeneratedFiles) { Assert ($before[$file] -eq (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash) "${language}: output is not deterministic: $file" }
        $sources = (Get-ChildItem $output -Filter '*.cpp' | Get-Content -Raw) -join "`n"
        Assert (-not $sources.Contains('manualOnly')) "${language}: script-ignore native helper was exported automatically"
        if ($language -eq 'js') {
            Assert ($sources.Contains('static_cast<void (ax::GeneratorNode::*)(float, float)>')) 'JS overload did not retain the exact native signature'
            Assert ($sources.Contains('args.length() == 1')) 'JS default argument candidate was not emitted'
            Assert ($sources.Contains('construct<ax::GeneratorNode>')) 'JS native constructor was not emitted'
            Assert ($sources.Contains('JS_PSGS("count"')) 'JS public field accessors were not emitted'
            Assert ($sources.Contains('registerType<ax::GeneratorNode, ax::GeneratorBase>')) 'JS native type inheritance registration was not emitted'
            Assert ($sources.Contains('JS::RootedObject constructor')) 'JS owner enum was not attached to its class'
            Assert ($sources.Contains('std::function<void (int)>')) 'JS callback typedef was not expanded for runtime conversion'
            Assert ($sources.Contains('ax::rhi::GeneratorFormat::VALUE')) 'JS namespace-relative default value was not qualified'
            Assert ($sources.Contains('static_cast<void (ax::GeneratorNode::*)(ax::rhi::GeneratorFormat)>(&ax::GeneratorNode::aliasFormat)')) 'JS enum alias expansion lost its namespace qualification'
            Assert ($sources.Contains('static_cast<void (ax::GeneratorNode::*)(const char *)>')) 'JS C string signature was not retained for the runtime owning argument storage'
            Assert ($sources.Contains('__axjs_callback_callback')) 'JS callback field does not preserve the assigned function'
            Assert ($sources.Contains('static_cast<const char *>(nullptr)')) 'JS constructor pointer default deduces an integer instead of a pointer'
            Assert ($sources.Contains('arg0.read(cx, args[0], receiver)')) 'JS shortened callback overload loses the native wrapper receiver'
            Assert ($sources.Contains('value.read(cx, args[0], wrapper)')) 'JS callback field assignment loses the native wrapper receiver'
            Assert ($sources.Contains('&ax::GeneratorNode::extensionBase')) 'JS deprecated extension namespace alias was not normalized to the native type'
            Assert ($sources.Contains('toJsValueWithOwner(cx, result, args.rval(), args.thisv())')) 'JS shortened native getter loses the receiver lifetime token'
            Assert ($sources.Contains('toJsValueWithOwner(cx, self->count, args.rval(), args.thisv())')) 'JS native field getter loses the receiver lifetime token'
            Assert ($sources.Contains('allocateIfDefaultConstructible<ax::GeneratorNode>')) 'JS default native allocator was not emitted for extend'
            Assert ($sources.Contains('if constexpr (std::is_default_constructible_v<T> && std::is_destructible_v<T>)')) 'JS allocator instantiates protected or unavailable default constructors outside a template'
            Assert ($sources.Contains('std::is_default_constructible_v<ax::GeneratorNode>')) 'JS allocator ignores public/default constructor accessibility'
            Assert ($sources.Contains('"__allocate"')) 'JS native allocator was not registered'
            Assert ($sources.Contains('return inheritNativeSubclass(cx, args.thisv(), args.rval());')) 'JS clone/reverse loses the same native type subclass prototype'
            $booleanPointer = 'invoke<static_cast<void (ax::GeneratorNode::*)(bool)>(&ax::GeneratorNode::selection)>(cx, args)'
            Assert ($sources.Contains('args.length() == 1 && args[0].isBoolean() && ' + $booleanPointer)) 'JS boolean overload consumes numeric enum arguments before a strict candidate can match'
            Assert ($sources.Contains('if (' + $booleanPointer + ') return true;')) 'JS boolean overload omits original truthiness fallback'
            Assert ($sources.Contains('&ax::GeneratorNode::consumeSpan')) 'JS collector still omits owning span parameters'
            Assert ($sources.Contains('&ax::GeneratorNode::inspectSpan')) 'JS const span reference loses its owning argument adapter'
            Assert ($sources.Contains('&ax::GeneratorNode::getSpan')) 'JS span return is not emitted for immediate array copying'
            Assert (-not $sources.Contains('&ax::GeneratorNode::mutateSpan')) 'JS mutable span reference lacks safe owning argument storage'
            Assert (-not $sources.Contains('&ax::GeneratorNode::volatileSpan')) 'JS volatile span reference was emitted without an adapter'
            Assert (-not $sources.Contains('&ax::GeneratorNode::consumeStringViews')) 'JS span of string views lacks owned backing bytes'
            $manifest = Get-Content (Join-Path $output 'manifests/axjs_fixture.json') -Raw | ConvertFrom-Json
            Assert ($manifest.language -eq 'js') 'JS API manifest has no language identifier'
        }
        else {
            Assert ($sources.Contains('module.class_<ax::GeneratorNode>')) 'Default Lua generation changed backend'
            Assert (-not $sources.Contains('JSContext')) 'Lua output unexpectedly contains JS API'
            Assert (-not $sources.Contains('Span')) 'Lua generation unexpectedly enables unsupported span conversions'
        }
    }
    Write-Host 'PASS: shared collector, Lua default, JS overload/default/constructor/field/enum/callback emission, deterministic regeneration'
}
finally {
    if (Test-Path $outputRoot) { Remove-Item $outputRoot -Force -Recurse }
}
