/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include "../overlay/common/src/platform/linux/ivshmem.c"
#include <assert.h>
#include <sys/wait.h>
int main(void)
{
  char path[] = "/tmp/lg-shm-test-XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0 && ftruncate(fd, 64 * 1024 * 1024) == 0);
  close(fd);
  struct IVSHMEM mapping = {0};
  assert(ivshmemOpenDev(&mapping, path));
  assert(mapping.size == 64 * 1024 * 1024 && !ivshmemHasDMA(&mapping));
  pid_t child = fork();
  assert(child >= 0);
  if (child == 0)
  {
    struct IVSHMEM second = {0};
    assert(ivshmemOpenDev(&second, path));
    memcpy((char *)second.mem + second.size - 16, "LG-SHARED-OK", 12);
    ivshmemClose(&second);
    _exit(0);
  }
  int status;
  assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && !WEXITSTATUS(status));
  assert(!memcmp((char *)mapping.mem + mapping.size - 16, "LG-SHARED-OK", 12));
  ivshmemClose(&mapping);
  assert(mapping.mem == NULL && mapping.size == 0);
  assert(unlink(path) == 0);
  char empty[] = "/tmp/lg-empty-XXXXXX";
  fd = mkstemp(empty);
  assert(fd >= 0);
  close(fd);
  assert(!ivshmemOpenDev(&mapping, empty));
  assert(unlink(empty) == 0);
  puts("PASS: production transport maps 64 MiB across processes without DMA/KVMFR");
  return 0;
}
