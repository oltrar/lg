/**
 * Looking Glass
 * Copyright (C) 2017-2025 The Looking Glass Authors
 * Copyright (C) 2026 Linux Wayland integration contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "portal.h"
#include "common/debug.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>

#define PORTAL_NAME "org.freedesktop.portal.Desktop"
#define PORTAL_PATH "/org/freedesktop/portal/desktop"

struct Portal
{
  GDBusConnection * conn;
  GDBusProxy * screenCast;
  char * senderName;
  char * tokenPath;
  unsigned serial;
};
struct Request
{
  bool completed, expired;
  uint32_t status;
  GVariant * results;
};

struct Portal * portal_create(void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char * key = NULL;
  g_autofree char * directory = NULL;
  struct Portal * p = calloc(1, sizeof(*p));
  if (!p)
    return NULL;
  p->conn = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
  if (!p->conn)
    goto fail;
  const char * name = g_dbus_connection_get_unique_name(p->conn);
  if (!name || name[0] != ':')
    goto fail;
  p->senderName = g_strdup(name + 1);
  for (char * c = p->senderName; *c; ++c)
    if (*c == '.') *c = '_';
  p->screenCast = g_dbus_proxy_new_sync(p->conn, G_DBUS_PROXY_FLAGS_NONE,
    NULL, PORTAL_NAME, PORTAL_PATH, "org.freedesktop.portal.ScreenCast",
    NULL, &error);
  if (!p->screenCast)
    goto fail;
  const char * desktop = g_getenv("XDG_CURRENT_DESKTOP");
  key = g_ascii_strdown(desktop ? desktop : "wayland", -1);
  for (char * c = key; *c; ++c)
    if (!g_ascii_isalnum(*c)) *c = '_';
  directory = g_build_filename(g_get_user_config_dir(),
    "looking-glass-linux-wayland", NULL);
  if (g_mkdir_with_parents(directory, 0700) == 0)
    p->tokenPath = g_strdup_printf("%s/restore-%s", directory, key);
  return p;
fail:
  DEBUG_ERROR("Cannot open desktop portal: %s",
    error ? error->message : "missing session bus identity");
  portal_free(p);
  return NULL;
}
void portal_free(struct Portal * p)
{
  if (!p) return;
  g_clear_object(&p->screenCast);
  g_clear_object(&p->conn);
  g_free(p->senderName);
  g_free(p->tokenPath);
  free(p);
}
static void responseCallback(GDBusConnection * connection, const char * sender,
  const char * path, const char * interface, const char * signal,
  GVariant * parameters, void * opaque)
{
  struct Request * request = opaque;
  if (request->completed)
    return;
  g_variant_get(parameters, "(u@a{sv})", &request->status, &request->results);
  request->completed = true;
}
static gboolean requestExpired(void * opaque)
{
  ((struct Request *)opaque)->expired = true;
  return G_SOURCE_REMOVE;
}

/* Subscribe before making the call: fast portal replies must not be missed.
 * Each request has a unique token; cancellation/failure never waits forever. */
static GVariant * request(struct Portal * p, const char * method,
  const char * session, GVariantBuilder * options)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) reply = NULL;
  g_autofree char * token = g_strdup_printf("lg%u_%u", (unsigned)getpid(), ++p->serial);
  g_autofree char * path = g_strdup_printf(PORTAL_PATH "/request/%s/%s",
    p->senderName, token);
  struct Request state = {0};
  guint subscription = g_dbus_connection_signal_subscribe(p->conn, PORTAL_NAME,
    "org.freedesktop.portal.Request", "Response", path, NULL,
    G_DBUS_SIGNAL_FLAGS_NONE, responseCallback, &state, NULL);
  guint timeout = 0;
  g_variant_builder_add(options, "{sv}", "handle_token", g_variant_new_string(token));
  if (!session)
    g_variant_builder_add(options, "{sv}", "session_handle_token", g_variant_new_string(token));

  GVariant * parameters = !session ? g_variant_new("(a{sv})", options) :
    strcmp(method, "Start") == 0 ? g_variant_new("(osa{sv})", session, "", options) :
    g_variant_new("(oa{sv})", session, options);
  reply = g_dbus_proxy_call_sync(p->screenCast, method, parameters,
    G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &error);
  if (!reply)
    goto done;
  const char * actualPath;
  g_variant_get(reply, "(&o)", &actualPath);
  if (strcmp(actualPath, path) != 0)
  {
    DEBUG_ERROR("Portal returned an unexpected request path");
    goto done;
  }
  timeout = g_timeout_add_seconds(120, requestExpired, &state);
  while (!state.completed && !state.expired)
    g_main_context_iteration(NULL, TRUE);

done:
  if (timeout && !state.expired)
    g_source_remove(timeout);
  g_dbus_connection_signal_unsubscribe(p->conn, subscription);
  if (error || !state.completed || state.status != 0)
  {
    DEBUG_ERROR("Portal %s failed: %s (response %u)", method,
      error ? error->message : state.expired ? "selection timed out" :
      "cancelled or rejected", state.status);
    if (!state.completed && reply)
      g_dbus_connection_call_sync(p->conn, PORTAL_NAME, path,
        "org.freedesktop.portal.Request", "Close", NULL, NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    g_clear_pointer(&state.results, g_variant_unref);
  }
  return state.results;
}
bool portal_createScreenCastSession(struct Portal * p, char ** handle)
{
  *handle = NULL;
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_autoptr(GVariant) result = request(p, "CreateSession", NULL, &options);
  if (!result)
    return false;
  /* The portal spec intentionally returns this object path as a STRING ('s'),
   * not an object-path variant ('o'), for historical compatibility. */
  if (!g_variant_lookup(result, "session_handle", "s", handle))
  {
    DEBUG_ERROR("Portal CreateSession response lacks string session_handle");
    return false;
  }
  if (!g_variant_is_object_path(*handle))
  {
    DEBUG_ERROR("Portal CreateSession returned an invalid session object path");
    g_clear_pointer(handle, g_free);
    return false;
  }
  return true;
}
void portal_destroySession(struct Portal * p, char ** handle)
{
  if (!handle || !*handle) return;
  g_dbus_connection_call_sync(p->conn, PORTAL_NAME, *handle,
    "org.freedesktop.portal.Session", "Close", NULL, NULL,
    G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
  g_clear_pointer(handle, g_free);
}
bool portal_selectSource(struct Portal * p, const char * session, bool restore)
{
  g_autoptr(GVariant) version = g_dbus_proxy_get_cached_property(p->screenCast,
    "version");
  g_autofree char * token = NULL;
  bool restoring = false;
  g_autoptr(GVariant) modes = g_dbus_proxy_get_cached_property(p->screenCast,
    "AvailableCursorModes");
  if (!modes || !(g_variant_get_uint32(modes) & 2))
  {
    DEBUG_ERROR("The portal must support an embedded cursor (KDE/GNOME)");
    return false;
  }
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(1));
  g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(FALSE));
  g_variant_builder_add(&options, "{sv}", "cursor_mode", g_variant_new_uint32(2));
  if (version && g_variant_get_uint32(version) >= 4 && p->tokenPath)
  {
    g_variant_builder_add(&options, "{sv}", "persist_mode", g_variant_new_uint32(2));
    gsize length = 0;
    if (restore && g_file_get_contents(p->tokenPath, &token, &length, NULL) &&
        length > 0 && length < 4096 && g_utf8_validate(token, length, NULL))
    {
      g_variant_builder_add(&options, "{sv}", "restore_token", g_variant_new_string(token));
      restoring = true;
      DEBUG_INFO("Requesting the saved monitor selection; use --choose-display to change it");
    }
  }
  if (!restoring)
    DEBUG_INFO("Requesting monitor selection: choose the NVIDIA/dummy-plug display in the guest portal");
  g_autoptr(GVariant) result = request(p, "SelectSources", session, &options);
  return result != NULL;
}
uint32_t portal_getPipewireNode(struct Portal * p, const char * session)
{
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_autoptr(GVariant) result = request(p, "Start", session, &options);
  g_autoptr(GVariant) streams = result ? g_variant_lookup_value(result,
    "streams", G_VARIANT_TYPE("a(ua{sv})")) : NULL;
  g_autoptr(GVariant) properties = NULL;
  g_autofree char * token = NULL;
  uint32_t node = 0;
  if (!streams || !g_variant_n_children(streams))
  {
    DEBUG_ERROR("Portal returned no monitor stream");
    return 0;
  }
  g_variant_get_child(streams, 0, "(u@a{sv})", &node, &properties);
  const char * id = NULL;
  int width = 0, height = 0;
  g_variant_lookup(properties, "id", "&s", &id);
  g_variant_lookup(properties, "size", "(ii)", &width, &height);
  DEBUG_INFO("Portal selected stream: node %u, id %s, logical size %dx%d",
    node, id ? id : "not supplied", width, height);
  if (p->tokenPath && g_variant_lookup(result, "restore_token", "s", &token))
  {
    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents_full(p->tokenPath, token, -1,
          G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
          0600, &error))
      DEBUG_WARN("Cannot save portal selection: %s", error->message);
    else
      DEBUG_INFO("Saved monitor selection for future capture startup");
  }
  return node;
}
int portal_openPipewireRemote(struct Portal * p, const char * session)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GUnixFDList) fds = NULL;
  GVariantBuilder options;
  g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
  g_autoptr(GVariant) response = g_dbus_proxy_call_with_unix_fd_list_sync(
    p->screenCast, "OpenPipeWireRemote", g_variant_new("(oa{sv})", session, &options),
    G_DBUS_CALL_FLAGS_NONE, 30000, NULL, &fds, NULL, &error);
  if (!response)
  {
    DEBUG_ERROR("Cannot open PipeWire remote: %s", error->message);
    return -1;
  }
  gint32 index;
  g_variant_get(response, "(h)", &index);
  int fd = g_unix_fd_list_get(fds, index, &error);
  if (fd < 0)
    DEBUG_ERROR("Cannot get PipeWire descriptor: %s", error->message);
  return fd;
}
