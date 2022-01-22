#pragma once

#include <stdlib.h>

// TODO:
// * Fix wrong deinit behavior when one array assigned to another (cycling reference, I guess?) (if deinit implemented with free() in it)
// * Add unordered remove

#define For(arr) \
  int __unique_i(__i_, __LINE__) = 0; \
  if((arr).count > 0) \
    for (auto it = (arr).data[__unique_i(__i_, __LINE__)]; __unique_i(__i_, __LINE__) < (arr).count; __unique_i(__i_, __LINE__)++, it = (arr).data[__unique_i(__i_, __LINE__)])

#define For_Pointer(arr) \
  int __unique_i(__i_, __LINE__) = 0; \
  if((arr).count > 0) \
    for (auto it = &((arr).data[__unique_i(__i_, __LINE__)]); __unique_i(__i_, __LINE__) < (arr).count; __unique_i(__i_, __LINE__)++, it = &((arr).data[__unique_i(__i_, __LINE__)]))

// Version for using in nested loops
#define For_it(arr, x) \
  int __unique_i(__i_, __LINE__) = 0; \
  if((arr).count > 0) \
    for (auto (x) = (arr).data[__unique_i(__i_, __LINE__)]; __unique_i(__i_, __LINE__) < (arr).count; __unique_i(__i_, __LINE__)++, (x) = (arr).data[__unique_i(__i_, __LINE__)])

#define For_it_Pointer(arr, x) \
  int __unique_i(__i_, __LINE__) = 0; \
  if((arr).count > 0) \
    for (auto (x) = &((arr).data[__unique_i(__i_, __LINE__)]); __unique_i(__i_, __LINE__) < (arr).count; __unique_i(__i_, __LINE__)++, (x) = &((arr).data[__unique_i(__i_, __LINE__)]))


#define __unique_i(x, y) __do_concat(x, y)
#define __do_concat(x, y) x ## y

// JB's for reference:
// #define Array_Foreach(l, x) \
//   for (int __i = 0; (__i < (l)->count) ? ((x) = (l)->data[__i]), true : false; __i++) \
                                                                                                                  
// #define Array_Foreach_Pointer(l, x) \
//   for (int __i = 0; (__i < (l)->count) ? ((x) = &((l)->data[__i])), true : false; __i++) \


template <typename T>
struct Array {
  int count = -1;
  T * data = nullptr;

  void init(int count = 0);
  void init(T array[], int count);
  void deinit(); // @Note: WARNING! Frees only data allocated by Array unrecursively!

  void reset();

  void add(const T & value);
  Array<T> join(Array<T> array); // @Unimplemented

  inline T & get(int index);
  inline void set(int index, T value);

  T remove(int index); // @Unimplemented
  T remove_unordered(int index);

  inline T & operator[] (int index) { return get(index); }

  inline T & back()  { return get(count - 1); }
  inline T & front() { return get(0);         }
  inline T & pop();

  T * find(const T & value); // TODO: Add custom predicate
  int find_index(const T & value); // TODO: Add custom predicate

  int get_allocated_size() { return allocated_size; } // Meh...

private:
  inline T * allocate(int size);
  inline T * reallocate(int new_size);

  int allocated_size = -1;
  constexpr static const int grow_factor = 2;
  constexpr static const int minimal_size = 8;
};

#include <string.h>
#include <assert.h>

template<typename T>
void Array<T>::init(int count) {
  this->data = allocate(count);
  this->count = count;
  memset(data, 0, count);
}

template<typename T>
void Array<T>::init(T * array, int count) {
  this->data = allocate(count);
  this->count = count;

  memcpy(data, array, sizeof(T)*count);
}

template<typename T>
void Array<T>::deinit() {
  if (data) {
    free(data);
    allocated_size = -1;
    count = -1;
  }
}

template<typename T>
void Array<T>::reset() {
  memset(data, 0, count);
  count = 0;
}

template<typename T>
inline T * Array<T>::allocate(int requested_size) {
  if (requested_size <= minimal_size)
    allocated_size = minimal_size;
  else
    allocated_size = requested_size;

  return static_cast <T *>(malloc(sizeof(T)*allocated_size));
}

template<typename T>
inline T * Array<T>::reallocate(int new_size) {
  if (new_size <= allocated_size) return data;
    
  allocated_size = new_size;
  return static_cast <T *>(realloc(data, sizeof(T)*allocated_size));
}

template<typename T>
void Array<T>::add(const T & value) {
  if(data == nullptr) {
    init();
  } else if (count == allocated_size) {
    data = reallocate(allocated_size * grow_factor);
  }

  count += 1;
  data[count-1] = value;
}

template<typename T>
T & Array<T>::pop() {
  assert(count > 0);
  count -= 1;
  return data[count];
}

template<typename T>
T Array<T>::remove(int index) {
  assert(0 && "Unimplemented");
}

template<typename T>
T Array<T>::remove_unordered(int index) {
  T removed_value = data[index];
  data[index] = data[count - 1];
  data[count - 1] = removed_value;

  count -= 1;

  return removed_value;
}

template<typename T>
Array<T> Array<T>::join(Array<T> array) {
  assert(0 && "Unimplemented");
}

template <typename T>
inline void Array<T>::set(int index, T value) {
  assert(0 <= index && index < count && "Attempt to index array out of its bounds");
  data[index] = value;
}

template <typename T>
inline T & Array<T>::get(int index) {
  assert(0 <= index && index < count && "Attempt to index array out of its bounds");
  return data[index];
}

template <typename T>
T * Array<T>::find(const T & value) {
  for (int i = 0; i < count; i++) {
    if (data[i] == value) {
      return &(data[i]);
    }
  }
  return nullptr;
}

template <typename T>
int Array<T>::find_index(const T & value) {
  for (int i = 0; i < count; i++) {
    if (data[i] == value) {
      return i;
    }
  }
  return -1;
}
