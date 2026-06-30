# Reproducing Alignment Issues on 32-bit Android (Pulley)

This document provides a guide to replicate the compilation and runtime alignment issues encountered when using the Wasmtime C API configured for the 32-bit Android (`i686-linux-android`) target using the Pulley execution engine.

---

## Prerequisites

* **Android NDK**: Version `28.2.13676358` is recommended.
* **Rust**: Nightly toolchain installed.
* **CMake & Ninja**: Required for building the C++ sample runner.
* **Android Emulator(Root Required)**: You can download an emulator via Android Studio and then root it using rootAVD: https://github.com/crowforkotlin/rootAVD
---

## Step-by-Step Replication

### 1. Environment Setup

Clone the specific repository branch and export the path to the Android NDK:

```bash
git clone [https://github.com/crowforkotlin/wasmtime.git](https://github.com/crowforkotlin/wasmtime.git)
cd wasmtime
git checkout issue-c-api-32bit

# Export your NDK path
export ANDROID_NDK_HOME="/home/crowf/Android/Sdk/ndk/28.2.13676358"

```
### 2. Build Wasmtime Artifacts
Generate the required components for both the host compiler and the target Android environment.
```bash
# Build Pulley engine for 32-bit Android
RUSTC_BOOTSTRAP=1 rustup run nightly \
  bash ci/wasmline/build-artifacts.sh pulley-min x86-android i686-linux-android

# Build Cranelift CLI compiler for the host Linux machine
RUSTC_BOOTSTRAP=1 rustup run nightly \
  bash ci/wasmline/build-artifacts.sh cranelift-min x86_64-linux x86_64-unknown-linux-gnu cli

```
### 3. Package Artifacts
Package the compiled binaries into distributable archives:
```bash
bash ci/wasmline/package-artifacts.sh x86_64-linux x86_64-unknown-linux-gnu cli
bash ci/wasmline/package-artifacts.sh x86-android i686-linux-android capi

```
The generated packages will be located in the wasmtime/dist directory. Extract both archives before proceeding.
### 4. Adjust Rust Source Compile-Time Assertions
To bypass early Rust panic during compilation, the alignment and size constraints must be explicitly modified in the Rust codebase. Update the following compile-time assertion block to match 8-byte alignment restrictions:
```rust
const _: () = {
-    assert!(std::mem::size_of::<wasmtime_val_union>() <= 24);
-    assert!(std::mem::align_of::<wasmtime_val_union>() == std::mem::align_of::<u64>());
+    assert!(std::mem::size_of::<wasmtime_val_union>() <= 32);
+    assert!(std::mem::align_of::<wasmtime_val_union>() == 8);
};

```
> **Note**: Modifying only the Rust-side assertions allows the binary to compile but exposes structural misalignment in the C header definitions, triggering downstream C++ compilation failures.
> 
### 5. Ahead-of-Time (AOT) Module Compilation
Utilize the host Cranelift CLI to compile the target WebAssembly module into a Pulley-compatible bytecode format (.pwasm32):
```bash
./wasmtime compile ~/Downloads/kotlin.wasm -o kotlin.pwasm32 \
  --target pulley32 \
  -W gc=y \
  -W function-references=y \
  -W exceptions=y \
  -W simd=n \
  -W relaxed-simd=n \
  -O static-memory-guard-size=0 \
  -O dynamic-memory-guard-size=0 \
  -O signals-based-traps=n \
  -O opt-level=2 \
  -C cranelift-debug-verifier=no 

```
### 6. Build the C++ Runner (Triggers Compilation Error)
When compiling the C++ runner with Clang using the provided cross-compilation script (build-runner.sh), the C++ compiler enforces structural validation via static_assert defined in the Wasmtime C API headers.
```bash
bash build-runner.sh

```
#### Observed Error Output
```text
In file included from /home/crowf/Downloads/error-result/pulley-runner.cpp:12:
In file included from /home/crowf/Downloads/error-result/wasmtime-dev-x86-android-c-api/include/wasmtime.h:199:
In file included from /home/crowf/Downloads/error-result/wasmtime-dev-x86-android-c-api/include/wasmtime/anyref.h:12:
/home/crowf/Downloads/error-result/wasmtime-dev-x86-android-c-api/include/wasmtime/val.h:353:17: error: static assertion failed due to requirement '__alignof(wasmtime_valunion) == 8': should be 8-byte aligned
  353 |   static_assert(__alignof(wasmtime_valunion_t) == 8,
      |                 ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/crowf/Downloads/error-result/wasmtime-dev-x86-android-c-api/include/wasmtime/val.h:353:48: note: expression evaluates to '4 == 8'
  353 |   static_assert(__alignof(wasmtime_valunion_t) == 8,
      |                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^~~~
/home/crowf/Downloads/error-result/wasmtime-dev-x86-android-c-api/include/wasmtime/val.h:356:17: error: static assertion failed due to requirement '__alignof(wasmtime_val_raw) == 8': should be 8-byte aligned
  356 |   static_assert(__alignof(wasmtime_val_raw_t) == 8, "should be 8-byte aligned");
      |                 ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
/home/crowf/Downloads/error-result/wasmtime-dev-x86-android-c-api/include/wasmtime/val.h:356:47: note: expression evaluates to '4 == 8'
  356 |   static_assert(__alignof(wasmtime_val_raw_t) == 8, "should be 8-byte aligned");
      |                 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~^~~~

```
**Cause**: On 32-bit x86 Android (i686-none-linux-android), the default alignment for 64-bit data structures inside unions falls back to 4 bytes. This causes a mismatch with the hardcoded 8-byte alignment verification requested by the header files (__alignof(wasmtime_valunion_t) == 8).
### 7. Target Device Execution (Post-Fix Verification)
Once structural byte alignment matches across the Rust implementation and C headers, transfer the artifacts to the target Android device to verify runtime behavior:
```bash
# Push assets and binaries to the device storage
adb push kotlin.pwasm32 /sdcard/wasmtime/
adb push pulley-runner /sdcard/wasmtime/

# Access device shell with root privileges
adb shell root
adb shell

# Execute within the authorized environment
cp /sdcard/wasmtime/* /data/local/tmp/
cd /data/local/tmp/
./pulley-runner kotlin.pwasm32 add 2 3

```
#### Expected Successful Execution Log
```text
[runner] engine created (pulley=1)
[runner] module loaded: kotlin.pwasm32
[runner] instantiated
[runner] calling _start ...
[Kotlin Wasi] Plugin main executed
[runner] _start returned
[runner] calling add(2, 3) ...
add: expected 1 results, got 2
```
