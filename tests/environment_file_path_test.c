/* SPDX-License-Identifier: GPL-2.0-or-later */
#undef NDEBUG
#include <assert.h>
#include <glob.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char ** argv)
{
  assert(argc == 3);
  glob_t matches = {0};
  /* systemd resolves EnvironmentFile through glob(3) before opening it. */
  assert(glob(argv[1], 0, NULL, &matches) == 0);
  assert(matches.gl_pathc == 1 && !strcmp(matches.gl_pathv[0], argv[2]));
  FILE * file = fopen(matches.gl_pathv[0], "r");
  assert(file);
  char line[512];
  int found = 0;
  while (fgets(line, sizeof(line), file))
    if (!strcmp(line, "LG_FPS=120\n")) found = 1;
  assert(found && fclose(file) == 0);
  globfree(&matches);
  puts("PASS: EnvironmentFile pattern opens exactly the intended saved settings");
  return 0;
}
