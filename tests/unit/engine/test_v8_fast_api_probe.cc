#include <v8-fast-api-calls.h>
#include <v8-function-callback.h>
#include <v8-template.h>

#include <cstdint>

namespace {

int32_t fast_route_int(v8::Local<v8::Object> receiver, int32_t slot,
                       v8::FastApiCallbackOptions& options)
{
    (void)receiver;
    (void)options;
    return slot;
}

void slow_route_int(const v8::FunctionCallbackInfo<v8::Value>& args)
{
    (void)args;
}

[[maybe_unused]] void compile_probe(v8::Isolate* isolate)
{
    v8::CFunction fast = v8::CFunction::Make(fast_route_int);
    v8::CFunction overloads[] = {fast};
    v8::Local<v8::FunctionTemplate> single = v8::FunctionTemplate::New(
        isolate, slow_route_int, v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 1,
        v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasNoSideEffect, &fast);
    v8::Local<v8::FunctionTemplate> multi = v8::FunctionTemplate::NewWithCFunctionOverloads(
        isolate, slow_route_int, v8::Local<v8::Value>(), v8::Local<v8::Signature>(), 1,
        v8::ConstructorBehavior::kThrow, v8::SideEffectType::kHasNoSideEffect,
        v8::MemorySpan<const v8::CFunction>(overloads));
    (void)single;
    (void)multi;
}

} // namespace

int main()
{
    return 0;
}
