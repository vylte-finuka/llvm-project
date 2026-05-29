// RUN: %clang_cc1 -triple x86_64-unknown-unknown -ast-dump %s | FileCheck %s

struct A {
  int arr[1];
};

struct B {
  const A &a = A{{0}};
};

void test() {
  B b{};
}
// The default member initializer is used in aggregate initialization, so it is
// part of the enclosing full-expression: the ExprWithCleanups wraps the whole
// initialization rather than being nested inside the CXXDefaultInitExpr, and
// the lifetime-extended temporary is anchored to the enclosing variable.
// CHECK: -ExprWithCleanups 0x{{[^ ]*}} <{{.*}}> 'B'
// CHECK-NEXT:  `-InitListExpr 0x{{[^ ]*}} <{{.*}}> 'B'
// CHECK-NEXT:    `-CXXDefaultInitExpr 0x{{[^ ]*}} <{{.*}}> 'const A' lvalue has rewritten init
// CHECK-NEXT:      `-MaterializeTemporaryExpr 0x{{[^ ]*}} <{{.*}}> 'const A' lvalue extended by Var 0x{{[^ ]*}} 'b' 'B'
// CHECK-NEXT:        `-ImplicitCastExpr 0x{{[^ ]*}} <{{.*}}> 'const A' <NoOp>
// CHECK-NEXT:          `-CXXFunctionalCastExpr 0x{{[^ ]*}} <{{.*}}> 'A' functional cast to A <NoOp>
// CHECK-NEXT:            `-InitListExpr 0x{{[^ ]*}} <{{.*}}> 'A'
// CHECK-NEXT:              `-InitListExpr 0x{{[^ ]*}} <{{.*}}> 'int[1]'
// CHECK-NEXT:                `-IntegerLiteral 0x{{[^ ]*}} <{{.*}}> 'int' 0
