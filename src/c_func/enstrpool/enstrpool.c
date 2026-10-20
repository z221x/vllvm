/* enstr 字符串池运行时：整块匿名内存 + 下标访问 + 按需解密。
 *
 * 由 EncryptoStrPass 以 bitcode 形式链入目标模块（Android/ELF 优先）。
 * 初始化只把密文原样拷入匿名页——内存中不存在全量明文；每个字符串
 * 首次被 __vllvm_enstr_get 取用时才在池内解密（isdec 标记），反 dump
 * 只能拿到被访问过的片段。表等链接期常量由 pass 在链接后填充。
 * mmap 只声明不定义，最终链接时由目标平台 libc 提供。 */
#include <stdint.h>

/* 自行声明 mmap，避免依赖宿主头文件。参数用固定位宽：位码按 LP64
 * ABI 生成，Android/ELF 目标的 size_t/off_t 都是 64 位。 */
void *mmap(void *addr, uint64_t length, int prot, int flags, int fd,
           int64_t offset);

/* 密文源：全部密文按池布局平铺（表项 offset 即此处下标），pass 侧在
 * 位码链入后填充；命名带 __vllvm 前缀避免与用户符号撞名导致链接回退。 */
uint8_t *__vllvm_enstr_src = 0;
struct vllvm_enstr_desc {
  uint32_t offset;    /* 字符串在池内偏移量 */
  uint32_t size;      /* 字符串大小 */
  uint8_t key[8];     /* 密钥 */
  uint8_t isdec;      /* 已解密标记；表因此必须可写 */
};

extern struct vllvm_enstr_desc *__vllvm_enstr_table;
extern const uint32_t __vllvm_enstr_count;
extern const uint32_t __vllvm_enstr_pool_size;
/* MAP_PRIVATE|MAP_ANON 的值随平台不同（Android/ELF 0x22，Darwin 0x1002），
 * 由 pass 按模块 triple 提供；PROT_READ|PROT_WRITE=3、fd=-1 固定。 */
extern const uint32_t __vllvm_enstr_mmap_flags;

static uint8_t *vllvm_enstr_pool_base = 0x0;

static void __vllvm_enstr_init(void) {
  uint8_t *region = (uint8_t *)mmap(0, __vllvm_enstr_pool_size, 3, __vllvm_enstr_mmap_flags, -1, 0);

  if (region == (void *)-1)
    __builtin_trap();
  for (uint32_t i = 0; i < __vllvm_enstr_count; ++i) {
    const struct vllvm_enstr_desc *d = &__vllvm_enstr_table[i];
    for (uint32_t j = 0; j < d->size; ++j)
      region[d->offset + j] = __vllvm_enstr_src[d->offset + j];
  }
  vllvm_enstr_pool_base = region;
}

uint8_t *__vllvm_enstr_get(uint32_t offset) {
  if (vllvm_enstr_pool_base == 0) {
    __vllvm_enstr_init();
  }
  struct vllvm_enstr_desc *d = (struct vllvm_enstr_desc *)(__vllvm_enstr_table + offset);
  if (!d->isdec) {
    /* 密钥读取必须 volatile：LTO 后端会把 key[j & 7] 的周期访问
     * 向量化成从 key 字段起连续 32 字节的加载（越界读到 isdec、
     * 填充与下一个描述符），长度 >= 32 的字符串除前 8 字节外全部
     * 解密错误。volatile 阻断该向量化。 */
    for (uint32_t j = 0; j < d->size; ++j)
      vllvm_enstr_pool_base[d->offset + j] =
          (uint8_t)(__vllvm_enstr_src[d->offset + j] ^
                    *(volatile const uint8_t *)&d->key[j & 7]);
    d->isdec = 1;
  }
  return vllvm_enstr_pool_base + d->offset;
}
