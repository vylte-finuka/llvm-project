// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s

// A default member initializer with a lambda init-capture, used in aggregate
// initialization, must destroy the closure temporary at the end of the
// enclosing full-expression. See https://github.com/llvm/llvm-project/issues/196469

struct Noisy {
  Noisy();
  ~Noisy();
};

struct Function {
  template <typename F> Function(F) {}
};

struct Options {
  Function function{[noisy = Noisy{}] {}};
};

Options kOptions{};

// CHECK-LABEL: define internal void @__cxx_global_var_init
// CHECK: call void @_ZN5NoisyC1Ev
// CHECK: call void @_ZN8FunctionC1IN7Options8functionMUlvE_EEET_
// CHECK: call void @_ZN7Options8functionMUlvE_D1Ev

// CHECK-LABEL: define {{.*}} @_ZN7Options8functionMUlvE_D2Ev
// CHECK: call void @_ZN5NoisyD1Ev
