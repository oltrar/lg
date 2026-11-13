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
#include <glib/gstdio.h>
#include <sys/stat.h>
#include "portal.h"
#include <sys/socket.h>
#include <sys/un.h>

#define NAME "org.freedesktop.portal.Desktop"
#define PATH "/org/freedesktop/portal/desktop"
#define SESSION PATH "/session/test/session"

enum ResponseMode { NORMAL, MISSING, WRONG_TYPE, INVALID_PATH, CANCEL, CANCEL_START, NO_STREAM, NO_RESTORE };
struct Server
{
  const char * address;
  GMainContext * context;
  GMainLoop * loop;
  GMutex mutex;
  GCond condition;
  bool ready;
  gint mode, closeCount, expectedToken, generation, createCount;
  const char * pipewireSocket;
  uint32_t node;
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
    int fd;
    if (server->pipewireSocket)
    {
      fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      struct sockaddr_un address = {.sun_family = AF_UNIX};
      assert(strlen(server->pipewireSocket) < sizeof(address.sun_path));
      strcpy(address.sun_path, server->pipewireSocket);
      assert(fd >= 0 && connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    }
    else fd = open("/dev/null", O_RDONLY);
    assert(fd >= 0);
    int index = g_unix_fd_list_append(fds, fd, NULL);
    close(fd);
    assert(index >= 0);
    g_dbus_method_invocation_return_value_with_unix_fd_list(invocation,
      g_variant_new("(h)", index), fds);
    return;
  }

  bool create = !strcmp(name, "CreateSession");
  if (create) g_atomic_int_inc(&server->createCount);
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
    assert(g_variant_lookup(options, "types", "u", &mode) && mode == 1);
    assert(g_variant_lookup(options, "persist_mode", "u", &mode) && mode == 2);
    gboolean multiple = TRUE;
    assert(g_variant_lookup(options, "multiple", "b", &multiple) && !multiple);
    const char * restore = NULL;
    bool hasToken = g_variant_lookup(options, "restore_token", "&s", &restore);
    int expected = g_atomic_int_get(&server->expectedToken);
    assert(hasToken == (expected != 0));
    if (expected == 1) assert(!strcmp(restore, "old-qxl-token"));
    if (expected == 2) assert(!strcmp(restore, "new-nvidia-token"));
  }
  g_autofree char * id = g_strdup(sender + 1);
  for (char * p = id; *p; ++p) if (*p == '.') *p = '_';
  g_autofree char * requestPath = g_strdup_printf(PATH "/request/%s/%s", id, token);
  GVariantBuilder results;
  g_variant_builder_init(&results, G_VARIANT_TYPE_VARDICT);
  int responseMode = g_atomic_int_get(&server->mode);
  if (create && (responseMode == NORMAL || responseMode == CANCEL_START ||
                 responseMode == NO_STREAM || responseMode == NO_RESTORE))
    g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_string(SESSION));
  else if (create && responseMode == WRONG_TYPE)
    g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_uint32(42));
  else if (create && responseMode == INVALID_PATH)
    g_variant_builder_add(&results, "{sv}", "session_handle", g_variant_new_string("not/a/path"));
  if (!strcmp(name, "Start") && responseMode != NO_STREAM)
  {
    GVariantBuilder streams, props;
    g_variant_builder_init(&streams, G_VARIANT_TYPE("a(ua{sv})"));
    g_variant_builder_init(&props, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&props, "{sv}", "id", g_variant_new_string("test-nvidia-monitor"));
    g_variant_builder_add(&props, "{sv}", "size", g_variant_new("(ii)", 1920, 1080));
    g_variant_builder_add(&streams, "(ua{sv})", server->node ? server->node : 42u, &props);
    g_variant_builder_add(&results, "{sv}", "streams", g_variant_builder_end(&streams));
    if (responseMode != NO_RESTORE)
      g_variant_builder_add(&results, "{sv}", "restore_token", g_variant_new_string(
        g_atomic_int_get(&server->generation) ? "rotated-nvidia-token" : "new-nvidia-token"));
  }
  /* Reply promptly, as a real portal does. Signals are deliberately emitted
   * immediately afterward to test subscribe-before-call handling. */
  g_dbus_method_invocation_return_value(invocation, g_variant_new("(o)", requestPath));
  g_autoptr(GError) error = NULL;
  assert(g_dbus_connection_emit_signal(conn, sender, requestPath,
    "org.freedesktop.portal.Request", "Response",
    g_variant_new("(ua{sv})", (create && responseMode == CANCEL) ||
      (!strcmp(name, "Start") && responseMode == CANCEL_START) ? 1u : 0u, &results), &error));
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

