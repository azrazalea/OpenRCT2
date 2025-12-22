# RPC Handlers

## Adding New Handlers

1. Create `handlers/NewDomainHandlers.cpp`
2. Include `HandlerRegistry.h`, `HandlerInit.h`, `RpcTypes.h`, `RpcUtils.h`
3. Use the registrar struct pattern below
4. Add `void InitNewDomainHandlers();` to `HandlerInit.h`
5. Call it from `HandlerRegistry::InitializeAllHandlers()`

## Handler Registration Pattern

```cpp
namespace OpenRCT2::Scripting::Rpc::Handlers
{
    using namespace Rpc;  // For shared types, constants, utilities

    namespace
    {
        RpcResult HandleFoo(const json_t& params) { /* ... */ }

        struct FooHandlerRegistrar
        {
            FooHandlerRegistrar()
            {
                auto& registry = HandlerRegistry::Instance();
                registry.Register("foo.bar", HandleFoo);
            }
        } fooRegistrar;
    }

    void InitFooHandlers() { (void)fooRegistrar; }
}
```

## Shared Infrastructure

**RpcTypes.h** provides:
- `RpcResult` - return type for all handlers
- `RideLookupResult` - common ride lookup struct
- Error constants: `kErrorInvalidParams`, `kErrorActionFailed`, `kErrorNotFound`, `kErrorServerError`

**RpcUtils.h** provides:
- Parameter extraction: `GetIntParam`, `GetStringParam`, `GetBoolParam`, `GetDoubleParam`
- Hint builders: `MakeGenericWindowHint`, `MakeTileHint`, `BuildTileCameraTarget`
- Utilities: `ToLower`, `NormaliseToken`, `BuildGameActionErrorMessage`, `MoneyToDouble`
