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

set(base_app "${PROJECT_SOURCE_DIR}/examples/codec-base64-hex/app.js")
set(base_readme "${PROJECT_SOURCE_DIR}/examples/codec-base64-hex/README.md")
set(text_app "${PROJECT_SOURCE_DIR}/examples/codec-text-binary/app.js")
set(text_readme "${PROJECT_SOURCE_DIR}/examples/codec-text-binary/README.md")
set(compression_app "${PROJECT_SOURCE_DIR}/examples/codec-compression/app.js")
set(compression_readme "${PROJECT_SOURCE_DIR}/examples/codec-compression/README.md")
set(stream_app "${PROJECT_SOURCE_DIR}/examples/codec-streaming-compression/app.js")
set(stream_readme "${PROJECT_SOURCE_DIR}/examples/codec-streaming-compression/README.md")
set(checksum_app "${PROJECT_SOURCE_DIR}/examples/codec-checksums/app.js")
set(checksum_readme "${PROJECT_SOURCE_DIR}/examples/codec-checksums/README.md")

foreach(required_file IN ITEMS
        "${base_app}" "${base_readme}"
        "${text_app}" "${text_readme}"
        "${compression_app}" "${compression_readme}"
        "${stream_app}" "${stream_readme}"
        "${checksum_app}" "${checksum_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing Codec example file: ${required_file}")
    endif()
endforeach()

file(READ "${base_app}" base_source)
file(READ "${base_readme}" base_readme_source)
file(READ "${text_app}" text_source)
file(READ "${text_readme}" text_readme_source)
file(READ "${compression_app}" compression_source)
file(READ "${compression_readme}" compression_readme_source)
file(READ "${stream_app}" stream_source)
file(READ "${stream_readme}" stream_readme_source)
file(READ "${checksum_app}" checksum_source)
file(READ "${checksum_readme}" checksum_readme_source)

foreach(required_pattern IN ITEMS
        "import { Base64, Base64Url, Hex } from \"sloppy/codec\";"
        "Base64.encode(bytes)"
        "Base64Url.encode(bytes, { padding: false })"
        "Hex.decode(encoded.hex)")
    require_substring("${base_source}" "${required_pattern}"
                      "examples/codec-base64-hex/app.js is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Binary, Text } from \"sloppy/codec\";"
        "Text.utf8.encode("
        "writer.u32le(1)"
        "writer.u16be(payload.length)"
        "Binary.reader(packet)")
    require_substring("${text_source}" "${required_pattern}"
                      "examples/codec-text-binary/app.js is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Compression, Text } from \"sloppy/codec\";"
        "await Compression.gzip(body, { level: 6 })"
        "await Compression.gunzip(gz, { maxOutputBytes: 1024 * 1024 })")
    require_substring("${compression_source}" "${required_pattern}"
                      "examples/codec-compression/app.js is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "Compression.gzipStream(chunks(), { signal, deadline })"
        "Deadline.after(1000)")
    require_substring("${stream_source}" "${required_pattern}"
                      "examples/codec-streaming-compression/app.js is missing expected API shape")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Checksums, Text } from \"sloppy/codec\";"
        "Checksums.crc32(payload)")
    require_substring("${checksum_source}" "${required_pattern}"
                      "examples/codec-checksums/app.js is missing expected API shape")
endforeach()

foreach(source IN ITEMS
        "${base_source}" "${text_source}" "${compression_source}" "${stream_source}"
        "${checksum_source}")
    foreach(forbidden_pattern IN ITEMS
            "Buffer."
            "node:"
            "require("
            "Bun."
            "Deno."
            "npm "
            "yarn "
            "pnpm "
            "console.log")
        reject_substring("${source}" "${forbidden_pattern}"
                         "Codec examples must keep evidence boundaries clear")
    endforeach()
endforeach()

foreach(readme_source IN ITEMS
        "${base_readme_source}" "${text_readme_source}" "${compression_readme_source}"
        "${stream_readme_source}" "${checksum_readme_source}")
    foreach(required_pattern IN ITEMS
            "This example")
        require_substring("${readme_source}" "${required_pattern}"
                          "Codec example README is missing useful example text")
    endforeach()
    foreach(forbidden_pattern IN ITEMS
            "compatible with Node"
            "compatible with Web Streams"
            "compatible with Bun"
            "compatible with Deno"
            "performance improvement"
            "fastest"
            "no overhead")
        reject_substring("${readme_source}" "${forbidden_pattern}"
                         "Codec example README must not overstate compatibility or performance")
    endforeach()
endforeach()

foreach(forbidden_pattern IN ITEMS
        "authentication"
        "attacker-resistant"
        "signature"
        "HMAC"
        "password"
        "cryptographic hash")
    reject_substring("${checksum_source}" "${forbidden_pattern}"
                     "Checksum example source must not present CRC32 as security")
endforeach()

foreach(required_pattern IN ITEMS
        "not authentication"
        "attacker-resistant integrity"
        "appropriate `sloppy/crypto` primitive")
    require_substring("${checksum_readme_source}" "${required_pattern}"
                      "Checksum README is missing required non-security boundary text")
endforeach()
