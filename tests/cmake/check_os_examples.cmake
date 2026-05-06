set(example_app "${PROJECT_SOURCE_DIR}/examples/os-runtime-api/app.js")
set(example_readme "${PROJECT_SOURCE_DIR}/examples/os-runtime-api/README.md")

file(READ "${example_app}" app_text)
file(READ "${example_readme}" readme_text)

foreach(required IN ITEMS
        "import { Environment, Process, Signals, System } from \"sloppy/os\""
        "System.platform"
        "System.arch"
        "System.cpuCount"
        "System.tempDirectory"
        "Environment.get(\"MY_APP_SETTING\")"
        "Process.run(\"git\", [\"status\", \"--short\"]"
        "capture: \"text\""
        "Process.start(command, args"
        "stdout: \"pipe\""
        "proc.stdout.readLines()"
        "await proc.wait()"
        "Signals.onShutdown")
    string(FIND "${app_text}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "examples/os-runtime-api/app.js is missing expected OS API shape")
    endif()
endforeach()

foreach(required IN ITEMS
        "source-shape evidence only"
        "does not prove shell execution"
        "Node"
        "Deno"
        "PID/native handle exposure"
        "benchmark evidence")
    string(FIND "${readme_text}" "${required}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "examples/os-runtime-api/README.md is missing required boundary text")
    endif()
endforeach()
