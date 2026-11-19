# Contributing to axmol-js

Open pull requests for the JavaScript runtime, bindings, debugger, templates and v3 compatibility tests against **`fresair/axmol-js:axmol-js`**. [Pull requests](https://github.com/fresair/axmol-js/pulls)

## Submit a change

1. Create a personal fork or copy and clone it as `origin`. Add this repository as `upstream`, then start a topic branch from `axmol-js`.

   ```sh
   git remote add upstream https://github.com/fresair/axmol-js.git
   git fetch upstream axmol-js
   git switch --no-track -c my-js-change upstream/axmol-js
   ```

2. Make the change and run the checks for the affected code. For VM, object lifetime and callback changes, use the runtime suite. For rendering or input, open the relevant category and scene in the v3 test app. Binding generator changes should also cover the generated output. [Runtime tests](tests/js-runtime-tests/README.md) · [Interactive test app](tests/js-tests/README.md) · [Binding generation](tools/js-bindings/README.md) · [C++ coding style](docs/CODING_STYLE.md)

3. Commit your changes and run `git push -u origin my-js-change`. When opening the pull request, check the base repository and branch shown above. Describe the problem, how to reproduce it and what changed. Include the commands or scene you tested, the platform and architecture, the build configuration, and any relevant coverage gaps.

## Attribution and upstream contributions

Keep applicable copyright, attribution and license notices with reused code and assets. Use accurate authorship and the applicable SPDX identifier for new project-owned source files. The Ye Liangqi notice covers Ye Liangqi's original contributions and modifications to axmol-js. [License](LICENSE) · [Authors](AUTHORS.md)

If you also submit a change to `axmolengine/axmol`, follow that project's target-branch, source-header and contributor agreement requirements. [Upstream contribution guide](https://github.com/axmolengine/axmol/blob/dev/CONTRIBUTING.md)
