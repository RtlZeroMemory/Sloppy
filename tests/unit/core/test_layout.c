#include "sloppy/engine.h"
#include "sloppy/http2_session.h"
#include "sloppy/http_context.h"
#include "sloppy/http_dispatch.h"
#include "sloppy/logging.h"
#include "sloppy/plan.h"

#include <stdint.h>

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(SlEngineResult) == 80U,
               "SlEngineResult must keep mutually exclusive result payloads compact");
_Static_assert(_Alignof(SlEngineResult) == _Alignof(SlHttpResponse),
               "SlEngineResult alignment should match its response payload");

_Static_assert(sizeof(SlHttp2ClosedStream) == 16U,
               "SlHttp2ClosedStream must stay compact for the session closed-stream ring");
_Static_assert(_Alignof(SlHttp2ClosedStream) == _Alignof(int64_t),
               "SlHttp2ClosedStream alignment should be driven by its 64-bit window");

_Static_assert(sizeof(SlHttp2Event) == 32U, "SlHttp2Event must keep event batches cache-local");
_Static_assert(_Alignof(SlHttp2Event) == _Alignof(SlHttp2EventPayload),
               "SlHttp2Event alignment should be driven by its payload union");

_Static_assert(sizeof(SlHttpRouteBinding) == 32U,
               "SlHttpRouteBinding must stay compact for route dispatch tables");
_Static_assert(_Alignof(SlHttpRouteBinding) == _Alignof(void*),
               "SlHttpRouteBinding alignment should be pointer-sized");

_Static_assert(sizeof(SlHttpRequestContext) == 200U,
               "SlHttpRequestContext must pack access-planning flags at the tail");
_Static_assert(_Alignof(SlHttpRequestContext) == _Alignof(void*),
               "SlHttpRequestContext alignment should be pointer-sized");
_Static_assert(SL_PLAN_INTERNAL_ABI_VERSION >= UINT32_C(2),
               "native JSON dispatch layout changes must bump the internal Plan ABI tag");

_Static_assert(sizeof(SlLogField) == 248U,
               "SlLogField must store only the active structured-field payload");
_Static_assert(_Alignof(SlLogField) == _Alignof(SlLogText),
               "SlLogField alignment should match its largest payload");
#endif

int main(void)
{
    return 0;
}
