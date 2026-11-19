# JavaScript manual adapter policy

Automatic bindings use current Axmol declarations and the shared runtime for
checked conversion, native identity, and rooted callback lifetime. A manual
adapter is justified by behavior outside that declaration model:

- Cocos2d-x v3 constructor spellings, inheritance/`_super`, property aliases,
  lifecycle dispatch, and scheduling semantics.
- Temporarily borrowed input events and owner/track-scoped Spine records.
- Ownership-sensitive factories, singleton lifetime, and native records that
  need explicit copies or conversion rather than an erased pointer.
- Browser-style XHR/WebSocket, persistence, and platform reflection protocols.
- Legacy scene parsers, `{node, action}` results, exported callbacks, and asset
  update/search-path/restart behavior.

Place these implementations under `extensions/scripting/js-bindings/manual`.
The directory policy is recorded in `manual-adapters.json`. Do not copy an old
SpiderMonkey proxy table into a generated module or add roots independently of
`JsRuntime`; wrapper and callback invalidation must remain centralized.
Unsupported automatic signatures are reported in the module API manifest.
Adding a manual API must include a compatibility test of its visible behavior;
it must not appear as a no-op function merely to fill an old namespace.
