/*
 * Sloppy status model.
 *
 * Public APIs return SlStatus for machine-checkable success/failure and emit richer
 * diagnostics through separate diagnostic channels. The message pointer is borrowed by the
 * caller and may be NULL; it is intended for boundary/debug text, not structured handling.
 */
#ifndef SLOPPY_STATUS_H
#define SLOPPY_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SlStatusCode
{
    SL_STATUS_OK = 0,
    SL_STATUS_UNKNOWN = 1,
    SL_STATUS_INVALID_ARGUMENT = 2,
    SL_STATUS_NOT_FOUND = 3,
    SL_STATUS_PERMISSION_DENIED = 4,
    SL_STATUS_OUT_OF_MEMORY = 5,
    SL_STATUS_INTERNAL = 6
} SlStatusCode;

typedef struct SlStatus
{
    SlStatusCode code;
    const char* message;
} SlStatus;

static inline SlStatus sl_status_ok(void)
{
    SlStatus status = {SL_STATUS_OK, 0};
    return status;
}

static inline SlStatus sl_status_make(SlStatusCode code, const char* message)
{
    SlStatus status = {code, message};
    return status;
}

static inline int sl_status_is_ok(SlStatus status)
{
    return status.code == SL_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif
