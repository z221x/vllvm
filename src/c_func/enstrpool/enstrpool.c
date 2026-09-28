/* enstr 字符串池运行时：整块匿名内存 + 下标访问。
 *
 * 由 EncryptoStrPass 以 bitcode 形式链入目标模块（Android/ELF 优先）。
 * 池描述表等链接期常量由 pass 在链接后填充；这里只声明为指针/标量，
 * 避免不完整数组类型带来的布局歧义。mmap 只声明不定义，最终链接时由
 * 目标平台 libc 提供。 */

#include <stdatomic.h>
#include <stdint.h>

/* 自行声明 mmap，避免依赖宿主头文件。参数用固定位宽：位码按 LP64
 * ABI 生成，Android/ELF 目标的 size_t/off_t 都是 64 位。 */
void *mmap(void *addr, uint64_t length, int prot, int flags, int fd,
           int64_t offset);

struct vllvm_enstr_desc {
  const uint8_t *src; /* 密文全局 */
  uint64_t offset;    /* 池内字节下标 */
  uint64_t size;      /* 含结尾 NUL 的字节数 */
  uint64_t key0;      /* 16 字节 XOR 密钥的低/高 8 字节 */
  uint64_t key1;
};

/* 以下四个常量由 pass 按模块内容生成，链接后转为 internal。 */
extern const struct vllvm_enstr_desc *__vllvm_enstr_table;
extern const uint64_t __vllvm_enstr_count;
extern const uint64_t __vllvm_enstr_pool_size;
/* MAP_PRIVATE|MAP_ANON 的值随平台不同（Android/ELF 0x22，Darwin 0x1002），
 * 由 pass 按模块 triple 提供；PROT_READ|PROT_WRITE=3、fd=-1 固定。 */
extern const uint32_t __vllvm_enstr_mmap_flags;

/* 1 表示初始化中，不会作为字符串地址发布。 */
#define VLLVM_ENSTR_BUSY ((void *)(uintptr_t)1)

static _Atomic(void *) vllvm_enstr_pool_base;

static uint8_t enstr_key_byte(const struct vllvm_enstr_desc *d, uint64_t j) {
  /* 16 字节密钥按小端展开：低 8 字节来自 key0，高 8 字节来自 key1，
   * 与 pass 侧的打包方式一致。volatile 读取阻止优化器把 src^key
   * 折叠成明文 store 常量（O2 下密文与密钥都是编译期常量）。 */
  const volatile uint64_t *part = (j & 8) ? &d->key1 : &d->key0;
  return (uint8_t)(*part >> ((j & 7) * 8));
}

static void __vllvm_enstr_init(void) {
  void *expected = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &vllvm_enstr_pool_base, &expected, VLLVM_ENSTR_BUSY,
          memory_order_acq_rel, memory_order_acquire)) {
    /* 抢锁失败：等初始化方发布；看到已发布地址则直接返回。 */
    while (atomic_load_explicit(&vllvm_enstr_pool_base,
                                memory_order_acquire) == VLLVM_ENSTR_BUSY) {
    }
    return;
  }

  char *region =
      mmap(0, __vllvm_enstr_pool_size, 3, __vllvm_enstr_mmap_flags, -1, 0);
  if (region == (void *)-1)
    __builtin_trap(); /* 失败不能发布哨兵，也不能让他人永久等待。 */

  for (uint64_t i = 0; i < __vllvm_enstr_count; ++i) {
    const struct vllvm_enstr_desc *d = &__vllvm_enstr_table[i];
    for (uint64_t j = 0; j < d->size; ++j)
      region[d->offset + j] = (char)(d->src[j] ^ enstr_key_byte(d, j));
  }

  /* 解密完成后才发布基地址；明文保持进程生命周期，不主动释放。 */
  atomic_store_explicit(&vllvm_enstr_pool_base, region, memory_order_release);
}

char *__vllvm_enstr_get(uint64_t index) {
  void *base =
      atomic_load_explicit(&vllvm_enstr_pool_base, memory_order_acquire);
  if (base == 0) {
    __vllvm_enstr_init();
    /* init 返回后池已发布，重读拿到真实基址。 */
    base = atomic_load_explicit(&vllvm_enstr_pool_base, memory_order_acquire);
  } else if (base == VLLVM_ENSTR_BUSY) {
    /* 他人正在初始化：自旋等发布，不能把哨兵当基址做下标运算。 */
    do {
      base = atomic_load_explicit(&vllvm_enstr_pool_base,
                                  memory_order_acquire);
    } while (base == VLLVM_ENSTR_BUSY);
  }
  return (char *)base + index;
}
