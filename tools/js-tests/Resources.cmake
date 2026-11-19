# Copyright (c) 2026 Ye Liangqi (axmol-js contributions and modifications).
# SPDX-License-Identifier: MIT
function(ax_js_test_prepare_resources engine_root app_root)
  option(AX_JS_TEST_RESOURCE_OFFLINE "Require cached pinned JS test resources" ${AX_JS_SPIDERMONKEY_OFFLINE})
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_js_resource_options)
  if(AX_JS_TEST_RESOURCE_OFFLINE)
    list(APPEND _js_resource_options --offline)
  endif()
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${engine_root}/tools/js-tests/prepare-resources.py"
      --app-root "${app_root}" ${_js_resource_options}
    RESULT_VARIABLE _js_resource_result
    OUTPUT_VARIABLE _js_resource_output
    ERROR_VARIABLE _js_resource_error)
  if(NOT _js_resource_result EQUAL 0)
    message(FATAL_ERROR "Unable to prepare pinned JS test resources:\n${_js_resource_error}")
  endif()
endfunction()
