#pragma once

#include "common.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <stdio.h>

// TODO:
// * Rewrite/clone implementation for separate chaining with proper element remove support (use Array<T> instead link lists) rOpenAddressingIssue
// * Implement subscription operator overload for inserting
// * Refactor to minimize template and malloc usage

template <typename K, typename T>
struct Hash_Table_Item {
  K key;
  T value;

  static Hash_Table_Item<K, T> REMOVED_ITEM;
};

template <typename K, typename T>
Hash_Table_Item<K, T> Hash_Table_Item<K, T>::REMOVED_ITEM = {(K)NULL, (T)NULL};

// TODO: Eliminate too much mallocs... and if possible, too much templates...
template <typename K, typename T>
static Hash_Table_Item<K, T> *hash_table_item_init(K key, T value) {
  Hash_Table_Item<K, T> *new_item = static_cast <Hash_Table_Item<K, T> *>(malloc(sizeof(Hash_Table_Item<K, T>)));

  new_item->key = key;
  new_item->value = value;

  return new_item;
}

template <typename T>
static Hash_Table_Item<char *, T> *hash_table_item_init(char *key, T value) {
  Hash_Table_Item<char *, T> *new_item = static_cast <Hash_Table_Item<char *, T> *>(malloc(sizeof(Hash_Table_Item<char *, T>)));

  new_item->key = strdup(key);
  if (new_item->key == nullptr)  return nullptr; 

  new_item->value = value;

  return new_item;
}


template <typename K, typename T>
static void hash_table_item_delete(Hash_Table_Item<K, T> *hash_table_item) {
  if (hash_table_item)  free(hash_table_item);
}

template <char *, typename T>
static void hash_table_item_delete(Hash_Table_Item<char *, T> *hash_table_item) {
  if (hash_table_item) {
    if (hash_table_item->key)    free(hash_table_item->key);
    free(hash_table_item);
  }
}

template <typename K>
s32 get_key_length(K key) { return 1; }

template <>
s32 get_key_length(char *key) { return strlen(key); }


template <typename K>
bool keys_equal(K first, K second, s32 key_length) { return first == second; }
bool keys_equal(char *first, char *second, s32 key_length) { return key_length != -1 ? strncmp(first, second, key_length) == 0 : false; }


template <typename K, typename T>
struct Hash_Table {
  s32 allocated_size = -1;
  s32 count = -1;
  s32 base_size = -1;

  Hash_Table_Item<K, T> ** items = nullptr;

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
u32 get_hash(K key, u32 key_length, u32 num_buckets, u32 attempt);

template <>
u32 get_hash(char *key, u32 key_length, u32 num_buckets, u32 attempt);

s32 next_prime(s32 number);

template <typename K, typename T>
void Hash_Table<K, T>::init(s32 base_size) {
  base_size = base_size > min_base_size ? base_size : min_base_size;
  allocated_size = next_prime(base_size); // Use next prime to avoid clustering, hence get better distribution
  items = static_cast <Hash_Table_Item<K, T> **>(calloc((size_t)allocated_size, sizeof(Hash_Table_Item<K, T> *)));
  count = 0;
}

template <typename K, typename T>
void Hash_Table<K, T>::deinit() {
  For_Count (allocated_size, i) {
    Hash_Table_Item<K, T> *item = items[i];
    if (item && item != &Hash_Table_Item<K, T>::REMOVED_ITEM)  free(item);
  }

  if (items)  free(items);

  base_size = -1;
  allocated_size = -1;
  items = nullptr;
  count = -1;
}

template <typename K, typename T>
void Hash_Table<K, T>::insert(K key, T value) {
  if (items == nullptr || allocated_size == -1) {
    init();
  }

  u32 load = count * 100 / allocated_size;
  if (load > 70) {
    if (verbose) {
      printf("WARNING! Hash_Table has to increase in size. Consider preallocating bigger size.");
    }
    resize_up();
  }

  auto item = hash_table_item_init(key, value);
  auto key_length = get_key_length<K>(key);
  auto index = get_hash<K>(key, key_length, allocated_size, 0);
  auto candidate_place = items[index];

  u32 attempt = 1;
  while (candidate_place != nullptr && candidate_place != &Hash_Table_Item<K, T>::REMOVED_ITEM) {
    if (keys_equal(candidate_place->key, key, key_length)) {
      hash_table_item_delete(candidate_place);
      items[index] = item;
      return;
    }
    index = get_hash<K>(key, key_length, allocated_size, attempt++);
    candidate_place = items[index];
  }

  items[index] = item;
  count++;
}

// :OpenAddressingIssue
// @Bug: Serious error in how removing routine handles collisions. See bottom comment for details.
template <typename K, typename T>
void Hash_Table<K, T>::remove(K key) {
  // @Speed: Avoid reallocating especially in case of preallocation in fast buffer
  // u32 load = count * 100 / allocated_size;
  // if (load < 10) {
  //   resize_down();
  // }

  if (verbose) {
    printf("WARNING! Implementation doesn't handles collisions with removed items correctly!\n"
           "         It strongly advised, not to use remove function.\n");
  }

  auto key_length = get_key_length<K>(key);
  auto index = get_hash<K>(key, key_length, allocated_size, 0);
  auto item = items[index];

  u32 attempt = 1;
  while (item != nullptr) {
    if (item != &Hash_Table_Item<K, T>::REMOVED_ITEM) {
      if (keys_equal(item->key, key, key_length)) {
        hash_table_item_delete(item);

        items[index] = &Hash_Table_Item<K, T>::REMOVED_ITEM;
        count--;
        return;
      }
    }
    index = get_hash<K>(key, key_length, allocated_size, attempt++);
    item = items[index];
  }  
}

template <typename K, typename T>
T * Hash_Table<K, T>::search(K key) {
  auto key_length = get_key_length<K>(key);
  auto index = get_hash<K>(key, key_length, allocated_size, 0);
  auto item = items[index];

  u32 attempt = 1;
  while (item != nullptr) {
    if (item != &Hash_Table_Item<K, T>::REMOVED_ITEM) {
      if (keys_equal(item->key, key, key_length)) {
        return &(item->value);
      }
    }
    index = get_hash<K>(key, key_length, allocated_size, attempt++);
    item = items[index];
  }
  
  return nullptr;
}

template <typename K, typename T>
void Hash_Table<K, T>::resize(s32 new_base_size) {
  if (new_base_size < min_base_size)  return;

  Hash_Table<K, T> new_ht;
  new_ht.init(new_base_size);
  
  For_Count (this->allocated_size, i) {
    auto item = this->items[i];
    if (item != nullptr && item != &Hash_Table_Item<K, T>::REMOVED_ITEM) {
      new_ht.insert(item->key, item->value);
    }
  }

  this->base_size = new_ht.base_size;
  this->count = new_ht.count;

  // TODO: Make swaps hardware-atomic
  SWAP(this->allocated_size, new_ht.allocated_size);
  SWAP(this->items, new_ht.items);

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

#define HASH_SEED_1 41
#define HASH_SEED_2 2777

template <typename K>
u32 get_hash(K key, u32 key_length, u32 num_buckets, u32 attempt) {
  u32 hash_1 = MurmurHash2(&key, key_length, HASH_SEED_1);
  u32 hash_2 = MurmurHash2(&key, key_length, HASH_SEED_2);
  return (hash_1 + (hash_2 + 1) * attempt) % num_buckets;
}

template <>
u32 get_hash(char *key, u32 key_length, u32 num_buckets, u32 attempt) {
  u32 hash_1 = MurmurHash2(key, key_length, HASH_SEED_1);
  u32 hash_2 = MurmurHash2(key, key_length, HASH_SEED_2);
  return (hash_1 + (hash_2 + 1) * attempt) % num_buckets;
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

/*
    :OpenAddressingIssue
    @Bug: In following case, when "A" and "B" collide on attempt=0

    insert("A", <value of A>)
    insert("B", <value of B>)
    remove("A")
    insert("B", <new value>)
    
    First inserted <value of B> would be leaked untill remove wasn't
    called on "B".
    
    This issue should generilize on different values of attempt

    TODO: Fix this by checking collision case in the remove function
          and restructure table accordingly

          
    More complicated case: suppose "A" collides with "B" on attempt=0 and "B" with "C" on attempt=1

    insert("A", <value of A>)
    insert("C", <value of C>)
    insert("B", <value of B>)
    remove("A")

    So, the path to finding "B" should be somewhat like this

    K1 < REMOVED ("A") -+
    K2    +-------------+
    K3    v 
    K4 < "C" --+
    K5         |
    K6 < "B" <-+
    K7

    But on the next insert of "B" the new element will be put in K1

    In order to fix the issue, we should restructure table in the remove function,
    replacing REMOVED "A" with next elements in collision chain

    Considering that two different keys, like "A" and "B", given their collision
    on some attempt value, not gurantiee to collide on another value of attempt,
    the only viable solution is to check all keys in the table for current attempt
    number to find possible collided key.

    In case if that key's been found, replace the removed entry with it. And table
    will be restructured following way:

    K1 < "B"
    K2
    K3
    K4 < "C"
    K5
    K6 < nullptr
    K7

    But what if something was dependet on "B" in K6? That means that it colides with
    "B" on some unknown attempt value. So, it should be properly deleted with the same
    collision check. And that's how we get into recursion problem.

    I guess, that's the point where the most correct solution is to rewrite whole table
    to separate chaining method of collision resolution. Or just not using remove for
    the sake of simplicity.

 */
