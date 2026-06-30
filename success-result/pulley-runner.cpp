/**
 * Minimal Wasmtime Pulley runner for Android x86.
 * Loads a precompiled .pwasm file and executes it via WASI.
 *
 * Cross-compile with Android NDK, then adb push to device.
 *
 * Usage: pulley-runner <path-to-pwasm>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wasmtime.h>
#include <wasi.h>

#define CHECK_ERROR(err, msg)                                   \
    do {                                                        \
        if ((err)) {                                            \
            wasm_byte_vec_t m;                                  \
            wasmtime_error_message((err), &m);                  \
            fprintf(stderr, "%s: %s\n", (msg), m.data);         \
            wasm_byte_vec_delete(&m);                           \
            wasmtime_error_delete(err);                         \
            goto cleanup;                                       \
        }                                                       \
    } while (0)

#define CHECK_TRAP(trap, msg)                                   \
    do {                                                        \
        if ((trap)) {                                           \
            wasm_byte_vec_t m;                                  \
            wasm_trap_message((trap), &m);                      \
            fprintf(stderr, "%s: %s\n", (msg), m.data);         \
            wasm_byte_vec_delete(&m);                           \
            wasm_trap_delete(trap);                             \
            goto cleanup;                                       \
        }                                                       \
    } while (0)

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.pwasm>\n", argv[0]);
        return 1;
    }

    int ret = 1;
    wasm_config_t *config = NULL;
    wasm_engine_t *engine = NULL;
    wasmtime_store_t *store = NULL;
    wasmtime_context_t *context = NULL;
    wasmtime_linker_t *linker = NULL;
    wasmtime_module_t *module = NULL;
    wasi_config_t *wasi = NULL;
    wasmtime_error_t *error = NULL;
    wasm_trap_t *trap = NULL;

    /* 1. Engine config — Pulley32 + Android-safe settings */
    config = wasm_config_new();
    if (!config) { fprintf(stderr, "config_new failed\n"); return 1; }

    error = wasmtime_config_target_set(config, "pulley32");
    if (error) {
        /* fallback to full triple */
        wasmtime_error_delete(error);
        error = wasmtime_config_target_set(config, "pulley32-unknown-unknown-elf");
        CHECK_ERROR(error, "set target pulley32-unknown-unknown-elf");
    }

    /* Android: disable signal-based traps (conflicts with ART) */
    wasmtime_config_signals_based_traps_set(config, false);
    /* 32-bit: no guard pages to avoid VSS OOM */
    wasmtime_config_memory_guard_size_set(config, 0);
    /* Kotlin/Wasm needs GC */
    wasmtime_config_wasm_gc_set(config, true);
    wasmtime_config_wasm_reference_types_set(config, true);
    wasmtime_config_wasm_function_references_set(config, true);
    /* Kotlin/Wasm uses exceptions */
    wasmtime_config_wasm_exceptions_set(config, true);

    /* 2. Create engine */
    engine = wasm_engine_new_with_config(config);
    config = NULL; /* engine owns config now */
    if (!engine) { fprintf(stderr, "engine_new failed\n"); goto cleanup; }
    printf("[runner] engine created (pulley=%d)\n",
           wasmtime_engine_is_pulley(engine));

    /* 3. Deserialize precompiled module */
    error = wasmtime_module_deserialize_file(engine, argv[1], &module);
    CHECK_ERROR(error, "deserialize module");
    printf("[runner] module loaded: %s\n", argv[1]);

    /* 4. Store + context */
    store = wasmtime_store_new(engine, NULL, NULL);
    if (!store) { fprintf(stderr, "store_new failed\n"); goto cleanup; }
    context = wasmtime_store_context(store);

    /* 5. WASI — inherit stdio + env */
    wasi = wasi_config_new();
    if (!wasi) { fprintf(stderr, "wasi_config_new failed\n"); goto cleanup; }
    wasi_config_inherit_stdin(wasi);
    wasi_config_inherit_stdout(wasi);
    wasi_config_inherit_stderr(wasi);
    wasi_config_inherit_env(wasi);

    error = wasmtime_context_set_wasi(context, wasi);
    wasi = NULL; /* context owns wasi now */
    CHECK_ERROR(error, "set wasi");

    /* 6. Linker — define WASI and instantiate */
    linker = wasmtime_linker_new(engine);
    if (!linker) { fprintf(stderr, "linker_new failed\n"); goto cleanup; }

    error = wasmtime_linker_define_wasi(linker);
    CHECK_ERROR(error, "linker define wasi");

    wasmtime_instance_t instance;
    error = wasmtime_linker_instantiate(linker, context, module, &instance, &trap);
    CHECK_ERROR(error, "instantiate");
    CHECK_TRAP(trap, "instantiate trap");
    printf("[runner] instantiated\n");

    /* 7. Try _start (command model) */
    wasmtime_extern_t start_fn;
    if (wasmtime_instance_export_get(context, &instance, "_start", 6, &start_fn)
        && start_fn.kind == WASMTIME_EXTERN_FUNC) {
        printf("[runner] calling _start ...\n");
        error = wasmtime_func_call(context, &start_fn.of.func, NULL, 0, NULL, 0, &trap);
        CHECK_ERROR(error, "_start");
        CHECK_TRAP(trap, "_start trap");
        printf("[runner] _start returned\n");
    }

    /* 8. Try _initialize (reactor model) */
    wasmtime_extern_t init_fn;
    if (wasmtime_instance_export_get(context, &instance, "_initialize", 11, &init_fn)
        && init_fn.kind == WASMTIME_EXTERN_FUNC) {
        printf("[runner] calling _initialize ...\n");
        error = wasmtime_func_call(context, &init_fn.of.func, NULL, 0, NULL, 0, &trap);
        CHECK_ERROR(error, "_initialize");
        CHECK_TRAP(trap, "_initialize trap");
        printf("[runner] _initialize returned\n");
    }

    /* 9. Call exported function with i32 args if specified:
     *    Usage: pulley-runner <file.pwasm> [func_name arg1 arg2 ...]
     */
    if (argc >= 4) {
        const char *func_name = argv[2];
        size_t func_name_len = strlen(func_name);
        int nargs = argc - 3;

        wasmtime_extern_t func;
        if (wasmtime_instance_export_get(context, &instance, func_name, func_name_len, &func)
            && func.kind == WASMTIME_EXTERN_FUNC) {

            /* Build i32 args */
            wasmtime_val_t *args = new wasmtime_val_t[nargs];
            for (int i = 0; i < nargs; i++) {
                args[i].kind = WASMTIME_I32;
                args[i].of.i32 = atoi(argv[3 + i]);
            }

            wasmtime_val_t results[2] = {};
            printf("[runner] calling %s(", func_name);
            for (int i = 0; i < nargs; i++) {
                if (i > 0) printf(", ");
                printf("%d", args[i].of.i32);
            }
            printf(") ...\n");

            error = wasmtime_func_call(context, &func.of.func, args, nargs, results, 2, &trap);
            CHECK_ERROR(error, func_name);
            CHECK_TRAP(trap, "func trap");

            /* Print results */
            printf("[runner] result: ");
            for (size_t i = 0; i < 2; i++) {
                if (results[i].kind == WASMTIME_I32) {
                    printf("%d ", results[i].of.i32);
                } else if (results[i].kind == WASMTIME_I64) {
                    printf("%lld ", (long long)results[i].of.i64);
                }
            }
            printf("\n");

            delete[] args;
        } else {
            fprintf(stderr, "[runner] export '%s' not found\n", func_name);
        }
    }

    ret = 0;
    printf("[runner] done.\n");

cleanup:
    if (linker) wasmtime_linker_delete(linker);
    if (store)  wasmtime_store_delete(store);
    if (module) wasmtime_module_delete(module);
    if (engine) wasm_engine_delete(engine);
    if (config) wasm_config_delete(config);
    if (wasi)   wasi_config_delete(wasi);
    return ret;
}
