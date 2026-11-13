/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ivshmem_pci.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool lgPciAddressValid(const char * address)
{
  if (!address || strlen(address) != 12)
    return false;
  for (unsigned i = 0; i < 12; ++i)
  {
    if (i == 4 || i == 7)
    {
      if (address[i] != ':') return false;
    }
    else if (i == 10)
    {
      if (address[i] != '.') return false;
    }
    else if (!isxdigit((unsigned char)address[i]))
      return false;
  }
  return address[8] <= '1' && address[11] >= '0' && address[11] <= '7';
}

static bool readNumber(const char * root, const char * address,
  const char * name, unsigned long * value)
{
  char path[PATH_MAX], text[64], * end;
  if (snprintf(path, sizeof(path), "%s/%s/%s", root, address, name) >= sizeof(path))
    return false;
  FILE * file = fopen(path, "r");
  if (!file) return false;
  bool read = fgets(text, sizeof(text), file) != NULL;
  fclose(file);
  if (!read) return false;
  errno = 0;
  *value = strtoul(text, &end, 0);
  if (errno || end == text) return false;
  while (isspace((unsigned char)*end)) ++end;
  return *end == '\0';
}

static bool matches(const char * root, const char * address)
{
  unsigned long vendor, device;
  return lgPciAddressValid(address) &&
    readNumber(root, address, "vendor", &vendor) && vendor == 0x1af4 &&
    readNumber(root, address, "device", &device) && device == 0x1110;
}

bool lgPciResolve(const char * root, const char * address,
  struct LGPciMapping * mapping, char * error, size_t errorSize)
{
  char selected[13] = {0}, path[PATH_MAX];
  unsigned count = 0;
  if (address && *address)
  {
    if (!matches(root, address))
    {
      snprintf(error, errorSize, "Expected an IVSHMEM PCI address (1af4:1110), e.g. ivshmem:0000:06:01.0");
      return false;
    }
    memcpy(selected, address, sizeof(selected));
  }
  else
  {
    DIR * directory = opendir(root);
    if (!directory)
    {
      snprintf(error, errorSize, "Cannot inspect PCI devices: %s", strerror(errno));
      return false;
    }
    struct dirent * entry;
    while ((entry = readdir(directory)))
      if (matches(root, entry->d_name))
      {
        ++count;
        memcpy(selected, entry->d_name, sizeof(selected));
      }
    closedir(directory);
    if (count != 1)
    {
      snprintf(error, errorSize, count ?
        "Multiple IVSHMEM devices: select one with ivshmem:DOMAIN:BUS:SLOT.FUNCTION" :
        "No IVSHMEM PCI device (1af4:1110); add the shmem device to the VM XML");
      return false;
    }
  }

  unsigned long enabled;
  if (!readNumber(root, selected, "enable", &enabled) || !enabled)
  {
    snprintf(error, errorSize, "IVSHMEM %s is not enabled; run setup-ivshmem.sh in the guest", selected);
    return false;
  }
  if (snprintf(path, sizeof(path), "%s/%s/driver", root, selected) >= sizeof(path))
    goto invalidResource;
  struct stat st;
  if (lstat(path, &st) == 0 || errno != ENOENT)
  {
    snprintf(error, errorSize, "IVSHMEM %s is bound to a driver; use its /dev/kvmfrN explicitly or unbind only this IVSHMEM device", selected);
    return false;
  }
  if (snprintf(path, sizeof(path), "%s/%s/resource", root, selected) >= sizeof(path))
    goto invalidResource;
  FILE * file = fopen(path, "r");
  if (!file) goto invalidResource;
  uint64_t start = 0, end = 0, flags = 0;
  char line[256];
  bool valid = true;
  for (unsigned bar = 0; bar <= 2; ++bar)
    if (!fgets(line, sizeof(line), file) ||
        sscanf(line, "%" SCNx64 " %" SCNx64 " %" SCNx64, &start, &end, &flags) != 3)
    {
      valid = false;
      break;
    }
  fclose(file);
  if (!valid || !(flags & 0x200) || !start || end < start ||
      end - start >= INT_MAX || end - start + 1 < 4096)
    goto invalidResource;
  mapping->size = (size_t)(end - start + 1);
  if (snprintf(mapping->path, sizeof(mapping->path), "%s/%s/resource2", root, selected) >= sizeof(mapping->path))
    goto invalidResource;
  return true;

invalidResource:
  snprintf(error, errorSize, "Invalid or inaccessible IVSHMEM BAR2 resource for %s", selected);
  return false;
}
