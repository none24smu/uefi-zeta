// Host test of ZetaSpmiKeys.h against a static model of an arbiter version 5 (cc -O2 -Wall keys_host.c && ./a.out).
// Each observer channel is preloaded with DONE and its data, so a read only succeeds through the right channel offset.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../ZetaSpmiKeys.h"

static uint32_t core[ZK_CORE_SIZE / 4];
static uint32_t cnfg[0x1000 / 4];
static uint32_t obs[(2 * ZK_OBS_EE_STRIDE) / 4];

static void map (int apid, uint32_t ppid, uint32_t owner)
{
  core[(ZK_APID_MAP + 4 * apid) / 4] = (ppid << 8) | 1u;
  cnfg[(ZK_OWNERSHIP + 4 * apid) / 4] = owner;
}

static void channel (int ee, int apid, uint32_t status, uint8_t data)
{
  uint32_t *ch = obs + (ZK_OBS_EE_STRIDE * ee + ZK_OBS_APID_STRIDE * apid) / 4;
  ch[ZK_STATUS / 4] = status;
  ch[ZK_RDATA0 / 4] = data;
}

int main (void)
{
  struct zk_arbiter a = { core, cnfg, obs, 0 };
  struct zk_keys k;
  uint8_t pressed;
  int apid;

  core[0] = 0x30000000;             // v3 and v7 cores use other layouts and are refused
  assert (zk_find_apid (&a, ZK_PPID_PON, &apid) == 1);
  core[0] = 0x70000000;
  assert (zk_find_apid (&a, ZK_PPID_PON, &apid) == 1);
  core[0] = 0x50020000;
  assert (zk_find_apid (&a, ZK_PPID_PON, &apid) == 3);

  map (7, ZK_PPID_PON, 3);          // first mapping, owned by another EE
  map (40, ZK_PPID_PON, 0);         // a later one owned by EE 0 replaces it
  map (41, ZK_PPID_PON, 2);         // and is kept against a still later foreign one
  map (511, ZK_PPID_GPIO6, 5);      // the last map slot (0x900 + 4 * 511 = 0x10fc) is scanned
  assert (zk_find_apid (&a, ZK_PPID_PON, &apid) == 0 && apid == 40);
  assert (zk_find_apid (&a, ZK_PPID_GPIO6, &apid) == 0 && apid == 511);
  assert (zk_open (&a, &k) == 0 && k.pon == 40 && k.gpio == 511);

  // Nothing pressed: PON bits clear, volume up line high.
  channel (0, 40, ZK_STATUS_DONE, 0x00);
  channel (0, 511, ZK_STATUS_DONE, 0x01);
  assert (zk_sample (&a, &k, &pressed) == 0 && pressed == 0);
  // Power and volume down from PON, volume up from the low GPIO line.
  channel (0, 40, ZK_STATUS_DONE, ZK_PON_KPDPWR | ZK_PON_RESIN);
  channel (0, 511, ZK_STATUS_DONE, 0x00);
  assert (zk_sample (&a, &k, &pressed) == 0 && pressed == (ZK_KEY_POWER | ZK_KEY_VOLDOWN | ZK_KEY_VOLUP));
  // The command lands in the channel: extended read of RT_STS, byte count 0.
  assert (obs[(ZK_OBS_APID_STRIDE * 40) / 4] == (((uint32_t) ZK_OP_EXT_READL << 27) | (ZK_REG_RT_STS << 4)));
  // An arbiter error reports nothing pressed.
  channel (0, 511, ZK_STATUS_DONE | 2, 0x00);
  assert (zk_sample (&a, &k, &pressed) == 2 && pressed == 0);

  // EE 1 reads through its own window, 0x10000 further.
  struct zk_arbiter b = { core, cnfg, obs, 1 };
  channel (1, 40, ZK_STATUS_DONE, ZK_PON_KPDPWR);
  uint8_t v = 0;
  assert (zk_read8 (&b, 40, ZK_REG_RT_STS, &v) == 0 && v == ZK_PON_KPDPWR);

  // Debounce: a press counts after two equal samples; a held volume key repeats, power does not.
  struct zk_filter f;
  memset (&f, 0, sizeof f);
  assert (zk_step (&f, ZK_KEY_VOLUP, 3, 2) == 0);
  assert (zk_step (&f, ZK_KEY_VOLUP, 3, 2) == ZK_KEY_VOLUP);
  int repeats = 0;
  for (int i = 0; i < 10; ++i) repeats += zk_step (&f, ZK_KEY_VOLUP | ZK_KEY_POWER, 3, 2) & ZK_KEY_VOLUP ? 1 : 0;
  assert (repeats > 0);
  printf ("ZetaSpmiKeys: all host checks passed\n");
  return 0;
}
