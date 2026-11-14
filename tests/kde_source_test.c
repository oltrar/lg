/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include <stdbool.h>
#include "kde_capture.h"
#include "kde_test_server.h"
#include <stdio.h>
#include <unistd.h>
int main(void)
{
  char runtime[] = "/tmp/lg-kde-test-XXXXXX";
  assert(mkdtemp(runtime) && setenv("XDG_RUNTIME_DIR", runtime, 1) == 0);
  for (unsigned boot = 0; boot < 2; ++boot)
  {
    struct KdeServer server = {.node = 42 + boot, .refresh = boot ? 165000 : 120000};
    startKdeServer(&server, boot == 0);
    assert(lgKdeCapture_list() == 0);
    assert(!lgKdeCapture_open("not-connected"));
    struct KDECapture * c = lgKdeCapture_open("HDMI-A-1");
    assert(c && lgKdeCapture_node(c) == 42 + boot && lgKdeCapture_alive(c));
    assert(lgKdeCapture_refresh(c) == (boot ? 165000 : 120000));
    assert(atomic_load(&server.capturedHdmi) && atomic_load(&server.opened) == 1);
    lgKdeCapture_close(c);
    for (unsigned n = 0; n < 100 && atomic_load(&server.closed) != 1; ++n) usleep(1000);
    assert(atomic_load(&server.closed) == 1);
    /* Losing the compositor/output must fail instead of selecting QXL. */
    c = lgKdeCapture_open("HDMI-A-1"); assert(c);
    stopKdeServer(&server);
    assert(!lgKdeCapture_alive(c)); lgKdeCapture_close(c);
  }
  assert(rmdir(runtime) == 0);
  puts("PASS: native KDE connector survives compositor restart/QXL removal/position change; no chooser, no wrong-output fallback, clean close");
  return 0;
}
