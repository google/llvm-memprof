# Informal collection of useful commands

## Clang

```
cmake -GNinja -DLLVM_ENABLE_PROJECTS="clang"  -DCMAKE_LINKER="lld" -DLLVM_ENABLE_LLD=On -DCMAKE_INSTALL_PREFIX=`pwd`/install -DLLVM_BUILD_TOOLS=ON  -DCMAKE_BUILD_TYPE=Debug -DLLVM_BUILD_LTO=OFF -DBUILD_SHARED_LIBS=OFF  -DLLVM_ENABLE_TERMINFO=OFF  -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INSTALL_UTILS=OFF -DCMAKE_C_COMPILER=/usr/local/google/home/weingartenmatt/Workspace/llvm-project/build/bin/clang -DCMAKE_CXX_COMPILER=/usr/local/google/home/weingartenmatt/Workspace/llvm-project/build/bin/clang++ -DCMAKE_CXX_FLAGS=" -Wl, -fno-exceptions -fdebug-info-for-profiling -mno-omit-leaf-frame-pointer -fno-omit-frame-pointer -fno-optimize-sibling-calls -m64 -Wl,-build-id -no-pie -fmemory-profile -mllvm -memprof-histogram -fPIC"    ../llvm
```