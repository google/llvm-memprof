// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <sanitizer/memprof_interface.h>
// #include "<sanitizer_common/default_options.h>"

#include <new>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <cstdio>


extern "C" {

// Override via MEMPROF_OPTIONS env var, e.g.:
// MEMPROF_OPTIONS='flag=value'
// Refer to the flags in the following files:
// http://google3/third_party/llvm/llvm-project/compiler-rt/lib/memprof/memprof_flags.inc
// http://google3/third_party/llvm/llvm-project/compiler-rt/lib/sanitizer_common/sanitizer_flags.inc.
#define MEMPROF_DEFAULT_OPTIONS " malloc_context_size=255"

// // Override default MemProf runtime options.
const char* __memprof_default_options() {
//   return SANITIZER_COMMON_DEFAULT_OPTIONS_NORMAL MEMPROF_DEFAULT_OPTIONS;
  return MEMPROF_DEFAULT_OPTIONS;
}
}  // extern "C"



// Memprof and bazel toghether will intercept new calls (reason unkown) when new is not builtin.
// For now the fix is to override new here and link it in each bazel build.

static inline std::size_t round_up(std::size_t n, std::size_t a) {
  return (n + (a - 1)) & ~(a - 1);
}

void* operator new(std::size_t n)
#if __cpp_exceptions
  throw(std::bad_alloc)
#endif
{
  if (void* p = malloc(n)) return p;
#if __cpp_exceptions
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

void* operator new[](std::size_t n)
#if __cpp_exceptions
  throw(std::bad_alloc)
#endif
{
  if (void* p = malloc(n)) return p;
#if __cpp_exceptions
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
  return malloc(n);
}

void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
  return malloc(n);
}

void* operator new(std::size_t n, std::align_val_t a)
#if __cpp_exceptions
  throw(std::bad_alloc)
#endif
{
  if (n == 0) n = 1;
  std::size_t align = static_cast<std::size_t>(a);
  if (align < sizeof(void*)) align = sizeof(void*);

  const std::size_t size = round_up(n, align);
  if (void* p = std::aligned_alloc(align, size)) return p;

#if __cpp_exceptions
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

void* operator new[](std::size_t n, std::align_val_t a)
#if __cpp_exceptions
  throw(std::bad_alloc)
#endif
{
  if (n == 0) n = 1;
  std::size_t align = static_cast<std::size_t>(a);
  if (align < sizeof(void*)) align = sizeof(void*);

  const std::size_t size = round_up(n, align);
  if (void* p = aligned_alloc(align, size)) return p;

#if __cpp_exceptions
  throw std::bad_alloc();
#else
  std::abort();
#endif
}

void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  if (n == 0) n = 1;
  std::size_t align = static_cast<std::size_t>(a);
  if (align < sizeof(void*)) align = sizeof(void*);
  if (align <= alignof(std::max_align_t)) {
    return std::malloc(n);
  }
  const std::size_t size = (n + (align - 1)) & ~(align - 1);
  return std::aligned_alloc(align, size);
}

void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
  if (n == 0) n = 1;
  std::size_t align = static_cast<std::size_t>(a);
  if (align < sizeof(void*)) align = sizeof(void*);
  if (align <= alignof(std::max_align_t)) {
    return std::malloc(n);
  }
  const std::size_t size = (n + (align - 1)) & ~(align - 1);
  return std::aligned_alloc(align, size);
}

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }

void operator delete(void* p, std::size_t) noexcept { free(p); }
void operator delete[](void* p, std::size_t) noexcept { free(p); }

void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }

void operator delete(void* p, std::size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { free(p); }

