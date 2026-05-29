// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify -Wno-unused-value %s
// expected-no-diagnostics

// CWG1815: a temporary bound to a reference member via a default member
// initializer in aggregate initialization is lifetime-extended to match the
// enclosing object. When the enclosing object is itself a temporary, the
// member temporary persists to the end of the full-expression (not anchored to
// the field), so its destructor runs there during constant evaluation.

struct A { int &x; constexpr ~A() { x = 0; } };
struct AA { int &x; constexpr ~AA() { x = -1; } };
struct B { int &x; const A &a = A{x}; };
struct BB { int &x; const AA &a = AA{x}; };

constexpr int g1() { int x = 1; B{x}; return x; }
constexpr int g2() { int x = 1; B{x}, BB{x}; return x; }
static_assert(g1() == 0);
static_assert(g2() == 0);

// Variable aggregate: the temporary is extended to the variable's lifetime.
constexpr int h() { int x = 1; { B b{x}; (void)b; } return x; }
static_assert(h() == 0);
