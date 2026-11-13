/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>

struct LGPciMapping
{
  char path[PATH_MAX];
  size_t size;
};

bool lgPciAddressValid(const char * address);
/* root is /sys/bus/pci/devices in production; tests supply a fixture tree. */
bool lgPciResolve(const char * root, const char * address,
  struct LGPciMapping * mapping, char * error, size_t errorSize);
