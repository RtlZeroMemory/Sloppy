function(require_file path)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing workers example file: ${path}")
    endif()
endfunction()

function(require_match text pattern description)
    string(FIND "${text}" "${pattern}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${pattern}")
    endif()
endfunction()

set(example_root "${PROJECT_SOURCE_DIR}/examples")

foreach(case_name IN ITEMS
        workers-background-service
        workers-workqueue
        workers-workerpool
        workers-js-isolate
        workers-shutdown)
    set(app_path "${example_root}/${case_name}/app.js")
    set(readme_path "${example_root}/${case_name}/README.md")
    require_file("${app_path}")
    require_file("${readme_path}")
    file(READ "${app_path}" app_js)
    file(READ "${readme_path}" readme_md)
    require_match("${app_js}" "from \"sloppy/workers\"" "${case_name} imports sloppy/workers")
    require_match("${app_js}" "export default app" "${case_name} exports the app")
    require_match("${readme_md}" "Workers" "${case_name} README names workers")
endforeach()

file(READ "${example_root}/workers-background-service/app.js" background_js)
require_match("${background_js}" "BackgroundService.create" "background service example")
require_match("${background_js}" "app.use(cleanup)" "background service app lifecycle")

file(READ "${example_root}/workers-workqueue/app.js" queue_js)
require_match("${queue_js}" "WorkQueue.create" "workqueue example")
require_match("${queue_js}" "maxQueued" "workqueue capacity")
require_match("${queue_js}" "concurrency" "workqueue concurrency")
require_match("${queue_js}" "overflow: \"reject\"" "workqueue overflow")

file(READ "${example_root}/workers-workerpool/app.js" pool_js)
require_match("${pool_js}" "WorkerPool.create" "workerpool example")
require_match("${pool_js}" "pool.run" "workerpool example runs work")
require_match("${pool_js}" "Deadline.after" "workerpool deadline")

file(READ "${example_root}/workers-js-isolate/app.js" worker_js)
require_match("${worker_js}" "Worker.start" "js worker example")
require_match("${worker_js}" "worker.invoke" "js worker invoke")
require_file("${example_root}/workers-js-isolate/workers/parser.ts")

file(READ "${example_root}/workers-shutdown/app.js" shutdown_js)
require_match("${shutdown_js}" "stop({ drain: true })" "workers shutdown drain example")
