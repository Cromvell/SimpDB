#include <stdio.h>


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

inline void inline_func(int i) {
  static unsigned int call_number = 0;
  printf("inline_func (call %d): %d\n", call_number++, i);
}

int many_args(char *one, int two, float three, int ******fuck_conventions) {
  return 42;
}

int many_args(char *one, int two, float three, int *****not_enough_stars) {
  return 42;
}

template <typename K>
void t_func(K foo){
  int n = 0;
  printf("Lol\n");

  n++;
  auto s = n + 1;
}

void overloaded_func(int foo){
  int n = 0;

  n++;
  auto s = n + 1;
  printf("Lol: %d\n", s);
}

void overloaded_func(long unsigned int foo, int bar){
  int n = 0;

  n++;
  auto s = n + 1;
  printf("long unsigned int Lol: %d\n", s);
}

void overloaded_func(long unsigned int foo, char* bar){
  int n = 0;

  n++;
  auto s = n + 1;
  printf("long unsigned int Lol: %d\n", s);
}

void overloaded_func(char * foo){
  printf("Lol: %s\n", foo);
}

void overloaded_func(const char * foo){
  printf("Const Lol: %s\n", foo);
}

void debugee2_call() {
    Foo obj;
    obj.member_function(22);
    Foo::static_member_function(22);

    const char * ch = "!";
    overloaded_func((char *)"Wat?");
    overloaded_func(ch);
    overloaded_func(42);

    long unsigned int lui = 100000000000000;
    overloaded_func(lui, 42);
    overloaded_func(lui, (char *)"ponk");

    int value = 69;
    int *a = &value, **b = &a, ***c = &b, ****d = &c, *****e = &d, ******f = &e;
    many_args((char *)"asdf", 1, 3.14, f);

    many_args((char *)"second_overload", 1, 3.14, e);
}
