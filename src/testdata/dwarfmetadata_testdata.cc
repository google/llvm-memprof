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

// The debugging info dwarfmetadata_testdata is built from this source file.

/*
# Command to build LLVM clang:
~$: blaze build -c opt /third_party/llvm/llvm/tools/clang

# Command to build testdata binary:
~$: clang -g -Wall dwarfmetadata_testdata.cc -o dwarfmetadata_testdata.k8

# Version of the compiler:
~$: clang --version
clang version google3-trunk (trunk r340869)
Target: x86_64-grtev4-linux-gnu
Thread model: posix

*/

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
  CCC() {};
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
