#include "Hash_Table.h"

template <>
u32 get_hash(char *key, u32 key_length, u32 num_buckets) {
  return MurmurHash2(key, key_length, HASH_SEED) % num_buckets;
}

template <>
s32 get_key_length(char *key) { return strlen(key); }


template <>
bool keys_equal(char *first, char *second, s32 key_length) { return key_length != -1 ? strncmp(first, second, key_length) == 0 : false; }


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
