/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../overlay/common/src/platform/linux/ivshmem.c"
#include <unistd.h>
int main(void)
{
  /* init enabled/chowned ONLY the IVSHMEM BAR2. Prove that the mapper does not
   * need root, CAP_SYS_RAWIO, or a guest kernel driver. */
  if (setgid(1000) || setuid(1000)) return 1;
  struct IVSHMEM mapping = {0};
  if (!ivshmemOpenDev(&mapping, "ivshmem")) return 2;
  if (mapping.size != 64 * 1024 * 1024 || ivshmemHasDMA(&mapping)) return 3;
  if (memcmp(mapping.mem, "LG-HOST-HELLO", 14)) return 4;
  memcpy((char *)mapping.mem + mapping.size - 16, "LG-GUEST-OK", 12);
  ivshmemClose(&mapping);
  puts("PASS: unprivileged guest mapped 64 MiB IVSHMEM BAR2 without KVMFR");
  return 0;
}
