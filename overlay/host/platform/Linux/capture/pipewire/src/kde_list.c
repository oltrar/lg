/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "kde_capture.h"
#include "common/debug.h"
int main(void)
{
  debug_init();
  return lgKdeCapture_list();
}
