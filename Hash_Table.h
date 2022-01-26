#pragma once

#include "common.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <stdio.h>

#include "Array.h"

// TODO:
// * Implement subscription operator overload for inserting
// * Refactor to minimize template and malloc usage

template <typename K, typename T>
struct Hash_Table_Item {
  K key;
  T value;
};

template <typename K, typename T>
static Hash_Table_Item<K, T> hash_table_item_init(K key, T value) {
  return (Hash_Table_Item<K, T>){key, value};
}

template <typename T>
static Hash_Table_Item<char *, T> hash_table_item_init(char *key, T value) {
  Hash_Table_Item<char *, T> new_item;

  new_item.key = strdup(key);
  if (new_item.key == nullptr)  return Hash_Table_Item<char *, T>{nullptr, (T)NULL}; 

  new_item.value = value;

  return new_item;
}


template <typename K, typename T>
static void hash_table_item_delete(Hash_Table_Item<K, T> hash_table_item) { }

template <char *, typename T>
static void hash_table_item_delete(Hash_Table_Item<char *, T> hash_table_item) {
  if (hash_table_item.key)  free(hash_table_item.key);
}

template <typename K>
s32 get_key_length(K key) { return 1; }

template <>
s32 get_key_length(char *key) { return strlen(key); }

template <typename K>
bool keys_equal(K first, K second, s32 key_length) { return first == second; }

template <>
bool keys_equal(char *first, char *second, s32 key_length) { return key_length != -1 ? strncmp(first, second, key_length) == 0 : false; }


template <typename K, typename T>
struct Hash_Table {
  s32 allocated_size = -1;
  s32 count = -1;
  s32 base_size = -1;

  Array<Hash_Table_Item<K, T>> * buckets = nullptr;

  bool verbose = true;

  constexpr static const s32 min_base_size = 50;

  void init(s32 base_size = 0);
  void deinit();

  inline T * operator [](K key) { return search(key); }

  inline bool exists(K key) { return search(key) != nullptr; }

  void insert(K key, T value);
  void remove(K key);
  T * search(K key);

private:
  void resize(s32 new_base_size);

  inline void resize_up() {
    s32 new_base_size = allocated_size * 2;
    resize(new_base_size);
  }

  // @Speed: It discouraged to use this function. Better preallocate the right amount of memory.
  inline void resize_down() {
    s32 new_base_size = allocated_size / 2;
    resize(new_base_size);
  }
};


#define SWAP(a, b) ({auto __tmp = (a); (a) = (b); (b) = (__tmp);})

template <typename K>
u32 get_hash(K key, u32 key_length, u32 num_buckets);

template <>
u32 get_hash(char *key, u32 key_length, u32 num_buckets);

s32 next_prime(s32 number);

template <typename K, typename T>
void Hash_Table<K, T>::init(s32 base_size) {
  base_size = base_size > min_base_size ? base_size : min_base_size;
  allocated_size = next_prime(base_size); // Use next prime to avoid clustering, hence get better distribution
  buckets = static_cast <Array<Hash_Table_Item<K, T>> *>(calloc((size_t)allocated_size, sizeof(Array<Hash_Table_Item<K, T>>)));

  For_Count (allocated_size, i) {
    buckets[i].minimal_size = 1;
    buckets[i].grow_factor = 2;
    buckets[i].init();
  }

  count = 0;
}

template <typename K, typename T>
void Hash_Table<K, T>::deinit() {
  For_Count (allocated_size, i) {
    For_Pointer (buckets[i]) {
      hash_table_item_delete(*it);
    }
    buckets[i].deinit();
  }

  if (buckets)  free(buckets);

  base_size = -1;
  allocated_size = -1;
  buckets = nullptr;
  count = -1;
}

template <typename K, typename T>
void Hash_Table<K, T>::insert(K key, T value) {
  if (buckets == nullptr || allocated_size == -1) {
    init();
  }

  u32 load = count * 100 / allocated_size;
  if (load >= 100) {
    if (verbose) {
      printf("WARNING! Hash_Table has to increase in size. Consider preallocating bigger size.");
    }
    resize_up();
  }

  auto item = hash_table_item_init(key, value);
  auto key_length = get_key_length<K>(key);
  auto index = get_hash<K>(key, key_length, allocated_size);

  bool item_exists = false;
  if (buckets[index].count > 0) {
    For_Pointer (buckets[index]) {
      if (keys_equal(it->key, key, key_length)) {
        buckets[index].remove_unordered(it - buckets[index].data);
        hash_table_item_delete(*it);

        buckets[index].add(item);
        item_exists = true;
        break;
      }
    }
  }

  if (!item_exists) {
    buckets[index].add(item);
    count++;
  }
}

template <typename K, typename T>
void Hash_Table<K, T>::remove(K key) {
  // @Speed: Avoid reallocating especially in case of preallocation in fast buffer
  u32 load = count * 100 / allocated_size;
  if (load < 50) {
    resize_down();
  }

  auto key_length = get_key_length<K>(key);
  auto index = get_hash<K>(key, key_length, allocated_size);

  For_Pointer (buckets[index]) {
    if (keys_equal(it->key, key, key_length)) {
      buckets[index].remove_unordered(it - buckets[index].data);
      hash_table_item_delete(*it);
      count--;

      return;
    }
  }
}

template <typename K, typename T>
T * Hash_Table<K, T>::search(K key) {
  auto key_length = get_key_length<K>(key);
  auto index = get_hash<K>(key, key_length, allocated_size);

  For_Pointer (buckets[index]) {
    if (keys_equal(it->key, key, key_length)) {
      return &(it->value);
    }
  }
  
  return nullptr;
}

template <typename K, typename T>
void Hash_Table<K, T>::resize(s32 new_base_size) {
  if (new_base_size < min_base_size)  return;

  Hash_Table<K, T> new_ht;
  new_ht.init(new_base_size);
  
  For_Count (this->allocated_size, i) {
    For (this->buckets[i]) {
      new_ht.insert(it.key, it.value);
    }
  }

  this->base_size = new_ht.base_size;
  this->count = new_ht.count;

  // TODO: Make swaps hardware-atomic
  SWAP(this->allocated_size, new_ht.allocated_size);
  SWAP(this->buckets, new_ht.buckets);

  new_ht.deinit();
}

///////////////////////////////////////
//
//    Hashing
//

// MurmurHash2, by Austin Appleby
unsigned int MurmurHash2(const void * key, int len, unsigned int seed) {
	// 'm' and 'r' are mixing constants generated offline.
	// They're not really 'magic', they just happen to work well.

	const unsigned int m = 0x5bd1e995;
	const int r = 24;

	// Initialize the hash to a 'random' value

	unsigned int h = seed ^ len;

	// Mix 4 bytes at a time into the hash

	const unsigned char * data = (const unsigned char *)key;

	while(len >= 4) {
		unsigned int k = *(unsigned int *)data;

		k *= m; 
		k ^= k >> r; 
		k *= m; 
		
		h *= m; 
		h ^= k;

		data += 4;
		len -= 4;
	}
	
	// Handle the last few bytes of the input array

	switch(len) {
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
	        h *= m;
	};

	// Do a few final mixes of the hash to ensure the last few
	// bytes are well-incorporated.

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
} 

#define HASH_SEED 41

template <typename K>
u32 get_hash(K key, u32 key_length, u32 num_buckets) {
  return MurmurHash2(&key, key_length, HASH_SEED) % num_buckets;
}

template <>
u32 get_hash(char *key, u32 key_length, u32 num_buckets) {
  return MurmurHash2(key, key_length, HASH_SEED) % num_buckets;
}

///////////////////////////////////////
//
//    Prime number finding
//

s32 is_prime(s32 number) {
  if (number % 2 == 0)  return 0;
  if (number == 2)  return 1;
  if (number < 2)   return -1;

  auto root = floor(sqrt(number));
  for (s32 i = 3; i <= root; i += 2) {
    if (number % i == 0)  return 0;
  }

  return 1;
}

s32 next_prime(s32 number) {
  s32 next_number = number;
  while (is_prime(next_number) != 1) {
    next_number++;
  }
  return next_number;
}
