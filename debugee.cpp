#include <stdio.h>

// struct test{
//     int i;
//     float j;
//     int k[42];
//     test* next;
// };

// int function_call(int x) {
//   static int seed = 0;
//   seed += (x + 1910241) % 124121;
//   return seed;
// }

// int main() {
//   for (int i = 0; i < 10000; i++) {
//     int ret_value = function_call(i);
//     printf("Return value: %d\n", ret_value);
//   }
//   return 0;
// }


int internal_call() {
  int lalz = 0xdeadbeef;
  return lalz;
}

class Foo {
public:
  int member_function(int i) {
    int n = i;
    printf("Lol\n");

    n++;
    auto s = internal_call() + i;
    return s;
  }

  static int static_member_function(int i) {
    return internal_call() + i;
  }
};


// Stack unwind
int a() {
    int foo = 1;
    return foo;
}

int b() {
    int foo = 2;
    return a();
}

int c() {
    int foo = 3;
    return b();
}

int d() {
    int foo = 4;
    return c();
}

int e() {
    int foo = 5;
    return d();
}

int f() {
    int foo = 6;
    return e();
}

inline void inline_func(int i) {
  static int call_number = 0;
  printf("inline_func (call %d): %d\n", call_number++, i);
}

template <typename K>
void t_func(K foo){
  int n = 0;
  printf("Lol\n");

  n++;
  auto s = n + 1;
}

int main() {
    f();
    inline_func(42);
    inline_func(1337);
    inline_func(29);
    t_func(52);


    Foo obj;
    obj.member_function(22);
    Foo::static_member_function(22);

    return 0;
}

// // Variable
// int main() {
//     long a = 3;
//     long b = 2;
//     long c = a + b;
//     a = 4;
// }
