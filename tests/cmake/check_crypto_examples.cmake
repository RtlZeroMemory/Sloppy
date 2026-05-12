if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()

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

set(random_app "${PROJECT_SOURCE_DIR}/examples/crypto-random-token/app.js")
set(random_readme "${PROJECT_SOURCE_DIR}/examples/crypto-random-token/README.md")
set(hash_app "${PROJECT_SOURCE_DIR}/examples/crypto-hash-hmac/app.js")
set(hash_readme "${PROJECT_SOURCE_DIR}/examples/crypto-hash-hmac/README.md")
set(password_app "${PROJECT_SOURCE_DIR}/examples/crypto-password/app.js")
set(password_readme "${PROJECT_SOURCE_DIR}/examples/crypto-password/README.md")
set(secret_app "${PROJECT_SOURCE_DIR}/examples/crypto-secret-constant-time/app.js")
set(secret_readme "${PROJECT_SOURCE_DIR}/examples/crypto-secret-constant-time/README.md")

foreach(required_file IN ITEMS
        "${random_app}" "${random_readme}"
        "${hash_app}" "${hash_readme}"
        "${password_app}" "${password_readme}"
        "${secret_app}" "${secret_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing Crypto example file: ${required_file}")
    endif()
endforeach()

file(READ "${random_app}" random_source)
file(READ "${random_readme}" random_readme_source)
file(READ "${hash_app}" hash_source)
file(READ "${hash_readme}" hash_readme_source)
file(READ "${password_app}" password_source)
file(READ "${password_readme}" password_readme_source)
file(READ "${secret_app}" secret_source)
file(READ "${secret_readme}" secret_readme_source)

foreach(required_pattern IN ITEMS
        "import { Random } from \"sloppy/crypto\";"
        "Random.uuid()"
        "Random.bytes(32)"
        "Random.token(32)"
        "Random.hex(32)"
        "Random.numericCode(6)")
    require_substring("${random_source}" "${required_pattern}"
                      "examples/crypto-random-token/app.js is missing expected Random API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Hash, Hmac, Secret } from \"sloppy/crypto\";"
        "Secret.fromUtf8("
        "await Hash.sha256Hex(payload)"
        "await Hash.sha256Base64(payload)"
        "Hash.create(\"sha256\")"
        "hasher.update(\"event:\")"
        "await hasher.digest(\"hex\")"
        "await Hmac.sha512(signingKey, payload)"
        "await Hmac.verifySha512(signingKey, payload, signature)"
        "signingKey.dispose()")
    require_substring("${hash_source}" "${required_pattern}"
                      "examples/crypto-hash-hmac/app.js is missing expected Hash/Hmac API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Password } from \"sloppy/crypto\";"
        "await Password.hash(password)"
        "await Password.verify(password, encodedHash)"
        "await Password.needsRehash(encodedHash)")
    require_substring("${password_source}" "${required_pattern}"
                      "examples/crypto-password/app.js is missing expected Password API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { ConstantTime, Hmac, Secret } from \"sloppy/crypto\";"
        "Secret.fromUtf8(configuredKeyText)"
        "await Hmac.sha256(key, body)"
        "ConstantTime.equals(expectedSignature, providedSignature)"
        "ConstantTime.equals(left, right)"
        "key.dispose()")
    require_substring("${secret_source}" "${required_pattern}"
                      "examples/crypto-secret-constant-time/app.js is missing expected Secret API shape")
endforeach()

foreach(source IN ITEMS
        "${random_source}" "${hash_source}" "${password_source}" "${secret_source}")
    foreach(forbidden_pattern IN ITEMS
            "Math.random"
            "crypto.subtle"
            "node:crypto"
            "require(\"crypto\")"
            "from \"crypto\""
            "Bun."
            "npm "
            "yarn "
            "pnpm "
            "console.log"
            "NonCryptoHash")
        reject_substring("${source}" "${forbidden_pattern}"
                         "Crypto examples must keep evidence boundaries and security namespaces clear")
    endforeach()
endforeach()

foreach(readme_source IN ITEMS
        "${random_readme_source}" "${hash_readme_source}" "${password_readme_source}"
        "${secret_readme_source}")
    foreach(forbidden_pattern IN ITEMS
            "crypto.subtle("
            "require(\"crypto\")"
            "from \"crypto\""
            "Bun."
            "npm install"
            "yarn add"
            "pnpm add"
            "console.log"
            "NonCryptoHash.")
        reject_substring("${readme_source}" "${forbidden_pattern}"
                         "Crypto example docs must keep evidence boundaries and security namespaces clear")
    endforeach()
endforeach()

foreach(required_pattern IN ITEMS
        "OS CSPRNG"
        "`Math.random` fallback")
    require_substring("${random_readme_source}" "${required_pattern}"
                      "examples/crypto-random-token/README.md is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "constant-time comparison"
        "does not define")
    require_substring("${hash_readme_source}" "${required_pattern}"
                      "examples/crypto-hash-hmac/README.md is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "async"
        "offloaded from the V8 owner thread"
        "Password hashing is async"
        "does not define a custom")
    require_substring("${password_readme_source}" "${required_pattern}"
                      "examples/crypto-password/README.md is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "best-effort cleanup"
        "avoids printing secret material"
        "side-channel")
    require_substring("${secret_readme_source}" "${required_pattern}"
                      "examples/crypto-secret-constant-time/README.md is missing required boundary text")
endforeach()
