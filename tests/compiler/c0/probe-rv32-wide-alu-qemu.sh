#!/usr/bin/env bash
# RV32I full-width native two-word arithmetic vs independent GNU C
# translation and QEMU, with GNU AS AND MiniAS machine consumers.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
test -x "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/wide.i" <<'SRC'
typedef unsigned long long U;
typedef long long I;
U w_add(U a,U b) { return a + b; }
U w_sub(U a,U b) { return a - b; }
U w_and(U a,U b) { return a & b; }
U w_or(U a,U b) { return a | b; }
U w_xor(U a,U b) { return a ^ b; }
U w_mul(U a,U b) { return a * b; }
U w_udiv(U a,U b) { return a / b; }
U w_umod(U a,U b) { return a % b; }
I w_sdiv(I a,I b) { return a / b; }
I w_smod(I a,I b) { return a % b; }
U w_not(U a) { return ~a; }
I w_neg(I a) { return -a; }
U w_shl(U a,unsigned b) { return a << b; }
U w_shr(U a,unsigned b) { return a >> b; }
I w_sar(I a,unsigned b) { return a >> b; }
int w_eq(U a,U b) { return a == b; }
int w_lt_u(U a,U b) { return a < b; }
int w_lt_s(I a,I b) { return a < b; }
int w_zero(U a) { return !a; }
struct VirtioConfig { U (*get_features)(void *); };
struct VirtioDev { const struct VirtioConfig *config; };
U virtio_features(const struct VirtioDev *dev, void *context) {
  return dev->config->get_features(context);
}
U virtio_feature32_mask(const struct VirtioDev *dev, void *context) {
  U features = dev->config->get_features(context);
  return features & (1ULL << 32);
}
int virtio_feature32_test(const struct VirtioDev *dev, void *context) {
  U features = dev->config->get_features(context);
  return (features & (1ULL << 32)) != 0ULL;
}

struct VirtioModelConfig {
  void (*reserved[9])(void);
  U (*get_features)(void *);
};
struct VirtioModelDriver {
  const unsigned *feature_table;
  unsigned feature_table_size;
  const unsigned *feature_table_legacy;
  unsigned feature_table_size_legacy;
};
struct VirtioModelDev {
  unsigned char head[40];
  unsigned char prefix[552];
  const struct VirtioModelConfig *config;
  const void *vringh_config;
  void *vqs_prev;
  void *vqs_next;
  U features;
};
struct VirtioModelTrace {
  U callback_bits;
  U driver_bits;
  U legacy_bits;
  U selected_bits;
  U transported_bits;
};
void virtio_probe_model(void *_d,
                        const struct VirtioModelDriver *drv,
                        struct VirtioModelTrace *trace) {
  struct VirtioModelDev *dev =
      (struct VirtioModelDev *)((char *)_d - 40);
  unsigned i;
  U device_features = dev->config->get_features(dev);
  U driver_features = 0ULL;
  U driver_features_legacy;
  trace->callback_bits = device_features;
  for (i = 0U; i < drv->feature_table_size; i++) {
    unsigned f = drv->feature_table[i];
    driver_features |= (1ULL << f);
  }
  trace->driver_bits = driver_features;
  if (drv->feature_table_legacy) {
    driver_features_legacy = 0ULL;
    for (i = 0U; i < drv->feature_table_size_legacy; i++) {
      unsigned f = drv->feature_table_legacy[i];
      driver_features_legacy |= (1ULL << f);
    }
  } else {
    driver_features_legacy = driver_features;
  }
  trace->legacy_bits = driver_features_legacy;
  if (device_features & (1ULL << 32))
    dev->features = driver_features & device_features;
  else
    dev->features = driver_features_legacy & device_features;
  trace->selected_bits = dev->features;
  for (i = 28U; i < 42U; i++)
    if (device_features & (1ULL << i))
      dev->features |= (1ULL << i);
  trace->transported_bits = dev->features;
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/wide.i" -o "$work/wide.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c \
  "$work/wide.s" -o "$work/wide-minic-gas.o"
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/wide-minic-as.o" "$work/wide.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -ffreestanding \
  -fno-builtin -fno-stack-protector -x c -c \
  "$work/wide.i" -o "$work/wide-gnu.o"
cat >"$work/driver.c" <<'SRC'
typedef unsigned long long U;
typedef long long I;
extern U w_add(U,U), w_sub(U,U), w_and(U,U), w_or(U,U), w_xor(U,U);
extern U w_mul(U,U), w_not(U), w_shl(U,unsigned), w_shr(U,unsigned);
extern U w_udiv(U,U), w_umod(U,U);
extern I w_sdiv(I,I), w_smod(I,I);
extern I w_neg(I), w_sar(I,unsigned);
extern int w_eq(U,U), w_lt_u(U,U), w_lt_s(I,I), w_zero(U);
struct VirtioConfig { U (*get_features)(void *); };
struct VirtioDev { const struct VirtioConfig *config; };
extern U virtio_features(const struct VirtioDev *, void *);
extern U virtio_feature32_mask(const struct VirtioDev *, void *);
extern int virtio_feature32_test(const struct VirtioDev *, void *);
static U virtio_get_features(void *context) {
  return *(volatile U *)context;
}
static const struct VirtioConfig virtio_config = { virtio_get_features };
static const struct VirtioDev virtio_device = { &virtio_config };
static volatile U virtio_feature_value;
static const U virtio_cases[7] = {
  0ULL, 1ULL, 0x100000000ULL, 0x100000005ULL,
  0x200000000ULL, 0xffffffffffffffffULL, 0x123456789abcdef0ULL
};
static volatile U values[10] = {
  0ULL, 1ULL, 0xffffffffULL, 0x100000000ULL,
  0x123456789abcdef0ULL, 0xaaaaaaaa55555555ULL,
  0xfedcba9876543210ULL, 0xffffffffffffffffULL,
  0x7fffffffffffffffULL, 0x8000000000000000ULL
};
static const unsigned shifts[7] = {0U,1U,7U,31U,32U,33U,63U};

struct VirtioModelConfig {
  void (*reserved[9])(void);
  U (*get_features)(void *);
};
struct VirtioModelDriver {
  const unsigned *feature_table;
  unsigned feature_table_size;
  const unsigned *feature_table_legacy;
  unsigned feature_table_size_legacy;
};
struct VirtioModelDev {
  unsigned char head[40];
  unsigned char prefix[552];
  const struct VirtioModelConfig *config;
  const void *vringh_config;
  void *vqs_prev;
  void *vqs_next;
  U features;
};
struct VirtioModelTrace {
  U callback_bits;
  U driver_bits;
  U legacy_bits;
  U selected_bits;
  U transported_bits;
};
extern void virtio_probe_model(void *, const struct VirtioModelDriver *,
                               struct VirtioModelTrace *);
static U virtio_model_get_features(void *context) {
  const struct VirtioModelDev *dev = (const struct VirtioModelDev *)context;
  if (dev->head[0] != 0xa5U || dev->prefix[551] != 0x5aU)
    return 0ULL;
  return virtio_feature_value;
}
static const struct VirtioModelConfig virtio_model_config = {
  .get_features = virtio_model_get_features
};
static const unsigned model_driver_table[10] = {0U,7U,28U,31U,32U,35U,39U,40U,41U,63U};
static const unsigned model_legacy_table[5] = {1U,7U,31U,33U,63U};
static const U model_cases[9] = {
  0ULL, (1ULL<<31), (1ULL<<32), ((1ULL<<32)|(1ULL<<28)),
  ((1ULL<<32)|(1ULL<<40)), (1ULL<<41), (1ULL<<63),
  0xffffffffffffffffULL, 0x123456789abcdef0ULL
};
static void reference_virtio_model(void *_d,
                                    const struct VirtioModelDriver *drv,
                                    struct VirtioModelTrace *trace) {
  struct VirtioModelDev *dev =
      (struct VirtioModelDev *)((char *)_d - 40);
  unsigned i;
  U device_features = dev->config->get_features(dev);
  U driver_features = 0ULL;
  U driver_features_legacy;
  trace->callback_bits = device_features;
  for (i = 0U; i < drv->feature_table_size; i++) {
    unsigned f = drv->feature_table[i];
    driver_features |= (1ULL << f);
  }
  trace->driver_bits = driver_features;
  if (drv->feature_table_legacy) {
    driver_features_legacy = 0ULL;
    for (i = 0U; i < drv->feature_table_size_legacy; i++) {
      unsigned f = drv->feature_table_legacy[i];
      driver_features_legacy |= (1ULL << f);
    }
  } else {
    driver_features_legacy = driver_features;
  }
  trace->legacy_bits = driver_features_legacy;
  if (device_features & (1ULL << 32))
    dev->features = driver_features & device_features;
  else
    dev->features = driver_features_legacy & device_features;
  trace->selected_bits = dev->features;
  for (i = 28U; i < 42U; i++)
    if (device_features & (1ULL << i))
      dev->features |= (1ULL << i);
  trace->transported_bits = dev->features;
}
static int test_virtio_model(void) {
  unsigned i, variant;
  static struct VirtioModelDev minic_dev, gnu_dev;
  struct VirtioModelTrace minic_trace, gnu_trace;
  struct VirtioModelDriver drv;
  drv.feature_table = model_driver_table;
  drv.feature_table_size = 10U;
  drv.feature_table_legacy = (const unsigned *)0;
  drv.feature_table_size_legacy = 0U;
  minic_dev.config = &virtio_model_config;
  gnu_dev.config = &virtio_model_config;
  minic_dev.head[0] = gnu_dev.head[0] = 0xa5U;
  minic_dev.prefix[551] = gnu_dev.prefix[551] = 0x5aU;
  if ((unsigned)((char *)&minic_dev.config - (char *)&minic_dev) != 592U ||
      (unsigned)((char *)&minic_dev.features - (char *)&minic_dev) != 608U ||
      (unsigned)((char *)&virtio_model_config.get_features -
                 (char *)&virtio_model_config) != 36U)
    return 17;
  for (variant = 0U; variant < 2U; variant++) {
    drv.feature_table_legacy = variant ? model_legacy_table : (const unsigned *)0;
    drv.feature_table_size_legacy = variant ? 5U : 0U;
    for (i = 0U; i < 9U; i++) {
      virtio_feature_value = model_cases[i];
      minic_dev.features = 0ULL;
      gnu_dev.features = 0ULL;
      virtio_probe_model((char *)&minic_dev + 40, &drv, &minic_trace);
      reference_virtio_model((char *)&gnu_dev + 40, &drv, &gnu_trace);
      if (minic_trace.callback_bits != gnu_trace.callback_bits ||
          minic_trace.callback_bits != model_cases[i]) return 18;
      if (minic_trace.driver_bits != gnu_trace.driver_bits) return 19;
      if (minic_trace.legacy_bits != gnu_trace.legacy_bits) return 20;
      if (minic_trace.selected_bits != gnu_trace.selected_bits) return 21;
      if (minic_trace.transported_bits != gnu_trace.transported_bits) return 22;
      if (minic_dev.features != gnu_dev.features) return 23;
    }
  }
  return 0;
}
static int test(void) {
  unsigned i,j,k;
  for (i=0;i<10U;++i) {
    U a=values[i];
    if (w_not(a)!=~a || w_neg((I)a)!=-(I)a || w_zero(a)!=(a==0))
      return 1;
    for (j=0;j<10U;++j) {
      U b=values[j];
      if (w_add(a,b)!=a+b) return 2;
      if (w_sub(a,b)!=a-b) return 3;
      if (w_and(a,b)!=(a&b) || w_or(a,b)!=(a|b) ||
          w_xor(a,b)!=(a^b)) return 4;
      if (w_mul(a,b)!=a*b) return 5;
      if (w_eq(a,b)!=(a==b)) return 6;
      if (w_lt_u(a,b)!=(a<b)) return 7;
      if (w_lt_s((I)a,(I)b)!=((I)a<(I)b)) return 8;
      if (b != 0ULL) {
        if (w_udiv(a,b)!=a/b || w_umod(a,b)!=a%b) return 12;
        if (!(a==0x8000000000000000ULL &&
              b==0xffffffffffffffffULL) &&
            (w_sdiv((I)a,(I)b)!=((I)a/(I)b) ||
             w_smod((I)a,(I)b)!=((I)a%(I)b))) return 13;
      }
    }
    for (k=0;k<7U;++k) {
      unsigned sh=shifts[k];
      if (w_shl(a,sh)!=(a<<sh)) return 9;
      if (w_shr(a,sh)!=(a>>sh)) return 10;
      if (w_sar((I)a,sh)!=((I)a>>sh)) return 11;
    }
  }
  return 0;
}
static int test_virtio(void) {
  unsigned i;
  for (i=0;i<7U;++i) {
    U bits=virtio_cases[i], masked=bits&(1ULL<<32);
    virtio_feature_value=bits;
    if (virtio_features(&virtio_device,(void *)&virtio_feature_value)!=bits)
      return 14;
    if (virtio_feature32_mask(&virtio_device,(void *)&virtio_feature_value)
        !=masked) return 15;
    if (virtio_feature32_test(&virtio_device,(void *)&virtio_feature_value)
        !=(masked!=0ULL)) return 16;
  }
  return 0;
}
int main(void) {
  int result=test();
  if (result != 0) return result;
  result = test_virtio();
  return result != 0 ? result : test_virtio_model();
}
SRC
cat >"$work/start.s" <<'SRC'
.text
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  call main
  li a7,93
  ecall
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
  -O1 -ffreestanding -fno-builtin -fno-stack-protector \
  -c "$work/driver.c" -o "$work/driver.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
  -c "$work/start.s" -o "$work/start.o"
bash tests/compiler/c0/build-rv32-gcc-runtime.sh "$work"
mapfile -t rt < "$work/rv32-runtime-objects.rsp"
for mode in gnu minic-gas minic-as; do
  case "$mode" in
    gnu) obj="$work/wide-gnu.o" ;;
    minic-gas) obj="$work/wide-minic-gas.o" ;;
    minic-as) obj="$work/wide-minic-as.o" ;;
  esac
  riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/$mode.elf" "$work/start.o" "$work/driver.o" "$obj" "${rt[@]}"
  set +e
  timeout 30s qemu-riscv32 "$work/$mode.elf"
  status=$?
  set -e
  if [ "$status" -ne 0 ]; then
    echo "RV32_I64_ALU=FAIL mode=$mode exit=$status (virtio_model_phase_18_callback_19_driver_20_legacy_21_selected_22_transport_23_store)" >&2
    exit 1
  fi
done
echo "RV32_I64_ALU=PASS checks=1500 functions=22 ISA=rv32i ABI=ilp32 GNU_C=1 GNU_AS=1 MiniAS=1 QEMU=1 upstream_llvm_DI_arithmetic=1"
echo "RV32_VIRTIO_FEATURE32=PASS cases=7 indirect_u64_callback_a0_a1=1 shift32=1 wide_bitwise_and=1 GNU_C=1 GNU_AS=1 MiniAS=1 QEMU=1"
echo "RV32_VIRTIO_MODEL=PASS cases=9 variants=2 feature_table=1ull_dynamic_shift transport_28_41=1 config_at_592_of_dev_552_of_embedded=1 feature_at_608_of_dev_568_of_embedded=1 config_callback_at_36=1 GNU_C=1 GNU_AS=1 MiniAS=1 QEMU=1"