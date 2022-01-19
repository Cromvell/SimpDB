#pragma once

// Warning! Potential issues with unary + overload. See cpp file

/*
#define defer auto __defer_id(__defer_declaration__, __LINE__, __) = +[=]
#define __defer_id(a, b, c) __defer_concat_tokens(a, b, c)
#define __defer_concat_tokens(a, b, c) a ## b ## c

template <typename F>
struct __Defer_Struct {
  F f;
  __Defer_Struct(F f) : f(f) {}
  ~__Defer_Struct() { f(); }
};

template <typename F>
__Defer_Struct<F> operator+(F f) {
  return __Defer_Struct<F>(f);
}

*/

// JB's implementation:

#define CONCAT_INTERNAL(x,y) x##y
#define CONCAT(x,y) CONCAT_INTERNAL(x,y)
 
template<typename T>
struct ExitScope {
    T lambda;
    ExitScope(T lambda):lambda(lambda){}
    ~ExitScope(){lambda();}
    ExitScope(const ExitScope&);
  private:
    ExitScope& operator =(const ExitScope&);
};
 
class ExitScopeHelp {
  public:
    template<typename T>
        ExitScope<T> operator+(T t){ return t;}
};
 
#define defer const auto& CONCAT(defer__, __LINE__) = ExitScopeHelp() + [&]()
