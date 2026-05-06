/*
 * src/engine/v8/intrinsics_codec.cc
 *
 * Installs the V8-internal codec namespace marker under __sloppy.codec. The PR2
 * Base64/Base64Url/Hex/UTF-8 algorithms are bootstrap stdlib helpers; later
 * Binary and Compression slices may add native bridge functions here.
 */
#include "engine_v8_internal.h"
#include "string_interop.h"

namespace {

SlStatus codec_v8_to_local_string(v8::Isolate* isolate, SlStr str, v8::Local<v8::String>* out)
{
    SlV8Engine* backend =
        isolate == nullptr ? nullptr : static_cast<SlV8Engine*>(isolate->GetData(0));
    return sl_v8_string_from_native_view(backend, str, out);
}

} // namespace

bool sl_v8_install_codec_intrinsics(SlV8Engine* backend, v8::Local<v8::Context> context,
                                    v8::Local<v8::Object> sloppy)
{
    v8::Isolate* isolate = backend == nullptr ? nullptr : backend->isolate;
    v8::Local<v8::String> codec_key;

    if (backend == nullptr || isolate == nullptr) {
        return false;
    }

    if (!backend->has_runtime_features ||
        !sl_v8_runtime_feature_active(backend, SL_RUNTIME_FEATURE_STDLIB_CODEC))
    {
        return true;
    }

    if (!sl_status_is_ok(codec_v8_to_local_string(isolate, sl_str_from_cstr("codec"), &codec_key)))
    {
        return false;
    }
    return sloppy->Set(context, codec_key, v8::Object::New(isolate)).FromMaybe(false);
}
