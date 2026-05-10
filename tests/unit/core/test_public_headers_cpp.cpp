#include "sloppy/app_host.h"
#include "sloppy/alloc.h"
#include "sloppy/arena.h"
#include "sloppy/async.h"
#include "sloppy/async_backend.h"
#include "sloppy/assert.h"
#include "sloppy/builder.h"
#include "sloppy/bytes.h"
#include "sloppy/cancellation.h"
#include "sloppy/capability.h"
#include "sloppy/checked_math.h"
#include "sloppy/compiler.h"
#include "sloppy/container.h"
#include "sloppy/crypto.h"
#include "sloppy/data.h"
#include "sloppy/data_postgres.h"
#include "sloppy/data_sqlite.h"
#include "sloppy/data_sqlserver.h"
#include "sloppy/diagnostics.h"
#include "sloppy/engine.h"
#include "sloppy/execution_domain.h"
#include "sloppy/features.h"
#include "sloppy/fs.h"
#include "sloppy/http.h"
#include "sloppy/http2_dispatch.h"
#include "sloppy/http2_frame.h"
#include "sloppy/http2_hpack.h"
#include "sloppy/http2_mapping.h"
#include "sloppy/http2_session.h"
#include "sloppy/http_backend.h"
#include "sloppy/http_context.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/http_response.h"
#include "sloppy/http_transport.h"
#include "sloppy/intern.h"
#include "sloppy/logging.h"
#include "sloppy/loop.h"
#include "sloppy/modules.h"
#include "sloppy/net.h"
#include "sloppy/os.h"
#include "sloppy/plan.h"
#include "sloppy/platform.h"
#include "sloppy/platform_process.h"
#include "sloppy/platform_thread.h"
#include "sloppy/platform_time.h"
#include "sloppy/provider_executor.h"
#include "sloppy/request_validation.h"
#include "sloppy/resource.h"
#include "sloppy/route.h"
#include "sloppy/runtime_contract.h"
#include "sloppy/scope.h"
#include "sloppy/source_loc.h"
#include "sloppy/status.h"
#include "sloppy/string.h"
#include "sloppy/worker_pool.h"

int main()
{
    SlEngineOptions engine_options = {};
    SlPlatformProcessArgs process_args = {};
    SlStatus (*executable_path)(char*, size_t) = sl_platform_process_executable_path;
    SlStatus (*run_process)(const SlPlatformProcessArgs*, int*) = sl_platform_process_run;
    SlPlatformThread* thread = nullptr;
    uint64_t now_ns = 0U;

    engine_options.kind = SL_ENGINE_KIND_NONE;
    process_args.file = nullptr;
    process_args.argv = nullptr;
    (void)executable_path;
    (void)run_process;
    sl_platform_thread_join(thread);
    (void)sl_platform_monotonic_time_ns(&now_ns);

    return engine_options.kind == SL_ENGINE_KIND_NONE && process_args.file == nullptr ? 0 : 1;
}
