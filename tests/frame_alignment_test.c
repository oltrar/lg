/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include "common/framebuffer.h"
#include "common/cpuinfo.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
int main(void)
{
  void * allocation = NULL, * destination = NULL;
  assert(posix_memalign(&allocation, 64, 1024) == 0);
  assert(posix_memalign(&destination, 4096, 8192) == 0);
  uint8_t * aligned = allocation;
  memset(aligned, 0x5a, 1024);
  FrameBuffer * frame = (FrameBuffer *)((uint8_t *)destination + 4096 - sizeof(FrameBuffer));
  /* Deliberately reproduce the old realloc guarantee: aligned to 16, not 32.
   * Run the real upstream writer in a child so the regression is observable. */
  if (cpuInfo_getFeatures()->avx2)
  {
    pid_t child = fork();
    assert(child >= 0);
    if (!child)
    {
      struct rlimit noCore = {0};
      setrlimit(RLIMIT_CORE, &noCore);
      framebuffer_prepare(frame);
      framebuffer_write(frame, aligned + 16, 512);
      _exit(0);
    }
    int status;
    assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
      WTERMSIG(status) == SIGSEGV);
    puts("PASS: reproduced old 16-byte snapshot SIGSEGV in real B7 AVX2 writer");
  }
  framebuffer_prepare(frame);
  assert(framebuffer_write(frame, aligned, 512));
  assert(atomic_load(&frame->wp) == 512 && !memcmp(frame->data, aligned, 512));
  free(destination); free(allocation);
  puts("PASS: aligned snapshot publishes through actual B7 SSE/AVX2 framebuffer writer");
  return 0;
}
