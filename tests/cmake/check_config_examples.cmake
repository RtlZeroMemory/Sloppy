set(config_basic_app "${PROJECT_SOURCE_DIR}/examples/config-basic/app.js")
set(config_basic_readme "${PROJECT_SOURCE_DIR}/examples/config-basic/README.md")
set(config_secret_app "${PROJECT_SOURCE_DIR}/examples/config-secrets-redaction/app.js")
set(config_secret_readme "${PROJECT_SOURCE_DIR}/examples/config-secrets-redaction/README.md")
set(config_strict_app "${PROJECT_SOURCE_DIR}/examples/config-strict-mode/app.js")
set(config_strict_readme "${PROJECT_SOURCE_DIR}/examples/config-strict-mode/README.md")

foreach(required_file IN ITEMS
        "${config_basic_app}"
        "${config_basic_readme}"
        "${config_secret_app}"
        "${config_secret_readme}"
        "${config_strict_app}"
        "${config_strict_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing config example file: ${required_file}")
    endif()
endforeach()

file(READ "${config_basic_app}" config_basic_js)
file(READ "${config_secret_app}" config_secret_js)
file(READ "${config_strict_app}" config_strict_js)
file(READ "${config_basic_readme}" config_basic_readme_text)
file(READ "${config_secret_readme}" config_secret_readme_text)
file(READ "${config_strict_readme}" config_strict_readme_text)

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

function(reject_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(NOT found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

require_substring("${config_basic_js}" "app.config.bind(\"Auth\"" "config-basic app is missing bind")
require_substring("${config_basic_js}" "jwtSecret: { type: \"secret\"" "config-basic app is missing secret descriptor")
require_substring("${config_basic_js}" "default: 60" "config-basic app is missing default descriptor")
require_substring("${config_secret_js}" "app.config.getSecret(\"Auth:JwtSecret\")" "secret example is missing getSecret")
require_substring("${config_strict_js}" "app.config.getString(\"Auth:Issuer\")" "strict example is missing required read")
require_substring("${config_basic_readme_text}" "placeholder" "config README must document secret boundary")
require_substring("${config_secret_readme_text}" "Do not replace the" "secret README must document placeholder policy")
require_substring("${config_strict_readme_text}" "Dynamic config keys" "strict README must document dynamic-key boundary")

foreach(example_text IN ITEMS "${config_basic_js}" "${config_secret_js}" "${config_strict_js}")
    reject_substring("${example_text}" "real-secret" "config examples must not include raw secret examples")
    reject_substring("${example_text}" "supersecret" "config examples must not include raw secret examples")
endforeach()
