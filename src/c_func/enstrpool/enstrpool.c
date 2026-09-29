/* enstr 字符串池运行时：整块匿名内存 + 下标访问 + 按需解密。
 *
 * 由 EncryptoStrPass 以 bitcode 形式链入目标模块（Android/ELF 优先）。
 * 初始化只把密文原样拷入匿名页——内存中不存在全量明文；每个字符串
 * 首次被 __vllvm_enstr_get 取用时才在池内解密（isdec 标记），反 dump
 * 只能拿到被访问过的片段。表等链接期常量由 pass 在链接后填充。
 * mmap 只声明不定义，最终链接时由目标平台 libc 提供。 */

#include <stdatomic.h>
#include <stdint.h>

/* 自行声明 mmap，避免依赖宿主头文件。参数用固定位宽：位码按 LP64
 * ABI 生成，Android/ELF 目标的 size_t/off_t 都是 64 位。 */
void *mmap(void *addr, uint64_t length, int prot, int flags, int fd,
           int64_t offset);

/* 密文源：全部密文按池布局平铺（表项 offset 即此处下标），pass 侧在
 * 位码链入后填充；命名带 __vllvm 前缀避免与用户符号撞名导致链接回退。 */
uint8_t *__vllvm_enstr_src = 0;
struct vllvm_enstr_desc {
  uint64_t offset;    /* 池内/密文源内字节下标 */
  uint32_t size;      /* 含结尾 NUL 的字节数 */
  uint8_t key;        /* 单字节密钥种子，按位置派生密钥流 */
  uint8_t isdec;      /* 已解密标记；表因此必须可写 */
};

extern struct vllvm_enstr_desc *__vllvm_enstr_table;
extern const uint64_t __vllvm_enstr_count;
extern const uint64_t __vllvm_enstr_pool_size;
/* MAP_PRIVATE|MAP_ANON 的值随平台不同（Android/ELF 0x22，Darwin 0x1002），
 * 由 pass 按模块 triple 提供；PROT_READ|PROT_WRITE=3、fd=-1 固定。 */
extern const uint32_t __vllvm_enstr_mmap_flags;

/* 池基址：0=未初始化，1=初始化中，其余=匿名页基址。 */
#define VLLVM_ENSTR_BUSY ((void *)(uintptr_t)1)
static _Atomic(void *) vllvm_enstr_pool_base;

/* 由单字节种子做 xorshift8 步进并混入字节位置；pass 侧以同一函数
 * 生成密文。步进次数限制在 8 以内，长串不会退化成 O(n^2)。 */
static uint8_t enstr_key_byte(const struct vllvm_enstr_desc *d, uint64_t j) {
  uint8_t k = d->key;
  for (unsigned i = (unsigned)(j & 7); i; --i) {
    k ^= (uint8_t)(k << 1);
    k ^= (uint8_t)(k >> 2);
  }
  return (uint8_t)(k ^ j);
}

static void __vllvm_enstr_init(void) {
  void *seen = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &vllvm_enstr_pool_base, &seen, VLLVM_ENSTR_BUSY,
          memory_order_acq_rel, memory_order_acquire)) {
    /* 抢锁失败：等初始化方发布；看到真实基址则直接返回。 */
    while (atomic_load_explicit(&vllvm_enstr_pool_base,
                                memory_order_acquire) == VLLVM_ENSTR_BUSY) {
    }
    return;
  }

  char *region =
      mmap(0, __vllvm_enstr_pool_size, 3, __vllvm_enstr_mmap_flags, -1, 0);
  if (region == (void *)-1)
    __builtin_trap(); /* 失败不能发布哨兵，也不能让他人永久等待。 */

  /* 只拷密文：初始化完成后内存里仍没有任何明文。密文源按池布局
   * 平铺，这里等价于一次整块搬运。 */
  for (uint64_t i = 0; i < __vllvm_enstr_count; ++i) {
    const struct vllvm_enstr_desc *d = &__vllvm_enstr_table[i];
    for (uint64_t j = 0; j < d->size; ++j)
      region[d->offset + j] = __vllvm_enstr_src[d->offset + j];
  }
  atomic_store_explicit(&vllvm_enstr_pool_base, region, memory_order_release);
}

/* index 是描述表下标。解密从密文源重算目标字节（幂等）：并发首访
 * 同一字符串时重复计算结果相同，不会出现原地 XOR 的二次解密损坏。 */
char *__vllvm_enstr_get(uint64_t index) {
  void *base =
      atomic_load_explicit(&vllvm_enstr_pool_base, memory_order_acquire);
  if (base == 0) {
    __vllvm_enstr_init();
    base = atomic_load_explicit(&vllvm_enstr_pool_base, memory_order_acquire);
  } else if (base == VLLVM_ENSTR_BUSY) {
    do {
      base = atomic_load_explicit(&vllvm_enstr_pool_base,
                                  memory_order_acquire);
    } while (base == VLLVM_ENSTR_BUSY);
  }

  struct vllvm_enstr_desc *d = &__vllvm_enstr_table[index];
  if (!__atomic_load_n(&d->isdec, __ATOMIC_ACQUIRE)) {
    char *region = (char *)base;
    for (uint64_t j = 0; j < d->size; ++j)
      region[d->offset + j] =
          (char)(__vllvm_enstr_src[d->offset + j] ^ enstr_key_byte(d, j));
    __atomic_store_n(&d->isdec, 1, __ATOMIC_RELEASE);
  }
  return (char *)base + d->offset;
}
