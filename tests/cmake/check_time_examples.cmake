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

set(time_basic_app "${PROJECT_SOURCE_DIR}/examples/time-basic/app.js")
set(time_basic_readme "${PROJECT_SOURCE_DIR}/examples/time-basic/README.md")
set(time_deadline_app "${PROJECT_SOURCE_DIR}/examples/time-deadline-cancellation/app.js")
set(time_deadline_readme "${PROJECT_SOURCE_DIR}/examples/time-deadline-cancellation/README.md")
set(time_interval_app "${PROJECT_SOURCE_DIR}/examples/time-interval-schedule/app.js")
set(time_interval_readme "${PROJECT_SOURCE_DIR}/examples/time-interval-schedule/README.md")
set(time_fake_clock_app "${PROJECT_SOURCE_DIR}/examples/time-fake-clock/app.js")
set(time_fake_clock_readme "${PROJECT_SOURCE_DIR}/examples/time-fake-clock/README.md")
set(fs_basic_app "${PROJECT_SOURCE_DIR}/examples/fs-basic/app.js")
set(fs_basic_readme "${PROJECT_SOURCE_DIR}/examples/fs-basic/README.md")

foreach(required_file IN ITEMS
        "${time_basic_app}" "${time_basic_readme}"
        "${time_deadline_app}" "${time_deadline_readme}"
        "${time_interval_app}" "${time_interval_readme}"
        "${time_fake_clock_app}" "${time_fake_clock_readme}"
        "${fs_basic_app}" "${fs_basic_readme}")
    if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Missing Time example file: ${required_file}")
    endif()
endforeach()

file(READ "${time_basic_app}" time_basic_source)
file(READ "${time_basic_readme}" time_basic_readme_source)
file(READ "${time_deadline_app}" time_deadline_source)
file(READ "${time_deadline_readme}" time_deadline_readme_source)
file(READ "${time_interval_app}" time_interval_source)
file(READ "${time_interval_readme}" time_interval_readme_source)
file(READ "${time_fake_clock_app}" time_fake_clock_source)
file(READ "${time_fake_clock_readme}" time_fake_clock_readme_source)
file(READ "${fs_basic_app}" fs_basic_source)
file(READ "${fs_basic_readme}" fs_basic_readme_source)

foreach(required_pattern IN ITEMS
        "import { Time } from \"sloppy/time\";"
        "await Time.delay(250);"
        "Time.timeout("
        "afterMs: 1000"
        "Time.yield()")
    require_substring("${time_basic_source}" "${required_pattern}"
                      "examples/time-basic/app.js is missing expected Time API shape")
endforeach()
reject_substring("${time_basic_source}" "setTimeout"
                 "Time examples must not imply Node timer compatibility")

foreach(required_pattern IN ITEMS
        "import { CancellationController, Deadline, Time } from \"sloppy/time\";"
        "import { File } from \"sloppy/fs\";"
        "Deadline.after(5000)"
        "File.readText(\"data:/users.json\", { deadline })"
        "controller.cancel(\"request aborted\")"
        "signal: controller.signal")
    require_substring("${time_deadline_source}" "${required_pattern}"
                      "examples/time-deadline-cancellation/app.js is missing expected API shape")
endforeach()
reject_substring("${time_deadline_source}" "AbortController"
                 "Time examples must use Sloppy CancellationController, not Node/Web AbortController")

foreach(required_pattern IN ITEMS
        "Time.interval(1000"
        "Time.every(\"5m\""
        "noOverlap: true"
        "missedRunPolicy: \"skip\""
        "await job.stop();")
    require_substring("${time_interval_source}" "${required_pattern}"
                      "examples/time-interval-schedule/app.js is missing expected API shape")
endforeach()
reject_substring("${time_interval_source}" "setInterval"
                 "Time examples must not imply Node timer compatibility")

foreach(required_pattern IN ITEMS
        "Time.fakeClock"
        "Time.delay(1000, { clock })"
        "Time.timeout("
        "clock.advanceBy(1000)"
        "clock.advanceBy(500)")
    require_substring("${time_fake_clock_source}" "${required_pattern}"
                      "examples/time-fake-clock/app.js is missing expected fake-clock shape")
endforeach()
reject_substring("${time_fake_clock_source}" "global"
                 "FakeClock example must not imply global fake-timer mutation")

foreach(required_pattern IN ITEMS
        "no Node timer compatibility"
        "not a cron parser"
        "no package-manager behavior"
        "no benchmark claims")
    require_substring("${time_basic_readme_source};${time_interval_readme_source}"
                      "${required_pattern}"
                      "Time example READMEs are missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "explicit test-scoped provider"
        "does not mutate global timers"
        "deterministic")
    require_substring("${time_fake_clock_readme_source}" "${required_pattern}"
                      "FakeClock README is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "Deadline.after"
        "CancellationController"
        "not claim to cancel arbitrary already-running work")
    require_substring("${time_deadline_readme_source}" "${required_pattern}"
                      "Deadline/cancellation README is missing required boundary text")
endforeach()

foreach(required_pattern IN ITEMS
        "import { Deadline } from \"sloppy/time\";"
        "Deadline.after(1000)"
        "File.readJson(\"./tmp/users.json\", { deadline })")
    require_substring("${fs_basic_source}" "${required_pattern}"
                      "examples/fs-basic/app.js is missing Time integration shape")
endforeach()
require_substring("${fs_basic_readme_source}" "deadline"
                  "examples/fs-basic/README.md must document Time integration")
