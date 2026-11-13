/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include "../overlay/common/src/platform/linux/ivshmem_pci.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void writeFile(const char * root, const char * address,
  const char * name, const char * contents)
{
  char path[PATH_MAX];
  assert(snprintf(path, sizeof(path), "%s/%s/%s", root, address, name) < sizeof(path));
  FILE * file = fopen(path, "w");
  assert(file);
  assert(fputs(contents, file) >= 0);
  assert(fclose(file) == 0);
}
static void device(const char * root, const char * address)
{
  char path[PATH_MAX];
  assert(snprintf(path, sizeof(path), "%s/%s", root, address) < sizeof(path));
  assert(mkdir(path, 0700) == 0);
  writeFile(root, address, "vendor", "0x1af4\n");
  writeFile(root, address, "device", "0x1110\n");
  writeFile(root, address, "enable", "1\n");
  writeFile(root, address, "resource", "0 0 0\n0 0 0\n100000000 103ffffff 2200\n");
}
static void removeDevice(const char * root, const char * address)
{
  char path[PATH_MAX];
  const char * files[] = {"vendor", "device", "enable", "resource"};
  for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i)
  {
    snprintf(path, sizeof(path), "%s/%s/%s", root, address, files[i]);
    assert(unlink(path) == 0);
  }
  snprintf(path, sizeof(path), "%s/%s", root, address);
  assert(rmdir(path) == 0);
}
int main(void)
{
  assert(lgPciAddressValid("0000:06:01.0"));
  assert(lgPciAddressValid("ffff:ff:1f.7"));
  assert(!lgPciAddressValid("0000:06:20.0"));
  assert(!lgPciAddressValid("0000:06:01.8"));
  assert(!lgPciAddressValid("../06:01.0"));
  assert(!lgPciAddressValid(""));
  char root[] = "/tmp/lg-pci-test-XXXXXX", error[256], path[PATH_MAX];
  assert(mkdtemp(root));
  struct LGPciMapping mapping;
  assert(!lgPciResolve(root, NULL, &mapping, error, sizeof(error)));
  device(root, "0000:06:01.0");
  assert(lgPciResolve(root, NULL, &mapping, error, sizeof(error)));
  assert(mapping.size == 64 * 1024 * 1024);
  assert(strstr(mapping.path, "/0000:06:01.0/resource2"));
  assert(!lgPciResolve(root, "../bad", &mapping, error, sizeof(error)));
  device(root, "0000:07:01.0");
  assert(!lgPciResolve(root, NULL, &mapping, error, sizeof(error)));
  assert(strstr(error, "Multiple"));
  assert(lgPciResolve(root, "0000:06:01.0", &mapping, error, sizeof(error)));
  writeFile(root, "0000:06:01.0", "enable", "0\n");
  assert(!lgPciResolve(root, "0000:06:01.0", &mapping, error, sizeof(error)));
  writeFile(root, "0000:06:01.0", "enable", "1\n");
  snprintf(path, sizeof(path), "%s/0000:06:01.0/driver", root);
  assert(symlink("/nonexistent-driver", path) == 0);
  assert(!lgPciResolve(root, "0000:06:01.0", &mapping, error, sizeof(error)));
  assert(unlink(path) == 0);
  const char * invalid[] = {
    "bad\n", "0 0 0\n0 0 0\n100 1ff 100\n",
    "0 0 0\n0 0 0\n200 100 200\n",
    "0 0 0\n0 0 0\n100 100000000 200\n"
  };
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
  {
    writeFile(root, "0000:06:01.0", "resource", invalid[i]);
    assert(!lgPciResolve(root, "0000:06:01.0", &mapping, error, sizeof(error)));
  }
  writeFile(root, "0000:07:01.0", "vendor", "0x10de\n");
  assert(!lgPciResolve(root, "0000:07:01.0", &mapping, error, sizeof(error)));
  removeDevice(root, "0000:06:01.0");
  removeDevice(root, "0000:07:01.0");
  assert(rmdir(root) == 0);
  puts("PASS: PCI auto/explicit selection, 64 MiB BAR2, ambiguity and invalid devices");
  return 0;
}
