/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Exercise the production portal client against a real private D-Bus service.
 * In particular CreateSession MUST return session_handle as 's', not 'o'. */
#undef NDEBUG
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include "portal.h"

#define NAME "org.freedesktop.portal.Desktop"
#define PATH "/org/freedesktop/portal/desktop"
#define SESSION PATH "/session/test/session"

enum ResponseMode { NORMAL, MISSING, WRONG_TYPE, INVALID_PATH, CANCEL };
struct Server
{
  const char * address;
  GMainContext * context;
  GMainLoop * loop;
  GMutex mutex;
  GCond condition;
  bool ready;
  gint mode, closeCount;
};

static const char xml[] =
  "<node><interface name='org.freedesktop.portal.ScreenCast'>"
  "<method name='CreateSession'><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
  "<method name='SelectSources'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
  "<method name='Start'><arg type='o' direction='in'/><arg type='s' direction='in'/><arg type='a{sv}' direction='in'/><arg type='o' direction='out'/></method>"
  "<method name='OpenPipeWireRemote'><arg type='o' direction='in'/><arg type='a{sv}' direction='in'/><arg type='h' direction='out'/></method>"
  "<property name='AvailableCursorModes' type='u' access='read'/>"
  "<property name='version' type='u' access='read'/>"
  "</interface><interface name='org.freedesktop.portal.Session'>"
  "<method name='Close'/></interface></node>";

static GVariant * property(GDBusConnection * conn, const char * sender,
  const char * path, const char * interface, const char * name,
  GError ** error, void * opaque)
{
  return g_variant_new_uint32(!strcmp(name, "version") ? 4 : 2);
}

static void method(GDBusConnection * conn, const char * sender, const char * path,
  const char * interface, const char * name, GVariant * parameters,
  GDBusMethodInvocation * invocation, void * opaque)
{
  struct Server * server = opaque;
  if (!strcmp(name, "Close"))
  {
    g_atomic_int_inc(&server->closeCount);
    g_dbus_method_invocation_return_value(invocation, NULL);
    return;
  }
  if (!strcmp(name, "OpenPipeWireRemote"))
  {
    g_autoptr(GUnixFDList) fds = g_unix_fd_list_new();
    int fd = open("/dev/null", O_RDONLY);
    assert(fd >= 0);
    int index = g_unix_fd_list_append(fds, fd, NULL);
    close(fd);
    assert(index >= 0);
    g_dbus_method_invocation_return_value_with_unix_fd_list(invocation,
      g_variant_new("(h)", index), fds);
    return;
  }

  bool create = !strcmp(name, "CreateSession");
  g_autoptr(GVariant) options = g_variant_get_child_value(parameters,
    create ? 0 : !strcmp(name, "Start") ? 2 : 1);
  const char * token = NULL;
  assert(g_variant_lookup(options, "handle_token", "&s", &token));
  if (!create)
  {
    g_autoptr(GVariant) session = g_variant_get_child_value(parameters, 0);
    assert(g_variant_is_of_type(session, G_VARIANT_TYPE_OBJECT_PATH));
    assert(!strcmp(g_variant_get_string(session, NULL), SESSION));
  }
  if (!strcmp(name, "SelectSources"))
  {
    guint32 mode = 0;
    assert(g_variant_lookup(options, "cursor_mode", "u", &mode) && mode == 2);
  }
  g_autofree char * id = g_strdup(sender + 1);
  for (char * p = id; *p; ++p) if (*p == '.') *p = '_';
  g_autofree char * requestPath = g_strdup_printf(PATH "/request/%s/%s", id, token);
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
  int responseMode = g_atomic_int_get(&server->mode);
  if (create && responseMode == NORMAL)
    g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_string(SESSION));
  else if (create && responseMode == WRONG_TYPE)
    g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_uint32(42));
  else if (create && responseMode == INVALID_PATH)
    g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_string("not/a/path"));
  if (!strcmp(name, "Start"))
  {
    GVariantBuilder streams, props;
    g_variant_builder_init(&streams, G_VARIANT_TYPE("a(ua{sv})"));
    g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&streams, "(ua{sv})", 42u, &props);
    g_variant_builder_add(&results, "{sv}", "streams", g_variant_builder_end(&streams));
  }
  /* Reply promptly, as a real portal does. Signals are deliberately emitted
   * immediately afterward to test subscribe-before-call handling. */
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", requestPath));
  g_autoptr(GError) error = NULL;
  assert(g_dbus_connection_emit_signal(conn, sender, requestPath,
    "org.freedesktop.portal.Request", "Response",
    g_variant_new("(ua{sv})", create && responseMode == CANCEL ? 1u : 0u, &results), &error));
  assert(!error);
}

static const GDBusInterfaceVTable vtable = {
  .method_call = method, .get_property = property
};

static gpointer serve(gpointer opaque)
{
  struct Server * server = opaque;
  g_autoptr(GError) error = NULL;
  GMainContext * context = g_main_context_new();
  g_main_context_push_thread_default(context);
  GMainLoop * loop = g_main_loop_new(context, FALSE);
  g_autoptr(GDBusConnection) conn = g_dbus_connection_new_for_address_sync(
    server->address, G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
    G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL, &error);
  assert(conn && !error);
  g_autoptr(GVariant) result = g_dbus_connection_call_sync(conn,
    "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
    "RequestName", g_variant_new("(su)", NAME, 0u), G_VARIANT_TYPE("(u)"),
    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, &error);
  assert(result && !error);
  g_autoptr(GDBusNodeInfo) info = g_dbus_node_info_new_for_xml(xml, &error);
  assert(info && !error);
  guint cast = g_dbus_connection_register_object(conn, PATH, info->interfaces[0],
    &vtable, server, NULL, &error);
  assert(cast && !error);
  guint session = g_dbus_connection_register_object(conn, SESSION, info->interfaces[1],
    &vtable, server, NULL, &error);
  assert(session && !error);
  g_mutex_lock(&server->mutex);
  server->context = context;
  server->loop = loop;
  server->ready = true;
  g_cond_signal(&server->condition);
  g_mutex_unlock(&server->mutex);
  g_main_loop_run(loop);
  g_dbus_connection_unregister_object(conn, cast);
  g_dbus_connection_unregister_object(conn, session);
  g_dbus_connection_close_sync(conn, NULL, NULL);
  g_main_loop_unref(loop);
  g_main_context_pop_thread_default(context);
  g_main_context_unref(context);
  return NULL;
}

static gboolean stop(gpointer opaque)
{
  g_main_loop_quit(((struct Server *)opaque)->loop);
  return G_SOURCE_REMOVE;
}

int main(int argc, char ** argv)
{
  g_test_init(&argc, &argv, NULL);
  g_autoptr(GError) error = NULL;
  g_autofree char * config = g_dir_make_tmp("lg-portal-config-XXXXXX", &error);
  assert(config && !error);
  g_setenv("XDG_CONFIG_HOME", config, TRUE);
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
  assert(portal_selectSource(portal, handle));
  assert(portal_getPipewireNode(portal, handle) == 42);
  int fd = portal_openPipewireRemote(portal, handle);
  assert(fd >= 0 && fcntl(fd, F_GETFD) >= 0);
  close(fd);
  portal_destroySession(portal, &handle);
  assert(!handle && g_atomic_int_get(&server.closeCount) == 1);
  portal_destroySession(portal, &handle);
  assert(g_atomic_int_get(&server.closeCount) == 1);

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
  puts("PASS: portal string session_handle, real D-Bus lifecycle, FD transfer, cancellation and invalid replies");
  return 0;
}
