# Contributing to axmol-js

Send native JavaScript changes to the `axmol-js` branch of `fresair/axmol`. This includes the SpiderMonkey runtime, bindings, project template, debugger and Cocos2d-x v3 compatibility tests. For changes intended for the upstream engine, follow the upstream branch and contributor rules. [Fork pull requests](https://github.com/fresair/axmol/pulls) · [Upstream Axmol contribution guide](https://github.com/axmolengine/axmol/blob/dev/CONTRIBUTING.md)

## Describe the change

Include the commit or branch you tested, the smallest reproduction, the expected result and what you observed. For a device-specific issue, include the platform, architecture and build configuration. For a regression in the original v3 test app, give the menu category and scene name so a reviewer can open the same scene. General engine questions and bugs reproducible on upstream Axmol can use the upstream community channels. [Upstream FAQ](https://github.com/axmolengine/axmol/wiki/FAQ) · [Upstream discussions](https://github.com/axmolengine/axmol/discussions) · [Upstream issues](https://github.com/axmolengine/axmol/issues)

## Submit a pull request

1. Create a fork or personal copy that you can push to. In your clone, use `origin` for your copy and add the repository that maintains the JS branch as `upstream`.

   ```sh
   git remote add upstream https://github.com/fresair/axmol.git
   git fetch upstream axmol-js
   git switch -c my-js-change upstream/axmol-js
   ```

2. Make a focused change and run the checks for the affected code. Use the runtime regressions for VM, bindings or callback changes. For graphics or input, open the affected scene in the test app and report what you saw on the target platform. [Runtime tests](tests/js-runtime-tests/README.md) · [Original v3 test app](tests/js-tests/README.md) · [Binding generation](tools/js-bindings/README.md)

3. Push your topic branch to your copy and open a pull request. In GitHub, set the **base repository** to `fresair/axmol` and the **base branch** to `axmol-js`. Describe which checks you ran and any platforms you could not test.

   ```sh
   git push -u origin my-js-change
   ```

## Review checklist

-   For C++ code follow the [axmol C++ Coding Style](docs/CODING_STYLE.md)
-   Describe what the patch does and why.
-   Include regression or unit tests when applicable.
-   Run the relevant C++23 build, test, and formatting checks using the Axmol CLI or the documented CMake workflow.
-   List the platforms and compilers you tested.
-   Must not degrade performance, break existing tests, break CI, or introduce avoidable backward incompatibility.
-   Must compile without warnings in the configurations tested.
-   New APIs must be easy to use, familiar to Axmol users, documented with Doxygen, and easy to extend and maintain.
-   Tools must include a README.md describing how to use them.
-   Keep implementations efficient and avoid duplicating existing code unless the new code deliberately deprecates or replaces it.
-   Large refactors of key components should be coordinated with maintainers and are generally reserved for the next major version.

## Copyright and attribution

For contributions to this fork, preserve applicable upstream and third-party notices and use the appropriate copyright and SPDX notice for new project-owned files. The Ye Liangqi notice identifies the original work and modifications that Ye Liangqi contributed to axmol-js. See the project and component licenses when reusing code, fixtures or artwork. [License](LICENSE) · [Authors](AUTHORS.md)

### Contributions sent upstream

Upstream Axmol maintains a contributor agreement and a copyright-assignment process for pull requests submitted to `axmolengine/axmol`. Its agreement assigns economic copyright rights for incoming contributions to Simdsoft Limited. Follow the upstream project's current CLA and source-header guidance when submitting a change there. Historical code from contributors who did not assign those rights retains its original MIT notices. [Upstream contribution and copyright policy](https://github.com/axmolengine/axmol/blob/dev/CONTRIBUTING.md)
