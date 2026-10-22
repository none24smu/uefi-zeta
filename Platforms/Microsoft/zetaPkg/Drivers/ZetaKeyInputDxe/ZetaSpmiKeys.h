/** @file
  Hardware keys of the Surface Duo 2 through the SPMI PMIC arbiter, as pure register-pointer code (host-testable).

  Facts, all from the stock zeta device tree (Lahaina V2.1 + Surface Duo2 MP overlay) and the stock kernel
  (surface-duo-oss-kernel surfaceduo2/11/2023.501.541, drivers/spmi/spmi-pmic-arb.c):
    power key   PMK8350 SID 0, pon_hlos@1300 (PID 0x13), PON_RT_STS at +0x10, KPDPWR = bit 7, set = pressed (linux,code 116)
    volume down same register, RESIN = bit 6, set = pressed (linux,code 114)
    volume up   PM8350 (SID 1) GPIO6, peripheral PID 0x88 + 5 = 0x8d, RT_STS at +0x10 bit 0; gpio-keys flags 1 = active low
  SM8350 has arbiter version 5 ("core" 0xc440000 size 0x1100, "obsrvr" 0xe600000, "cnfg" 0xc40a000, EE 0):
    the APID map is core + 0x900 + 4 * n for every n below the core size; one PPID can have several APIDs, and like Linux
    (pmic_arb_read_apid_map_v5) the first one is taken and any later one whose ownership entry (cnfg + 0x700 + 4 * n,
    bits 2:0) names this EE replaces it; the observer channel of an
    APID is obsrvr + 0x10000 * ee + 0x80 * apid, and a register byte is read with an extended-read command on it.
  sheng's SM8550 header is arbiter version 7 (map at 0x2000, channel stride 0x20); none of its offsets apply here.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef ZETA_SPMI_KEYS_H_
#define ZETA_SPMI_KEYS_H_

#include <stdint.h>

enum {
  ZK_VERSION_V5_MIN = 0x50000000u, ZK_VERSION_V7_MIN = 0x70000000u,
  ZK_CORE_SIZE = 0x1100, ZK_APID_MAP = 0x900, ZK_OWNERSHIP = 0x700, ZK_OWNER_MASK = 0x7u,
  ZK_OBS_EE_STRIDE = 0x10000, ZK_OBS_APID_STRIDE = 0x80,
  ZK_CMD = 0x00, ZK_STATUS = 0x08, ZK_RDATA0 = 0x18,
  ZK_OP_EXT_READL = 1,
  ZK_STATUS_DONE = 1u << 0, ZK_STATUS_ERRORS = 0xeu,
  ZK_POLLS = 100000,
  ZK_PPID_PON = 0x013, ZK_PPID_GPIO6 = 0x18d, ZK_REG_RT_STS = 0x10,
  ZK_PON_RESIN = 1u << 6, ZK_PON_KPDPWR = 1u << 7,
  ZK_KEY_POWER = 1u << 0, ZK_KEY_VOLDOWN = 1u << 1, ZK_KEY_VOLUP = 1u << 2
};

struct zk_arbiter {
  volatile uint32_t *core;   // ZK_CORE_SIZE bytes
  volatile uint32_t *cnfg;   // ownership table at +0x700
  volatile uint32_t *obs;    // observer channels, EE 0 at the base
  uint8_t ee;
};

// 0 and *apid on success. 1 not arbiter version 5, 3 PPID not mapped.
static inline int zk_find_apid (const struct zk_arbiter *a, uint32_t ppid, int *apid)
{
  uint32_t version = a->core[0];
  *apid = -1;
  if (version < ZK_VERSION_V5_MIN || version >= ZK_VERSION_V7_MIN) return 1;
  for (uint32_t n = 0; ZK_APID_MAP + 4 * n < ZK_CORE_SIZE; ++n) {
    uint32_t map = a->core[(ZK_APID_MAP + 4 * n) / 4];
    if (!map || ((map >> 8) & 0xfff) != ppid) continue;
    uint32_t owner = a->cnfg[(ZK_OWNERSHIP + 4 * n) / 4] & ZK_OWNER_MASK;
    if (*apid < 0 || owner == a->ee) *apid = (int) n;
  }
  return *apid < 0 ? 3 : 0;
}

// One register byte through the observer channel. 0 ok, 1 timeout, 2 the arbiter reported an error.
static inline int zk_read8 (const struct zk_arbiter *a, int apid, uint8_t reg, uint8_t *value)
{
  volatile uint32_t *ch = a->obs + (ZK_OBS_EE_STRIDE * a->ee + ZK_OBS_APID_STRIDE * (uint32_t) apid) / 4;
  ch[ZK_CMD / 4] = ((uint32_t) ZK_OP_EXT_READL << 27) | ((uint32_t) reg << 4);
  for (unsigned n = 0; n < ZK_POLLS; ++n) {
    uint32_t s = ch[ZK_STATUS / 4];
    if (s & ZK_STATUS_DONE) {
      if (s & ZK_STATUS_ERRORS) return 2;
      *value = (uint8_t) ch[ZK_RDATA0 / 4];
      return 0;
    }
  }
  return 1;
}

struct zk_keys { int pon, gpio; };   // APIDs, -1 = not available

static inline int zk_open (const struct zk_arbiter *a, struct zk_keys *k)
{
  int rc = zk_find_apid (a, ZK_PPID_PON, &k->pon);
  if (rc) return rc;
  if (zk_find_apid (a, ZK_PPID_GPIO6, &k->gpio)) k->gpio = -1;   // the power and volume-down keys still work without it
  return 0;
}

// Pressed keys as ZK_KEY_* bits. 0 ok; non-zero when a read failed (nothing is reported pressed then).
static inline int zk_sample (const struct zk_arbiter *a, const struct zk_keys *k, uint8_t *pressed)
{
  uint8_t v = 0;
  *pressed = 0;
  int rc = zk_read8 (a, k->pon, ZK_REG_RT_STS, &v);
  if (rc) return rc;
  if (v & ZK_PON_KPDPWR) *pressed |= ZK_KEY_POWER;
  if (v & ZK_PON_RESIN) *pressed |= ZK_KEY_VOLDOWN;
  if (k->gpio >= 0) {
    rc = zk_read8 (a, k->gpio, ZK_REG_RT_STS, &v);
    if (rc) { *pressed = 0; return rc; }
    if (!(v & 1)) *pressed |= ZK_KEY_VOLUP;       // active low
  }
  return 0;
}

// Debounce and auto-repeat. A key counts as pressed after two identical samples. Volume keys repeat while held: first after
// `delay` polls, then every `period` polls. step() returns the keys to report now (new presses plus repeats).
struct zk_filter { uint8_t last, stable; uint16_t held[3]; };

static inline uint8_t zk_step (struct zk_filter *f, uint8_t sample, uint16_t delay, uint16_t period)
{
  uint8_t out = 0;
  if (sample == f->last) {
    uint8_t fresh = (uint8_t) (sample & ~f->stable);
    f->stable = sample;
    out = fresh;
    for (unsigned i = 0; i < 3; ++i) {
      uint8_t bit = (uint8_t) (1u << i);
      if (!(sample & bit)) { f->held[i] = 0; continue; }
      if (fresh & bit) { f->held[i] = 0; continue; }
      ++f->held[i];
      if (bit != ZK_KEY_POWER && f->held[i] >= delay && (f->held[i] - delay) % period == 0) out |= bit;
    }
  }
  f->last = sample;
  return out;
}

#endif
