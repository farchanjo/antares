# CLI Surface

antares is a GUI application; there is no end-user command line yet. What exists
is the build and check surface, and it is stable.

## Build surface

The cycle these verbs serve is `doc/arch/runbooks/dev-loop.md`: author here,
validate on the macOS host, and only then publish.

| Verb | Effect |
|------|--------|
| `cmake --preset dev` | configure the development build |
| `cmake --build --preset dev` | build it |
| `cmake --build --preset dev --target check` | build and run every test |
| `cmake --build --preset dev --target asan` | the AddressSanitizer pass |
| `cmake --build --preset dev --target leak` | the leak pass on a sanitizer-free build |
| `cmake --build --preset dev --target lint` | the rules check |

## Environment

| Variable | Effect |
|----------|--------|
| `ANTARES_IO_BACKEND` | `kqueue` (default) or `poll` |
| `ANTARES_ALLOCATOR` | `mimalloc` (default) or `system` |
| `JOBS` | caps the build width at configure time |

## Conventions

Exit codes are stable and small: `0` success, `1` test failure, `2` usage
error. A tool that needs more granularity adds a code rather than reusing one.

## Examples

```sh
cmake --preset dev
cmake --build --preset dev --target check
ANTARES_IO_BACKEND=poll cmake --build --preset dev --target check
```

## Reference

Presets live in `CMakeLists.txt`. Targets are thin wrappers over `ctest`; there
is no shell script between the caller and the build.
