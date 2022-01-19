#pragma once

#include <stdlib.h>

// TODO:
// * Nested For loops
// * Fix wrong deinit behavior when one array assigned to another (if deinit implemented with free() in it)

#define For(arr) \
  int __unique_i(__i_, __LINE__) = 0; \
  if((arr).count > 0) \
  for (auto it = (arr).data[__unique_i(__i_, __LINE__)]; __unique_i(__i_, __LINE__) < (arr).count; __unique_i(__i_, __LINE__)++, it = (arr).data[__unique_i(__i_, __LINE__)])

#define __unique_i(x, y) __for_do_concat(x, y)
#define __for_do_concat(x, y) x ## y

// #define For_ptr(arr) for (auto it = (arr).data; (it - (arr).data) < (arr).count; it++)
// #define For(arr) for (auto __i = 0, it = (arr).data[__i]; __i < (arr).count; __i++, it = (arr).data[__i])
// #define For(arr) for (auto it = (arr).data; (it - (arr).data) < (arr).count; it++)

// #define Array_Foreach(l, x) \
//   for (int __i = 0; (__i < (l)->count) ? ((x) = (l)->data[__i]), true : false; __i++) \
                                                                                                                  
// #define Array_Foreach_Pointer(l, x) \
//   for (int __i = 0; (__i < (l)->count) ? ((x) = &((l)->data[__i])), true : false; __i++) \


template <typename T>
struct Array {
  size_t count = 0;
  T * data = NULL;

  void init(size_t count = 0);
  void init(T array[], size_t count);
  void deinit(); // @Note: WARNING! Frees only data allocated by Array unrecursively!

  void reset();

  void add(const T & element);
  Array<T> join(Array<T> array);

  inline T & get(size_t index);
  inline void set(size_t index, T value);

  T & remove(size_t index);

  size_t get_allocated_size() { return allocated_size; }

  inline T & operator[] (size_t index) { return get(index); }

  inline T back()  { return get(count - 1); }
  inline T front() { return get(0);         }

  inline T pop();

private:
  inline T * allocate(size_t size);
  inline T * reallocate(size_t new_size);

  size_t allocated_size = 0;
  constexpr static const size_t grow_factor = 2;
  constexpr static const size_t minimal_size = 8;
};

#include <string.h>
#include <assert.h>

template<typename T>
void Array<T>::init(size_t count) {
  this->data = allocate(count);
  this->count = count;
  memset(data, 0, count);
}

template<typename T>
void Array<T>::init(T * array, size_t count) {
  this->data = allocate(count);
  this->count = count;

  memcpy(data, array, sizeof(T)*count);
}

template<typename T>
void Array<T>::deinit() {
  if (data) {
    free(data);
    allocated_size = 0;
  }
}

template<typename T>
void Array<T>::reset() {
  memset(data, 0, count);
  count = 0;
}

template<typename T>
inline T * Array<T>::allocate(size_t requested_size) {
  if (requested_size <= minimal_size)
    allocated_size = minimal_size;
  else
    allocated_size = requested_size;

  return (T *)malloc(sizeof(T)*allocated_size);
}

template<typename T>
inline T * Array<T>::reallocate(size_t new_size) {
  if (new_size <= allocated_size) return data;
    
  allocated_size = new_size;
  return (T *)realloc(data, sizeof(T)*allocated_size);
}

template<typename T>
void Array<T>::add(const T & element) {
  if(data == NULL) {
    data = allocate(minimal_size);
  } else if (count == allocated_size) {
    data = reallocate(allocated_size * grow_factor);
  }

  count += 1;
  data[count-1] = element;
}

template<typename T>
T Array<T>::pop() {
  assert(count > 0);
  count -= 1;
  return data[count];
}

template<typename T>
T & Array<T>::remove(size_t index) {
  assert(0 && "Unimplemented");
}

template<typename T>
Array<T> Array<T>::join(Array<T> array) {
  assert(0 && "Unimplemented");
}

template <typename T>
inline void Array<T>::set(size_t index, T value) {
  assert(0 <= index && index < count && "Attempt to index array out of its bounds");
  data[index] = value;
}

template <typename T>
inline T & Array<T>::get(size_t index) {
  assert(0 <= index && index < count && "Attempt to index array out of its bounds");
  return data[index];
}
