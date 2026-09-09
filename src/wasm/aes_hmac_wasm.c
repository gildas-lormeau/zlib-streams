/* The engine behind the AES entries of zip.js: AES-CTR with the WinZip counter, a 16-byte
   little-endian integer starting at 1, and HMAC-SHA1 over the ciphertext. Forward-only T-table
   AES, the aes_core.c layout. One context per stream: init() takes the cipher key and the
   authentication key, process() works in place and takes whole 16-byte blocks except for the last
   call, end() writes the 20-byte MAC when given an output pointer and frees the context. */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define BLOCK_LENGTH 16
#define SHA1_BLOCK_LENGTH 64
#define SHA1_DIGEST_LENGTH 20
#define ROUND_KEYS_LENGTH 60

struct aes_hmac_ctx {
  uint32_t round_keys[ROUND_KEYS_LENGTH];
  int rounds;
  uint8_t counter[BLOCK_LENGTH];
  uint32_t h[5];
  uint8_t block[SHA1_BLOCK_LENGTH];
  unsigned block_length;
  uint64_t total_length;
  uint8_t opad[SHA1_BLOCK_LENGTH];
};

static uint32_t Te0[256], Te1[256], Te2[256], Te3[256];
static uint8_t sbox[256];
static int tables_ready;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }
static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
static uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
static uint32_t load_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void store_be32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

static void init_tables(void) {
  uint8_t p = 1, q = 1;
  do {
    p = (uint8_t)(p ^ (p << 1) ^ ((p & 0x80) ? 0x1b : 0));
    q ^= (uint8_t)(q << 1);
    q ^= (uint8_t)(q << 2);
    q ^= (uint8_t)(q << 4);
    q ^= (q & 0x80) ? 0x09 : 0;
    uint8_t x = (uint8_t)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^ ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
    sbox[p] = (uint8_t)(x ^ 0x63);
  } while (p != 1);
  sbox[0] = 0x63;
  for (int i = 0; i < 256; i++) {
    uint8_t s = sbox[i], s2 = xtime(s), s3 = (uint8_t)(s2 ^ s);
    uint32_t t = ((uint32_t)s2 << 24) | ((uint32_t)s << 16) | ((uint32_t)s << 8) | s3;
    Te0[i] = t;
    Te1[i] = rotr(t, 8);
    Te2[i] = rotr(t, 16);
    Te3[i] = rotr(t, 24);
  }
  tables_ready = 1;
}

static uint32_t sub_word(uint32_t w) {
  return ((uint32_t)sbox[w >> 24] << 24) | ((uint32_t)sbox[(w >> 16) & 255] << 16) |
         ((uint32_t)sbox[(w >> 8) & 255] << 8) | sbox[w & 255];
}

static void expand_key(struct aes_hmac_ctx *c, const uint8_t *key, unsigned key_length) {
  int nk = (int)key_length / 4;
  c->rounds = nk + 6;
  int total = 4 * (c->rounds + 1);
  uint8_t rcon = 1;
  for (int i = 0; i < nk; i++) {
    c->round_keys[i] = load_be32(key + 4 * i);
  }
  for (int i = nk; i < total; i++) {
    uint32_t t = c->round_keys[i - 1];
    if (i % nk == 0) {
      t = sub_word(rotl(t, 8)) ^ ((uint32_t)rcon << 24);
      rcon = xtime(rcon);
    } else if (nk > 6 && i % nk == 4) {
      t = sub_word(t);
    }
    c->round_keys[i] = c->round_keys[i - nk] ^ t;
  }
}

static void encrypt_block(const struct aes_hmac_ctx *c, const uint8_t in[BLOCK_LENGTH], uint8_t out[BLOCK_LENGTH]) {
  const uint32_t *rk = c->round_keys;
  uint32_t s0 = load_be32(in) ^ rk[0], s1 = load_be32(in + 4) ^ rk[1];
  uint32_t s2 = load_be32(in + 8) ^ rk[2], s3 = load_be32(in + 12) ^ rk[3];
  uint32_t t0, t1, t2, t3;
  rk += 4;
  for (int r = 1; r < c->rounds; r++) {
    t0 = Te0[s0 >> 24] ^ Te1[(s1 >> 16) & 255] ^ Te2[(s2 >> 8) & 255] ^ Te3[s3 & 255] ^ rk[0];
    t1 = Te0[s1 >> 24] ^ Te1[(s2 >> 16) & 255] ^ Te2[(s3 >> 8) & 255] ^ Te3[s0 & 255] ^ rk[1];
    t2 = Te0[s2 >> 24] ^ Te1[(s3 >> 16) & 255] ^ Te2[(s0 >> 8) & 255] ^ Te3[s1 & 255] ^ rk[2];
    t3 = Te0[s3 >> 24] ^ Te1[(s0 >> 16) & 255] ^ Te2[(s1 >> 8) & 255] ^ Te3[s2 & 255] ^ rk[3];
    s0 = t0;
    s1 = t1;
    s2 = t2;
    s3 = t3;
    rk += 4;
  }
  store_be32(out, (((uint32_t)sbox[s0 >> 24] << 24) | ((uint32_t)sbox[(s1 >> 16) & 255] << 16) | ((uint32_t)sbox[(s2 >> 8) & 255] << 8) | sbox[s3 & 255]) ^ rk[0]);
  store_be32(out + 4, (((uint32_t)sbox[s1 >> 24] << 24) | ((uint32_t)sbox[(s2 >> 16) & 255] << 16) | ((uint32_t)sbox[(s3 >> 8) & 255] << 8) | sbox[s0 & 255]) ^ rk[1]);
  store_be32(out + 8, (((uint32_t)sbox[s2 >> 24] << 24) | ((uint32_t)sbox[(s3 >> 16) & 255] << 16) | ((uint32_t)sbox[(s0 >> 8) & 255] << 8) | sbox[s1 & 255]) ^ rk[2]);
  store_be32(out + 12, (((uint32_t)sbox[s3 >> 24] << 24) | ((uint32_t)sbox[(s0 >> 16) & 255] << 16) | ((uint32_t)sbox[(s1 >> 8) & 255] << 8) | sbox[s2 & 255]) ^ rk[3]);
}

static void sha1_block(struct aes_hmac_ctx *c, const uint8_t *p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) {
    w[i] = load_be32(p + 4 * i);
  }
  for (int i = 16; i < 80; i++) {
    w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], t;
  for (int i = 0; i < 20; i++) {
    t = rotl(a, 5) + ((b & cc) | (~b & d)) + e + 0x5A827999 + w[i];
    e = d;
    d = cc;
    cc = rotl(b, 30);
    b = a;
    a = t;
  }
  for (int i = 20; i < 40; i++) {
    t = rotl(a, 5) + (b ^ cc ^ d) + e + 0x6ED9EBA1 + w[i];
    e = d;
    d = cc;
    cc = rotl(b, 30);
    b = a;
    a = t;
  }
  for (int i = 40; i < 60; i++) {
    t = rotl(a, 5) + ((b & cc) | (b & d) | (cc & d)) + e + 0x8F1BBCDC + w[i];
    e = d;
    d = cc;
    cc = rotl(b, 30);
    b = a;
    a = t;
  }
  for (int i = 60; i < 80; i++) {
    t = rotl(a, 5) + (b ^ cc ^ d) + e + 0xCA62C1D6 + w[i];
    e = d;
    d = cc;
    cc = rotl(b, 30);
    b = a;
    a = t;
  }
  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += cc;
  c->h[3] += d;
  c->h[4] += e;
}

static void sha1_init(struct aes_hmac_ctx *c) {
  c->h[0] = 0x67452301;
  c->h[1] = 0xEFCDAB89;
  c->h[2] = 0x98BADCFE;
  c->h[3] = 0x10325476;
  c->h[4] = 0xC3D2E1F0;
  c->block_length = 0;
  c->total_length = 0;
}

static void sha1_update(struct aes_hmac_ctx *c, const uint8_t *p, unsigned length) {
  c->total_length += length;
  if (c->block_length) {
    unsigned n = SHA1_BLOCK_LENGTH - c->block_length;
    if (n > length) {
      n = length;
    }
    memcpy(c->block + c->block_length, p, n);
    c->block_length += n;
    p += n;
    length -= n;
    if (c->block_length == SHA1_BLOCK_LENGTH) {
      sha1_block(c, c->block);
      c->block_length = 0;
    }
  }
  while (length >= SHA1_BLOCK_LENGTH) {
    sha1_block(c, p);
    p += SHA1_BLOCK_LENGTH;
    length -= SHA1_BLOCK_LENGTH;
  }
  if (length) {
    memcpy(c->block, p, length);
    c->block_length = length;
  }
}

static void sha1_final(struct aes_hmac_ctx *c, uint8_t out[SHA1_DIGEST_LENGTH]) {
  uint64_t bits = c->total_length * 8;
  uint8_t padding[SHA1_BLOCK_LENGTH + 8] = { 0x80 };
  unsigned padding_length = (c->block_length < 56 ? 56 : 120) - c->block_length;
  for (int i = 0; i < 8; i++) {
    padding[padding_length + i] = (uint8_t)(bits >> (56 - 8 * i));
  }
  sha1_update(c, padding, padding_length + 8);
  for (int i = 0; i < 5; i++) {
    store_be32(out + 4 * i, c->h[i]);
  }
}

static void hmac_init(struct aes_hmac_ctx *c, const uint8_t *key, unsigned length) {
  uint8_t ipad[SHA1_BLOCK_LENGTH], hashed_key[SHA1_DIGEST_LENGTH];
  if (length > SHA1_BLOCK_LENGTH) {
    sha1_init(c);
    sha1_update(c, key, length);
    sha1_final(c, hashed_key);
    key = hashed_key;
    length = SHA1_DIGEST_LENGTH;
  }
  for (unsigned i = 0; i < SHA1_BLOCK_LENGTH; i++) {
    uint8_t k = i < length ? key[i] : 0;
    ipad[i] = (uint8_t)(k ^ 0x36);
    c->opad[i] = (uint8_t)(k ^ 0x5c);
  }
  sha1_init(c);
  sha1_update(c, ipad, SHA1_BLOCK_LENGTH);
}

static void hmac_final(struct aes_hmac_ctx *c, uint8_t out[SHA1_DIGEST_LENGTH]) {
  uint8_t inner[SHA1_DIGEST_LENGTH];
  sha1_final(c, inner);
  sha1_init(c);
  sha1_update(c, c->opad, SHA1_BLOCK_LENGTH);
  sha1_update(c, inner, SHA1_DIGEST_LENGTH);
  sha1_final(c, out);
}

unsigned aes_hmac_new(void) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)malloc(sizeof(*c));
  if (!c) {
    return 0;
  }
  memset(c, 0, sizeof(*c));
  return (unsigned)(uintptr_t)c;
}

int aes_hmac_init(unsigned ctx, unsigned key_ptr, unsigned key_length, unsigned auth_ptr, unsigned auth_length) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)(uintptr_t)ctx;
  if (!c || (key_length != 16 && key_length != 24 && key_length != 32)) {
    return -1;
  }
  if (!tables_ready) {
    init_tables();
  }
  expand_key(c, (const uint8_t *)(uintptr_t)key_ptr, key_length);
  memset(c->counter, 0, BLOCK_LENGTH);
  hmac_init(c, (const uint8_t *)(uintptr_t)auth_ptr, auth_length);
  return 0;
}

void aes_hmac_process(unsigned ctx, unsigned ptr, unsigned length, int decrypt) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)(uintptr_t)ctx;
  uint8_t *p = (uint8_t *)(uintptr_t)ptr;
  uint32_t keystream[4], words[4];
  if (!c) {
    return;
  }
  if (decrypt) {
    sha1_update(c, p, length);
  }
  for (unsigned i = 0; i < length; i += BLOCK_LENGTH) {
    for (int j = 0; j < BLOCK_LENGTH; j++) {
      if (++c->counter[j]) {
        break;
      }
    }
    encrypt_block(c, c->counter, (uint8_t *)keystream);
    if (length - i >= BLOCK_LENGTH) {
      memcpy(words, p + i, BLOCK_LENGTH);
      words[0] ^= keystream[0];
      words[1] ^= keystream[1];
      words[2] ^= keystream[2];
      words[3] ^= keystream[3];
      memcpy(p + i, words, BLOCK_LENGTH);
    } else {
      const uint8_t *k = (const uint8_t *)keystream;
      for (unsigned j = 0; j < length - i; j++) {
        p[i + j] ^= k[j];
      }
    }
  }
  if (!decrypt) {
    sha1_update(c, p, length);
  }
}

void aes_hmac_end(unsigned ctx, unsigned out_ptr) {
  struct aes_hmac_ctx *c = (struct aes_hmac_ctx *)(uintptr_t)ctx;
  if (!c) {
    return;
  }
  if (out_ptr) {
    hmac_final(c, (uint8_t *)(uintptr_t)out_ptr);
  }
  free(c);
}
