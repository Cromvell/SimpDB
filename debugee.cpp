#include <stdio.h>
#include <unistd.h>

#include "debugee2.cpp"

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

int main() {
    printf("in debugee, process pid: %d\n", getpid());

    f();
    inline_func(42);
    inline_func(1337);
    inline_func(29);
    t_func(52);

    debugee2_call();

    // sleep(1000);

    f();

    return 0;
}

// // Variables
// int main() {
//     long a = 3;
//     long b = 2;
//     long c = a + b;
//     a = 4;
// }
