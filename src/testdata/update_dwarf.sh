# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#!/bin/bash

# Regenerate dwarf binaries for type_resolver_test.

# Setup environment and path.

readonly TESTDATA_PATH="${TOP_DIR}/src/testdata"

readonly MEMPROF_FLAGS=" -fuse-ld=lld -Wl,--no-rosegment \
-fno-exceptions -fdebug-info-for-profiling -fPIC \
-mno-omit-leaf-frame-pointer \
-fno-omit-frame-pointer -fno-optimize-sibling-calls \
-m64 -Wl,-build-id -no-pie -fPIC -fmemory-profile \
-mllvm -memprof-use-callbacks=true -mllvm -memprof-histogram"

readonly CC_FLAGS="\
  -std=c++20 \
  -stdlib=libstdc++ \
  -g -gdwarf-5 \
  -fuse-ld=lld \
  -Wl,-build-id \
  -L${TOP_DIR}/third_party/llvm-project/build/lib \
  -Wl,-rpath,${TOP_DIR}/third_party/llvm-project/build/lib \
"


set -e
set -x

which clang
which clang++
which ld.lld

function compile_and_cp () {
  eval "clang++ $CC_FLAGS ${TESTDATA_PATH}/$1.cc -o ${TESTDATA_PATH}/$1.dwarf"
}

function compile_proto () {
  eval "bazel build //src/testdata:$1 \
        --features=-simple_template_names \
        -c dbg \
        --copt=-O0 \
        --copt=-g \
        --copt=-gdwarf-5 \
        --copt=-ggdb \
        --strip=never"
  rm -rf ${TESTDATA_PATH}/$1.dwp || true
  cp bazel-bin/src/testdata/$1 ${TESTDATA_PATH}/$1.dwp || true
}

function compile_local () {
  eval "clang++ -mllvm -memprof-use-callbacks=true \
  -fPIC -fuse-ld=lld -Wl,--no-rosegment -g -fdebug-info-for-profiling \
  -mno-omit-leaf-frame-pointer -fno-omit-frame-pointer -fno-optimize-sibling-calls \
  -m64 -Wl,-build-id -no-pie -fmemory-profile=${TESTDATA_PATH}\
  ${TESTDATA_PATH}/$1.cc -o ${TESTDATA_PATH}/$1.exe_test"
}

function compile_bazel () {
  eval "bazel run --config=memprof --fission=no --strip=never \
  -c dbg --fdo_instrument=/tmp \ --copt=-fuse-ld=lld\
  --copt=-g --copt=-fdebug-info-for-profiling --copt=-O0 \
  --copt=-mllvm --copt=-memprof-histogram --copt=-fdebug-info-for-profiling \
  --copt=-mno-omit-leaf-frame-pointer --copt=-fno-omit-frame-pointer \
  --copt=-fno-optimize-sibling-calls \
  --copt=-m64 --copt=-Wl,--copt=-build-id --copt=-fPIC \
  --copt=-no-pie --copt=-fmemory-profile=${TESTDATA_PATH}\
  //src/testdata:$1"
  eval "mv bazel-bin/src/testdata/$1 ${TESTDATA_PATH}.exe_test"
}

function run_and_copy_memprof () {
  eval "${TESTDATA_PATH}/$1.exe_test"
  memprof_raw=$(find $TESTDATA_PATH -name "memprof.profraw.*" -print -quit 2>/dev/null)
  eval "echo ${memprof_raw}"
  mv "${memprof_raw}" "${TESTDATA_PATH}/$1.memprofraw_test"
}

# Initial dwarfmetadata test data.
function write_dwarfmetadata_testdata () {
cat > ${TESTDATA_PATH}/dwarfmetadata_testdata.cc << EOF
class Foo {
 public:
  class FooInsider {
   public:
    int a_;
    int b_;
    int c_;
  };

  Foo() {}
  Foo(const Foo &other) {}
  int a_;
  char bad_pad_;
  int *b_;
  char b_arr_[32];
  int **c_;
};

template <typename T>
class Bar {
 public:
  class BarPublicInsider {
   public:
    int a_;
    T t_;
  };
  T GetT() { return b_ + bpi_.t_ + bpi2_.t_; }
  Foo c_;
  Bar *d_;
  Foo *e_;
  Foo::FooInsider i_;
  BarPublicInsider bpi_;

 private:
  class BarPrivateInsider {
   public:
    int a_;
    T t_;
    T **t_p_p_;
  };
  int a_;
  T b_;
  BarPrivateInsider bpi2_;
};

namespace AAA {
namespace BBB {

class CCC {
 public:
  CCC(double a, int b) {
    ccc1 = b;
    ccc2 = a;
    fff.bad_pad_ = 'c';
  }
  CCC(int a) : ccc1(a) {}
  CCC(){};
  Foo fff;
  int ccc1;
  double ccc2;
};

class Foo {
 public:
  Foo() {}
  int a;
  int b;
};

class ChildFoo : Foo {
 public:
  ChildFoo() {}
  int c;
  int b;
};

}  // namespace BBB
}  // namespace AAA

typedef Foo FooFoo;
typedef int int32_t;
typedef int32_t myint32_t;
typedef AAA::BBB::CCC MyCCC;

int main() {
  Bar<char> bar1;
  Bar<int> bar2;
  Bar<Foo> bar3;
  Bar<Foo> bar4 = bar3;
  Bar<AAA::BBB::CCC> bar5;
  Bar<MyCCC> bar6;
  FooFoo foofoo;
  myint32_t i = 0;
  AAA::BBB::CCC ccc(1.0, 2);
  AAA::BBB::ChildFoo cf;
  MyCCC ccc2(1);
  return i;
}
EOF
}

# Basic type test with simple class.
function write_basic_type () {
cat > ${TESTDATA_PATH}/basic_type.cc << EOF
class A {
public:
  long int x;
  long int y;
};

int main(int argc, char **argv) {
  A *a = new A;
  return 0;
}
EOF
}

# Embedded type checks that we can recursively resolve types.
function write_embedded_type_test () {
cat > ${TESTDATA_PATH}/embedded_type.cc << EOF
class A {
public:
  long int x;
  long int y;
};

class B {
public:
  A a;
};

int main(int argc, char **argv) {
  B* b = new B;
  return 0;
}
EOF
}
# Enum type checks that we can recursively resolve types.
function write_enum_type_test () {
cat > ${TESTDATA_PATH}/enum_type.cc << EOF
enum E { X = 1, Y = 2, Z = 3 };

class A {
public:
  E e;
  double x;
};

int main() {
  A *a = new A;
  return 0;
}
EOF
}

# Clashing names in different namespaces test.
function write_namespace_clash_test () {
cat > ${TESTDATA_PATH}/namespace_clash.cc << EOF
namespace name1 {
class A {
 public:
  long x;
  long y;
};
}  // namespace name1

namespace name2 {
class A {
 public:
  double x;
  double y;
};
}  // namespace name2

int main() {
  name1::A *a1 = new name1::A;
  name2::A *a2 = new name2::A;
  return 0;
}
EOF
}

# Field with type from different namespace test.
function write_namespace_field_test () {
cat > ${TESTDATA_PATH}/namespace_field.cc << EOF
#include <string>

namespace n1 {
struct B {
  long x;
  B() : x(1) {}
};
}

struct A {
  long x;
  std::string y;
  n1::B b;
  A() : x(1), y(""), b() {}
};

int main() {
  A* a = new A;
  n1::B* bobj = new n1::B;
  return 0;
}
EOF
}

# Typedef into another namespace test.
function write_namespace_typedef_test () {
cat > ${TESTDATA_PATH}/namespace_typedef.cc << EOF
namespace n1 {
struct A {
  double x;
  double y;
};
}

namespace n2 {
typedef n1::A B;
}

int main() {
  n1::A* a = new n1::A;
  n2::B* b = new n2::B;
  return 0;
}
EOF
}

# Check if we can insert padding between fields.
function write_padding_type_test () {
cat > ${TESTDATA_PATH}/padding_type.cc << EOF
struct A{
public:
  int x;
  long int y;
};

struct B{
public:
  long int y;
  int x;
} __attribute__ ((packed));

struct C{
public:
  B b;
  double x;
};

int main(int argc, char **argv) {
  A* b = new A;
  C* c = new C;
  return 0;
}
EOF
}

# Container test. Checks if we can unwrap the type from membuf, and
# resolve special case where two fields share the same offset in DWARF data.
function write_map_type () {
cat > ${TESTDATA_PATH}/std_map_type.cc << EOF
#include <map>
class A {
public:
  double x;
  double y;
};

int main(int argc, char **argv) {
  std::map<long unsigned, A> As;
}
EOF
}

# Union test. Checks if we can resolve union types.
function write_union_type() {
cat > ${TESTDATA_PATH}/union_type.cc << EOF
struct B {
    int x;
    int y;
};
struct C {
    double x;
};
union A {
    B b;
    C c;
};
struct X {
    A a;
};
int main(int argc, char **argv) {
  A * a = new A;
  X * x = new X;
  return 0;
}
EOF
}


# Vector type test. Checks if we can resolve vector types.
function write_vector_type () {
cat > ${TESTDATA_PATH}/vector_type.cc << EOF
#include <vector>
int main(int argc, char **argv) {
  std::vector<double>* v  = new std::vector<double>();
  return 0;
}
EOF
}

# Array access count test. Checks if we
# can resolve accesses into structs inside embedded arrays.
function write_array_access_count_test () {
cat > ${TESTDATA_PATH}/array_access_count_test.cc << EOF
struct A {
  double x;
  double y;
};

struct B {
  A a[4];
};

struct C {
  B b[4];
};

struct D {
  int x[4];
};

struct E {
  double x;
  A a[4];
};

int main(int argc, char **argv) {
  B* b = new B;
  C* c = new C;
  D* d = new D;
  E* e = new E;
}
EOF
}

# Array type test. Checks if we can resolve embedded arrays.
function write_array_type() {
cat > ${TESTDATA_PATH}/array_type.cc << EOF
struct A {
  long int x;
  int y[24];
};

struct B {
  A a[12];
};

struct C {
  long int x;
  int y[24][24];
  A a[24][24];
};

struct D {
  A* a[12];
};

struct E {
  int x[3];
  double y;
};

int main(int argc, char **argv) {
  A * a = new A;
  B * b = new B;
  C * c = new C;
  D * d = new D;
  E * e = new E;
  return 0;
}
EOF
}

# Simple record access type test. Checks if we can resolve
# accesses into fields of a struct correctly.
function write_simple_record_access_type(){
cat > ${TESTDATA_PATH}/simple_record_access_type.cc << EOF
class A {
public:
  long int x;
  long int y;
};

class B {
public:
  A a;
};

class C {
public:
  char c;
};

class D {
public:
  int x;
  int y;
  int z;
} __attribute__((packed));

int main(int argc, char **argv) {
  B* b = new B;
  C* c = new C;
  D* d = new D;
  return 0;
}
EOF
}

# Container test. Checks if we can resolve types of const pointers.
function write_const_pointer_type() {
cat > ${TESTDATA_PATH}/const_pointer_type.cc << EOF
#include <vector>
class A {
public:
  double x;
  double y;
};

int main(int argc, char **argv) {
  std::vector<const A *> As;
  As.push_back(new A());
}
EOF
}

# Write unique pointer container test. Checks if we
# can resolve type of unique pointers inside containers.
function write_vector_unique_pointer_type() {
cat > ${TESTDATA_PATH}/vector_unique_pointer_type.cc << EOF
#include <vector>
#include <memory>
class A {
public:
  double x;
  double y;
};
int main(int argc, char **argv) {
  std::vector<std::unique_ptr<A>> As;
  As.push_back(std::make_unique<A>());
}
EOF
}

# Write higher-order function container test.
function write_vector_function_type() {
cat > ${TESTDATA_PATH}/vector_function_type.cc << EOF
#include <functional>
#include <vector>
class A {
public:
  double x;
  double y;
};
int main(int argc, char **argv) {
  std::vector<std::function<void(const A&, int)>> As;
}
EOF
}

# Write simple union type test.
function write_simple_union_type() {
cat > ${TESTDATA_PATH}/simple_union_type.cc << EOF
struct SimpleUnion {
  union A {
    int x;
    float y;
  };
  A a;
};
int main(int argc, char **argv) {
  SimpleUnion su;
}
EOF
}

# Write anonymous union test.
function write_anonymous_union_type() {
cat > ${TESTDATA_PATH}/anonymous_union_type.cc << EOF
struct Intern1 {
  int x;
  int y;
};
struct Intern2 {
  char str[4];
};
struct AnonymousUnion {
  union {
    int x;
    double y;
  };
  union {
    Intern1 i1;
    Intern2 i2;
  };
  union {
    Intern1 i3;
  };
};
int main(int argc, char **argv) {
  AnonymousUnion au;
}
EOF
}

# Write std::optional test.
function write_std_optional_type() {
cat > ${TESTDATA_PATH}/std_optional_type.cc << EOF
#include <optional>
struct A {
  int x;
  int y;
};
struct B {
  std::optional<A> a;
};
int main(int argc, char **argv) {
  B b;
}
EOF
}


main() {
  write_dwarfmetadata_testdata
  compile_and_cp "dwarfmetadata_testdata"
  write_basic_type
  compile_and_cp "basic_type"
  write_embedded_type_test
  compile_and_cp "embedded_type"
  write_enum_type_test
  compile_and_cp "enum_type"
  write_namespace_clash_test
  compile_and_cp "namespace_clash"
  write_namespace_field_test
  compile_and_cp "namespace_field"
  write_namespace_typedef_test
  compile_and_cp "namespace_typedef"
  write_padding_type_test
  compile_and_cp "padding_type"
  write_map_type
  compile_and_cp "std_map_type"
  write_union_type
  compile_and_cp "union_type"
  write_vector_type
  compile_and_cp "vector_type"
  write_array_type
  compile_and_cp "array_type"
  write_simple_record_access_type
  compile_and_cp "simple_record_access_type"
  write_array_access_count_test
  compile_and_cp "array_access_count_test"
  write_const_pointer_type
  compile_and_cp "const_pointer_type"
  write_vector_unique_pointer_type
  compile_and_cp "vector_unique_pointer_type"
  write_vector_function_type
  compile_and_cp "vector_function_type"
  write_simple_union_type
  compile_and_cp "simple_union_type"
  write_anonymous_union_type
  compile_and_cp "anonymous_union_type"
  write_std_optional_type
  compile_and_cp "std_optional_type"

  compile_proto "proto_simple"
  compile_proto "proto_complex"



  # Fix supported containers generation
  # compile_local "supported_stl_containers"
  # run_and_copy_memprof "supported_stl_containers"
  # compile_bazel "supported_adt_containers"
  # compile_bazel "supported_abseil_containers"
}

main