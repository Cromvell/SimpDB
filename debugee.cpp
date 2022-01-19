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

// Stack unwind
// void a() {
//     int foo = 1;
// }

// void b() {
//     int foo = 2;
//     a();
// }

// void c() {
//     int foo = 3;
//     b();
// }

// void d() {
//     int foo = 4;
//     c();
// }

// void e() {
//     int foo = 5;
//     d();
// }

// void f() {
//     int foo = 6;
//     e();
// }

// int main() {
//     f();
// }

// Variable
int main() {
    long a = 3;
    long b = 2;
    long c = a + b;
    a = 4;
}
