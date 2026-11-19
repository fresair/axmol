# Spine 4.3 overlay for the original v3 native tests

The original files remain unchanged in `legacy/js-tests/res/spine`. These JSON
exports come from the same official Spineboy and Goblins example projects at
[spine-runtimes commit 1ad4e94bc3f16f4908a87a97d31e0de657f1788a](https://github.com/EsotericSoftware/spine-runtimes/tree/1ad4e94bc3f16f4908a87a97d31e0de657f1788a/examples).
They were exported by `4.3.75-beta`, matching the actual 4.3 parser used by Axmol.
They are official re-exports of those example projects, rather than a mechanical
rewriting of the old JSON; animation samples may differ across the example
project revisions. All seven original Spineboy animation names, Goblin `walk`,
and all original skins remain available.

Both `.atlas` and `.png` pairs are exact copies of the original imported test
resources. This preserves the original artwork and every old atlas region,
including the old Spineboy dust regions and the original region spelling.
The actual Axmol native parser successfully resolves the 4.3 attachments against
these atlas files.

`manifest.json` records pinned upstream and original sources, SHA256 digests,
animation/skin/atlas inventories, and every current runtime source/header.
The application prepends `Content/legacy-compat` to the original test resource
search paths so unchanged requests such as `spine/spineboy-ess.json` load this
overlay. Carry the three `LICENSE_*.txt` files with these resources. Their example
image license permits redistribution with the notice and excludes commercial
use of the example artwork.
