#include "portal_test_server.h"

int main(int argc, char ** argv)
{
  g_test_init(&argc, &argv, NULL);
  g_autoptr(GError) error = NULL;
  g_autofree char * config = g_dir_make_tmp("lg-portal-config-XXXXXX", &error);
  assert(config && !error);
  g_setenv("XDG_CONFIG_HOME", config, TRUE);
  g_setenv("XDG_CURRENT_DESKTOP", "KDE", TRUE);
  g_autoptr(GTestDBus) bus = g_test_dbus_new(G_TEST_DBUS_NONE);
  g_test_dbus_up(bus);
  struct Server server = {.address = g_test_dbus_get_bus_address(bus)};
  g_mutex_init(&server.mutex);
  g_cond_init(&server.condition);
  GThread * thread = g_thread_new("fake-portal", serve, &server);
  g_mutex_lock(&server.mutex);
  gint64 deadline = g_get_monotonic_time() + 15 * G_TIME_SPAN_SECOND;
  while (!server.ready)
    assert(g_cond_wait_until(&server.condition, &server.mutex, deadline));
  g_mutex_unlock(&server.mutex);
  struct Portal * portal = portal_create();
  assert(portal);
  GDBusConnection * client = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  assert(client && !error);
  char * handle = NULL;
  assert(portal_createScreenCastSession(portal, &handle));
  assert(handle && !strcmp(handle, SESSION));
  assert(portal_selectSource(portal, handle, true));
  assert(portal_getPipewireNode(portal, handle) == 42);
  int fd = portal_openPipewireRemote(portal, handle);
  assert(fd >= 0 && fcntl(fd, F_GETFD) >= 0);
  close(fd);
  portal_destroySession(portal, &handle);
  assert(!handle && g_atomic_int_get(&server.closeCount) == 1);
  portal_destroySession(portal, &handle);
  assert(g_atomic_int_get(&server.closeCount) == 1);

  g_autofree char * tokenPath = g_build_filename(config,
    "looking-glass-linux-wayland", "restore-kde", NULL);
  g_autofree char * saved = NULL;
  assert(g_file_get_contents(tokenPath, &saved, NULL, NULL));
  assert(!strcmp(saved, "new-nvidia-token"));
  struct stat tokenStat;
  assert(stat(tokenPath, &tokenStat) == 0 && (tokenStat.st_mode & 0777) == 0600);

  /* A remembered QXL display is sent normally, but never when the caller asks
   * for a fresh chooser. Cancellation must keep the prior token untouched. */
  assert(g_file_set_contents(tokenPath, "old-qxl-token", -1, NULL));
  g_atomic_int_set(&server.expectedToken, 1);
  assert(portal_createScreenCastSession(portal, &handle));
  assert(portal_selectSource(portal, handle, true));
  portal_destroySession(portal, &handle);
  g_atomic_int_set(&server.expectedToken, 0);
  g_atomic_int_set(&server.mode, CANCEL_START);
  assert(portal_createScreenCastSession(portal, &handle));
  assert(portal_selectSource(portal, handle, false));
  assert(portal_getPipewireNode(portal, handle) == 0);
  portal_destroySession(portal, &handle);
  g_clear_pointer(&saved, g_free);
  assert(g_file_get_contents(tokenPath, &saved, NULL, NULL));
  assert(!strcmp(saved, "old-qxl-token"));

  g_atomic_int_set(&server.mode, NO_STREAM);
  assert(portal_createScreenCastSession(portal, &handle));
  assert(portal_selectSource(portal, handle, false));
  assert(portal_getPipewireNode(portal, handle) == 0);
  portal_destroySession(portal, &handle);
  g_clear_pointer(&saved, g_free);
  assert(g_file_get_contents(tokenPath, &saved, NULL, NULL));
  assert(!strcmp(saved, "old-qxl-token"));

  g_atomic_int_set(&server.mode, NORMAL);
  assert(portal_createScreenCastSession(portal, &handle));
  assert(portal_selectSource(portal, handle, false));
  assert(portal_getPipewireNode(portal, handle) == 42);
  portal_destroySession(portal, &handle);
  g_clear_pointer(&saved, g_free);
  assert(g_file_get_contents(tokenPath, &saved, NULL, NULL));
  assert(!strcmp(saved, "new-nvidia-token"));
  assert(stat(tokenPath, &tokenStat) == 0 && (tokenStat.st_mode & 0777) == 0600);

  /* Normal startup reuses the replacement selection and rotates its token. */
  g_atomic_int_set(&server.expectedToken, 2);
  g_atomic_int_set(&server.generation, 1);
  assert(portal_createScreenCastSession(portal, &handle));
  assert(portal_selectSource(portal, handle, true));
  assert(portal_getPipewireNode(portal, handle) == 42);
  portal_destroySession(portal, &handle);
  g_clear_pointer(&saved, g_free);
  assert(g_file_get_contents(tokenPath, &saved, NULL, NULL));
  assert(!strcmp(saved, "rotated-nvidia-token"));

  /* If persistence is denied, a successful fresh choice must forget QXL.
   * Keeping its still-unused token could silently switch back on next login. */
  assert(g_file_set_contents(tokenPath, "old-qxl-token", -1, NULL));
  g_atomic_int_set(&server.expectedToken, 0);
  g_atomic_int_set(&server.mode, NO_RESTORE);
  assert(portal_createScreenCastSession(portal, &handle));
  assert(portal_selectSource(portal, handle, false));
  assert(portal_getPipewireNode(portal, handle) == 42);
  portal_destroySession(portal, &handle);
  assert(!g_file_test(tokenPath, G_FILE_TEST_EXISTS));

  for (int mode = MISSING; mode <= CANCEL; ++mode)
  {
    g_atomic_int_set(&server.mode, mode);
    assert(!portal_createScreenCastSession(portal, &handle));
    assert(!handle);
  }
  portal_free(portal);
  g_dbus_connection_close_sync(client, NULL, NULL);
  g_object_unref(client);
  g_main_context_invoke(server.context, stop, &server);
  g_thread_join(thread);
  g_cond_clear(&server.condition);
  g_mutex_clear(&server.mutex);
  g_test_dbus_down(bus);
  g_clear_pointer(&saved, g_free);
  g_autofree char * directory = g_path_get_dirname(tokenPath);
  assert(g_rmdir(directory) == 0);
  assert(g_rmdir(config) == 0);
  puts("PASS: portal lifecycle, FD transfer, fresh monitor choice, token rotation, denied persistence and cancelled-choice preservation");
  return 0;
}
