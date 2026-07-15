#include "application.h"

#include "device.h"
#include "discovery.h"
#include "l2-provision.h"
#include "network.h"
#include "web-provision.h"
#include "web-uri.h"

#include <string.h>

enum {
  COL_TYPE,
  COL_IP,
  COL_MAC,
  COL_MODEL,
  COL_NAME,
  COL_FIRMWARE,
  COL_DATA_PORT,
  COL_HTTP_PORT,
  COL_DEVICE,
  N_COLUMNS,
};

typedef struct {
  GtkApplication *application;
  GtkWindow *window;
  GtkListStore *store;
  GtkTreeModelFilter *filtered;
  GtkTreeSelection *selection;
  GtkWidget *tree_view;
  GtkWidget *search_entry;
  GtkWidget *type_combo;
  GtkWidget *port_start;
  GtkWidget *port_end;
  GtkWidget *auto_combo;
  GtkWidget *refresh_button;
  GtkWidget *status_label;
  GtkWidget *identity_label;
  GtkWidget *model_label;
  GtkWidget *firmware_label;
  GtkWidget *ip_entry;
  GtkWidget *mask_entry;
  GtkWidget *gateway_entry;
  GtkWidget *dhcp_check;
  GtkWidget *password_entry;
  GtkWidget *apply_button;
  char *bind_address;
  guint timeout_ms;
  guint auto_source;
  gboolean scanning;
} App;

typedef struct {
  char *mac;
  char *old_ip;
  char *new_ip;
  char *subnet_mask;
  char *gateway;
  char *password;
  guint16 http_port;
  guint32 protocol_version;
  gboolean dhcp;
  char *bind_address;
  guint timeout_ms;
} ModifyJob;

typedef struct {
  gboolean verified;
  gboolean dhcp;
  gboolean observed_dhcp;
  gboolean used_web;
  char *observed_ip;
} ModifyWorkResult;

static const char *device_types[] = {
  "All devices", "IPC", "NVR", "DVR", "MDVR", "Storage", "Decoder", "Network keyboard", "Unknown", NULL
};

static const char *auto_labels[] = {
  "Auto refresh: off", "Every 3 seconds", "Every 5 seconds", "Every 10 seconds",
  "Every 15 seconds", "Every 30 seconds", "Every minute", NULL
};

static const guint auto_seconds[] = { 0, 3, 5, 10, 15, 30, 60 };

static void start_discovery(App *app);

static void
modify_job_free(ModifyJob *job)
{
  if (!job)
    return;
  if (job->password) {
    memset(job->password, 0, strlen(job->password));
    g_free(job->password);
  }
  g_free(job->mac);
  g_free(job->old_ip);
  g_free(job->new_ip);
  g_free(job->subnet_mask);
  g_free(job->gateway);
  g_free(job->bind_address);
  g_free(job);
}

static void
modify_work_result_free(ModifyWorkResult *result)
{
  if (!result)
    return;
  g_free(result->observed_ip);
  g_free(result);
}

static void
app_free(App *app)
{
  if (!app)
    return;
  if (app->auto_source)
    g_source_remove(app->auto_source);
  g_free(app->bind_address);
  g_free(app);
}

static GtkWidget *
label_with_style(const char *text, const char *style_class)
{
  GtkWidget *label = gtk_label_new(text);
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  if (style_class)
    gtk_style_context_add_class(gtk_widget_get_style_context(label), style_class);
  return label;
}

static void
show_notice(App *app, const char *title, const char *message, gboolean is_error)
{
  GtkWidget *dialog = gtk_message_dialog_new(
    app->window,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    is_error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO,
    GTK_BUTTONS_CLOSE,
    "%s",
    title);
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", message);
  gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
}

static TvtDevice *
selected_device(App *app)
{
  GtkTreeModel *model = NULL;
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected(app->selection, &model, &iter))
    return NULL;
  TvtDevice *device = NULL;
  gtk_tree_model_get(model, &iter, COL_DEVICE, &device, -1);
  return device;
}

static void
open_device_web(App *app, TvtDevice *device)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *uri = tvt_device_web_uri(device, &error);
  if (!uri || !g_app_info_launch_default_for_uri(uri, NULL, &error)) {
    show_notice(app, "Could not open device", error ? error->message : "No web address is available.", TRUE);
    return;
  }
  g_autofree char *status = g_strdup_printf("Opened %s", uri);
  gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

static void
row_activated(GtkTreeView *tree_view, GtkTreePath *path, GtkTreeViewColumn *column,
              gpointer user_data)
{
  App *app = user_data;
  (void)column;
  gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
  TvtDevice *device = selected_device(app);
  if (!device)
    return;
  open_device_web(app, device);
  g_object_unref(device);
}

static gboolean
filter_visible(GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
  App *app = user_data;
  TvtDevice *device = NULL;
  gtk_tree_model_get(model, iter, COL_DEVICE, &device, -1);
  if (!device)
    return FALSE;

  const char *query = gtk_entry_get_text(GTK_ENTRY(app->search_entry));
  int type_index = gtk_combo_box_get_active(GTK_COMBO_BOX(app->type_combo));
  const char *type = type_index >= 0 && (guint)type_index < G_N_ELEMENTS(device_types) - 1
                       ? device_types[type_index] : device_types[0];
  guint start = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->port_start));
  guint end = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->port_end));
  guint data_port = tvt_device_get_data_port(device);
  guint http_port = tvt_device_get_http_port(device);
  gboolean visible = tvt_device_matches(device, query, type) &&
                     ((data_port >= start && data_port <= end) ||
                      (http_port >= start && http_port <= end));
  g_object_unref(device);
  return visible;
}

static void
filter_changed(GtkWidget *widget, gpointer user_data)
{
  App *app = user_data;
  (void)widget;
  gtk_tree_model_filter_refilter(app->filtered);
}

static void
set_entry(GtkWidget *entry, const char *text)
{
  gtk_entry_set_text(GTK_ENTRY(entry), text ? text : "");
}

static void
selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
  App *app = user_data;
  (void)selection;
  TvtDevice *device = selected_device(app);
  if (!device) {
    gtk_label_set_text(GTK_LABEL(app->identity_label), "Select a device");
    gtk_label_set_text(GTK_LABEL(app->model_label), "");
    gtk_label_set_text(GTK_LABEL(app->firmware_label), "");
    set_entry(app->ip_entry, "");
    set_entry(app->mask_entry, "");
    set_entry(app->gateway_entry, "");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->dhcp_check), FALSE);
    gtk_widget_set_sensitive(app->apply_button, FALSE);
    return;
  }

  g_autofree char *identity = g_strdup_printf("%s  ·  %s", tvt_device_get_ip(device), tvt_device_get_mac(device));
  g_autofree char *model = g_strdup_printf("%s  ·  %s", tvt_device_get_device_type(device), tvt_device_get_model(device));
  g_autofree char *firmware = g_strdup_printf("Firmware %s  ·  Data %u  ·  HTTP %u",
                                               tvt_device_get_firmware(device),
                                               tvt_device_get_data_port(device),
                                               tvt_device_get_http_port(device));
  gtk_label_set_text(GTK_LABEL(app->identity_label), identity);
  gtk_label_set_text(GTK_LABEL(app->model_label), model);
  gtk_label_set_text(GTK_LABEL(app->firmware_label), firmware);
  set_entry(app->ip_entry, tvt_device_get_ip(device));
  set_entry(app->mask_entry, tvt_device_get_subnet_mask(device));
  set_entry(app->gateway_entry, tvt_device_get_gateway(device));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->dhcp_check), tvt_device_get_dhcp(device));
  set_entry(app->password_entry, "");
  gtk_widget_set_sensitive(app->apply_button, *tvt_device_get_mac(device) != '\0');
  g_object_unref(device);
}

static gboolean
has_duplicate_ip(App *app, const char *new_ip, const char *selected_mac)
{
  GtkTreeModel *model = GTK_TREE_MODEL(app->store);
  GtkTreeIter iter;
  gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
  while (valid) {
    TvtDevice *device = NULL;
    gtk_tree_model_get(model, &iter, COL_DEVICE, &device, -1);
    gboolean duplicate = device && g_str_equal(tvt_device_get_ip(device), new_ip) &&
                         g_ascii_strcasecmp(tvt_device_get_mac(device), selected_mac) != 0;
    if (device)
      g_object_unref(device);
    if (duplicate)
      return TRUE;
    valid = gtk_tree_model_iter_next(model, &iter);
  }
  return FALSE;
}

static void
modify_worker(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  ModifyJob *job = task_data;
  (void)source_object;
  ModifyWorkResult *result = g_new0(ModifyWorkResult, 1);
  result->dhcp = job->dhcp;
  g_autoptr(GError) web_error = NULL;
  if (tvt_web_set_network(job->old_ip, job->http_port, job->mac, job->password,
                          job->new_ip, job->subnet_mask, job->gateway, job->dhcp,
                          cancellable, &web_error)) {
    result->used_web = TRUE;
  } else {
    g_autoptr(GError) l2_error = NULL;
    if (!tvt_l2_send_set_ip(job->bind_address, job->protocol_version,
                            job->mac, job->password, job->new_ip,
                            job->subnet_mask, job->gateway, job->dhcp, &l2_error)) {
      g_autofree char *message = g_strdup_printf(
        "Authenticated web configuration failed: %s. Layer-2 fallback failed: %s",
        web_error->message, l2_error->message);
      modify_work_result_free(result);
      g_task_return_new_error(task, TVT_WEB_PROVISION_ERROR,
                              TVT_WEB_PROVISION_ERROR_PROTOCOL, "%s", message);
      return;
    }
  }
  if (job->password)
    memset(job->password, 0, strlen(job->password));

  for (guint attempt = 0; attempt < 40 && (!cancellable || !g_cancellable_is_cancelled(cancellable)); attempt++) {
    TvtDiscoveryOptions options = {
      .timeout_ms = MAX(1000, job->timeout_ms),
      .retries = 1,
      .bind_address = job->bind_address,
    };
    g_autoptr(GError) discover_error = NULL;
    GPtrArray *devices = tvt_discover_lan(&options, cancellable, &discover_error);
    if (devices) {
      for (guint i = 0; i < devices->len; i++) {
        TvtDevice *device = g_ptr_array_index(devices, i);
        if (g_ascii_strcasecmp(tvt_device_get_mac(device), job->mac) == 0) {
          g_free(result->observed_ip);
          result->observed_ip = g_strdup(tvt_device_get_ip(device));
          result->observed_dhcp = tvt_device_get_dhcp(device);
          if ((job->dhcp && result->observed_dhcp) ||
              (!job->dhcp && g_str_equal(result->observed_ip, job->new_ip))) {
            result->verified = TRUE;
            break;
          }
        }
      }
      g_ptr_array_unref(devices);
    }
    if (result->verified)
      break;
    g_usleep(500000);
  }
  g_task_return_pointer(task, result, (GDestroyNotify)modify_work_result_free);
}

static void
modify_done(GObject *source_object, GAsyncResult *async_result, gpointer user_data)
{
  App *app = user_data;
  (void)source_object;
  gtk_widget_set_sensitive(app->apply_button, TRUE);
  g_autoptr(GError) error = NULL;
  ModifyWorkResult *result = g_task_propagate_pointer(G_TASK(async_result), &error);
  if (!result) {
    gtk_label_set_text(GTK_LABEL(app->status_label), "Network change failed");
    show_notice(app, "Network change failed", error->message, TRUE);
    return;
  }
  if (!result->verified) {
    if (result->observed_ip) {
      gtk_label_set_text(GTK_LABEL(app->status_label), "Network change not applied");
      g_autofree char *message = result->dhcp
        ? g_strdup_printf("The device reappeared at %s with DHCP still disabled. The Layer-2 request "
                          "was sent, but the camera/NVR did not apply it. Check the administrator password and retry.",
                          result->observed_ip)
        : g_strdup_printf("The device is still advertising %s. The Layer-2 request was sent, but the camera/NVR "
                          "did not apply the requested address. Check the administrator password and retry.",
                          result->observed_ip);
      show_notice(app, "Network change not applied", message, TRUE);
    } else {
      gtk_label_set_text(GTK_LABEL(app->status_label), "Change sent; device not rediscovered");
      show_notice(app, "Verification required",
                  "The network request was accepted, but the device did not reappear during verification. "
                  "Refresh and locate the same MAC before changing NVR channel mappings.", TRUE);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(app->status_label), "Network change verified");
    g_autofree char *message = result->dhcp
      ? g_strdup_printf("The same device MAC reappeared at %s with DHCP enabled (%s).",
                        result->observed_ip, result->used_web ? "authenticated web API" : "Layer 2")
      : g_strdup_printf("The same device MAC reappeared at the requested IP address (%s).",
                        result->used_web ? "authenticated web API" : "Layer 2");
    show_notice(app, "Network change verified", message, FALSE);
  }
  modify_work_result_free(result);
  start_discovery(app);
}

static void
start_modify(App *app, ModifyJob *job)
{
  gtk_widget_set_sensitive(app->apply_button, FALSE);
  gtk_label_set_text(GTK_LABEL(app->status_label), "Applying network change…");
  GTask *task = g_task_new(app->application, NULL, modify_done, app);
  g_task_set_task_data(task, job, (GDestroyNotify)modify_job_free);
  g_task_run_in_thread(task, modify_worker);
  g_object_unref(task);
}

static void
dhcp_toggled(GtkToggleButton *button, gpointer user_data)
{
  App *app = user_data;
  gboolean editable = !gtk_toggle_button_get_active(button);
  gtk_widget_set_sensitive(app->ip_entry, editable);
  gtk_widget_set_sensitive(app->mask_entry, editable);
  gtk_widget_set_sensitive(app->gateway_entry, editable);
}

static void
apply_clicked(GtkButton *button, gpointer user_data)
{
  App *app = user_data;
  (void)button;
  TvtDevice *device = selected_device(app);
  if (!device)
    return;

  const char *new_ip = gtk_entry_get_text(GTK_ENTRY(app->ip_entry));
  const char *mask = gtk_entry_get_text(GTK_ENTRY(app->mask_entry));
  const char *gateway = gtk_entry_get_text(GTK_ENTRY(app->gateway_entry));
  g_autofree char *warning = NULL;
  g_autoptr(GError) error = NULL;
  if (!tvt_network_validate(new_ip, mask, gateway, &warning, &error)) {
    show_notice(app, "Invalid network settings", error->message, TRUE);
    g_object_unref(device);
    return;
  }
  if (has_duplicate_ip(app, new_ip, tvt_device_get_mac(device))) {
    show_notice(app, "IP address already discovered",
                "Another discovered device already uses this IP address. Resolve that conflict before applying changes.",
                TRUE);
    g_object_unref(device);
    return;
  }

  ModifyJob *job = g_new0(ModifyJob, 1);
  job->mac = g_strdup(tvt_device_get_mac(device));
  job->old_ip = g_strdup(tvt_device_get_ip(device));
  job->new_ip = g_strdup(new_ip);
  job->subnet_mask = g_strdup(mask);
  job->gateway = g_strdup(gateway);
  job->password = g_strdup(gtk_entry_get_text(GTK_ENTRY(app->password_entry)));
  job->http_port = (guint16)tvt_device_get_http_port(device);
  job->protocol_version = tvt_device_get_protocol_version(device);
  job->dhcp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->dhcp_check));
  set_entry(app->password_entry, "");
  job->bind_address = g_strdup(app->bind_address);
  job->timeout_ms = app->timeout_ms;
  g_object_unref(device);

  g_autofree char *summary = g_strdup_printf(
    "Device: %s\nCurrent IP: %s\nMode: %s\n%s IP: %s\nSubnet mask: %s\nGateway: %s%s%s",
    job->mac, job->old_ip, job->dhcp ? "DHCP" : "Static",
    job->dhcp ? "Fallback" : "New", job->new_ip, job->subnet_mask, job->gateway,
    warning ? "\n\nWarning: " : "", warning ? warning : "");
  GtkWidget *dialog = gtk_message_dialog_new(
    app->window,
    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
    GTK_MESSAGE_QUESTION,
    GTK_BUTTONS_CANCEL,
    "%s",
    "Confirm network change");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", summary);
  gtk_dialog_add_button(GTK_DIALOG(dialog), "Apply and verify", GTK_RESPONSE_ACCEPT);
  int response = gtk_dialog_run(GTK_DIALOG(dialog));
  gtk_widget_destroy(dialog);
  if (response == GTK_RESPONSE_ACCEPT)
    start_modify(app, job);
  else
    modify_job_free(job);
}

static void
discovery_worker(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  TvtDiscoveryOptions *options = task_data;
  (void)source_object;
  g_autoptr(GError) error = NULL;
  GPtrArray *devices = tvt_discover_lan(options, cancellable, &error);
  if (!devices)
    g_task_return_error(task, g_steal_pointer(&error));
  else
    g_task_return_pointer(task, devices, (GDestroyNotify)g_ptr_array_unref);
}

static void
discovery_options_free(TvtDiscoveryOptions *options)
{
  if (!options)
    return;
  g_free((char *)options->bind_address);
  g_free(options);
}

static void
append_device(App *app, TvtDevice *device)
{
  GtkTreeIter iter;
  g_autofree char *data_port = tvt_device_get_data_port(device)
                                ? g_strdup_printf("%u", tvt_device_get_data_port(device)) : g_strdup("");
  g_autofree char *http_port = tvt_device_get_http_port(device)
                                ? g_strdup_printf("%u", tvt_device_get_http_port(device)) : g_strdup("");
  gtk_list_store_append(app->store, &iter);
  gtk_list_store_set(app->store, &iter,
                     COL_TYPE, tvt_device_get_device_type(device),
                     COL_IP, tvt_device_get_ip(device),
                     COL_MAC, tvt_device_get_mac(device),
                     COL_MODEL, tvt_device_get_model(device),
                     COL_NAME, tvt_device_get_name(device),
                     COL_FIRMWARE, tvt_device_get_firmware(device),
                     COL_DATA_PORT, data_port,
                     COL_HTTP_PORT, http_port,
                     COL_DEVICE, device,
                     -1);
}

static void
select_mac(App *app, const char *mac)
{
  if (!mac || !*mac)
    return;
  GtkTreeModel *model = GTK_TREE_MODEL(app->filtered);
  GtkTreeIter iter;
  gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
  while (valid) {
    TvtDevice *candidate = NULL;
    gtk_tree_model_get(model, &iter, COL_DEVICE, &candidate, -1);
    gboolean match = candidate && g_ascii_strcasecmp(tvt_device_get_mac(candidate), mac) == 0;
    if (candidate)
      g_object_unref(candidate);
    if (match) {
      gtk_tree_selection_select_iter(app->selection, &iter);
      return;
    }
    valid = gtk_tree_model_iter_next(model, &iter);
  }
}

static void
discovery_done(GObject *source_object, GAsyncResult *async_result, gpointer user_data)
{
  App *app = user_data;
  (void)source_object;
  app->scanning = FALSE;
  gtk_widget_set_sensitive(app->refresh_button, TRUE);
  g_autoptr(GError) error = NULL;
  GPtrArray *devices = g_task_propagate_pointer(G_TASK(async_result), &error);
  if (!devices) {
    gtk_label_set_text(GTK_LABEL(app->status_label), error->message);
    return;
  }

  TvtDevice *selected = selected_device(app);
  g_autofree char *selected_mac = selected ? g_strdup(tvt_device_get_mac(selected)) : NULL;
  if (selected)
    g_object_unref(selected);
  gtk_list_store_clear(app->store);
  for (guint i = 0; i < devices->len; i++)
    append_device(app, g_ptr_array_index(devices, i));
  g_ptr_array_unref(devices);
  gtk_tree_model_filter_refilter(app->filtered);
  select_mac(app, selected_mac);

  guint count = (guint)gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app->filtered), NULL);
  g_autofree char *status = g_strdup_printf("%u device%s discovered", count, count == 1 ? "" : "s");
  gtk_label_set_text(GTK_LABEL(app->status_label), status);
}

static void
start_discovery(App *app)
{
  if (app->scanning)
    return;
  app->scanning = TRUE;
  gtk_widget_set_sensitive(app->refresh_button, FALSE);
  gtk_label_set_text(GTK_LABEL(app->status_label), "Searching the local network…");

  TvtDiscoveryOptions *options = g_new0(TvtDiscoveryOptions, 1);
  options->timeout_ms = app->timeout_ms;
  options->retries = 2;
  options->bind_address = g_strdup(app->bind_address);
  GTask *task = g_task_new(app->application, NULL, discovery_done, app);
  g_task_set_task_data(task, options, (GDestroyNotify)discovery_options_free);
  g_task_run_in_thread(task, discovery_worker);
  g_object_unref(task);
}

static void
refresh_clicked(GtkButton *button, gpointer user_data)
{
  (void)button;
  start_discovery(user_data);
}

static gboolean
auto_refresh(gpointer user_data)
{
  start_discovery(user_data);
  return G_SOURCE_CONTINUE;
}

static void
auto_changed(GtkComboBox *combo, gpointer user_data)
{
  App *app = user_data;
  if (app->auto_source) {
    g_source_remove(app->auto_source);
    app->auto_source = 0;
  }
  int selected = gtk_combo_box_get_active(combo);
  if (selected >= 0 && (guint)selected < G_N_ELEMENTS(auto_seconds) && auto_seconds[selected] > 0)
    app->auto_source = g_timeout_add_seconds(auto_seconds[selected], auto_refresh, app);
}

static GtkWidget *
editor_row(GtkGrid *grid, int row, const char *title, GtkWidget *value)
{
  GtkWidget *label = label_with_style(title, "dim-label");
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_grid_attach(grid, label, 0, row, 1, 1);
  gtk_widget_set_hexpand(value, TRUE);
  gtk_grid_attach(grid, value, 1, row, 1, 1);
  return value;
}

static GtkWidget *
build_editor(App *app)
{
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_size_request(box, 330, -1);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 16);
  gtk_widget_set_margin_end(box, 16);
  gtk_box_pack_start(GTK_BOX(box), label_with_style("Network parameters", "title-3"), FALSE, FALSE, 0);
  app->identity_label = label_with_style("Select a device", "heading");
  app->model_label = label_with_style("", NULL);
  app->firmware_label = label_with_style("", "dim-label");
  gtk_label_set_ellipsize(GTK_LABEL(app->firmware_label), PANGO_ELLIPSIZE_END);
  gtk_box_pack_start(GTK_BOX(box), app->identity_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), app->model_label, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), app->firmware_label, FALSE, FALSE, 0);

  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(separator, 6);
  gtk_widget_set_margin_bottom(separator, 6);
  gtk_box_pack_start(GTK_BOX(box), separator, FALSE, FALSE, 0);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
  app->ip_entry = editor_row(GTK_GRID(grid), 0, "IP address", gtk_entry_new());
  app->mask_entry = editor_row(GTK_GRID(grid), 1, "Subnet mask", gtk_entry_new());
  app->gateway_entry = editor_row(GTK_GRID(grid), 2, "Gateway", gtk_entry_new());
  GtkWidget *addressing_label = label_with_style("Addressing", NULL);
  app->dhcp_check = gtk_check_button_new_with_label("Enable DHCP");
  gtk_grid_attach(GTK_GRID(grid), addressing_label, 0, 3, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), app->dhcp_check, 1, 3, 1, 1);
  g_signal_connect(app->dhcp_check, "toggled", G_CALLBACK(dhcp_toggled), app);
  app->password_entry = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(app->password_entry), FALSE);
  gtk_entry_set_input_purpose(GTK_ENTRY(app->password_entry), GTK_INPUT_PURPOSE_PASSWORD);
  editor_row(GTK_GRID(grid), 4, "Admin password", app->password_entry);
  GtkWidget *hint = label_with_style(
    "Uses the authenticated NVR web API when reachable, otherwise TVT Layer 2, then verifies the same MAC.",
    "dim-label");
  gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);
  app->apply_button = gtk_button_new_with_label("Apply and verify");
  gtk_style_context_add_class(gtk_widget_get_style_context(app->apply_button), "suggested-action");
  gtk_widget_set_sensitive(app->apply_button, FALSE);
  g_signal_connect(app->apply_button, "clicked", G_CALLBACK(apply_clicked), app);
  gtk_box_pack_start(GTK_BOX(box), app->apply_button, FALSE, FALSE, 0);
  return box;
}

static void
add_text_column(App *app, const char *title, int model_column, int width)
{
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", model_column, NULL);
  gtk_tree_view_column_set_min_width(column, width);
  gtk_tree_view_column_set_resizable(column, TRUE);
  gtk_tree_view_append_column(GTK_TREE_VIEW(app->tree_view), column);
}

static GtkWidget *
build_combo(const char *const *labels)
{
  GtkWidget *combo = gtk_combo_box_text_new();
  for (guint i = 0; labels[i]; i++)
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), labels[i]);
  gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
  return combo;
}

static void
install_css(void)
{
  static const char css[] =
    ".app-toolbar { padding: 10px; }"
    ".status-bar { padding: 7px 12px; border-top: 1px solid alpha(@borders, .7); }"
    ".heading { font-weight: 700; }"
    ".dim-label { opacity: .72; }";
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(provider, css, -1, NULL);
  gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

static void
activate(GtkApplication *application, gpointer user_data)
{
  App *app = user_data;
  if (app->window) {
    gtk_window_present(app->window);
    return;
  }
  install_css();
  app->window = GTK_WINDOW(gtk_application_window_new(application));
  gtk_window_set_title(app->window, "TVT IPTool for Linux");
  gtk_window_set_default_size(app->window, 1240, 720);

  GtkWidget *header = gtk_header_bar_new();
  gtk_header_bar_set_title(GTK_HEADER_BAR(header), "TVT IPTool");
  gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "Linux device discovery and configuration");
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
  app->refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(app->refresh_button, "Search now");
  g_signal_connect(app->refresh_button, "clicked", G_CALLBACK(refresh_clicked), app);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->refresh_button);
  app->auto_combo = build_combo(auto_labels);
  g_signal_connect(app->auto_combo, "changed", G_CALLBACK(auto_changed), app);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->auto_combo);
  gtk_window_set_titlebar(app->window, header);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(app->window), root);
  GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_style_context_add_class(gtk_widget_get_style_context(toolbar), "app-toolbar");
  app->search_entry = gtk_search_entry_new();
  gtk_widget_set_hexpand(app->search_entry, TRUE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(app->search_entry), "Filter by IP, MAC, model, name, or firmware");
  g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(filter_changed), app);
  gtk_box_pack_start(GTK_BOX(toolbar), app->search_entry, TRUE, TRUE, 0);
  app->type_combo = build_combo(device_types);
  g_signal_connect(app->type_combo, "changed", G_CALLBACK(filter_changed), app);
  gtk_box_pack_start(GTK_BOX(toolbar), app->type_combo, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(toolbar), label_with_style("Port", "dim-label"), FALSE, FALSE, 0);
  app->port_start = gtk_spin_button_new_with_range(0, 65535, 1);
  gtk_widget_set_size_request(app->port_start, 90, -1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->port_start), 0);
  g_signal_connect(app->port_start, "value-changed", G_CALLBACK(filter_changed), app);
  gtk_box_pack_start(GTK_BOX(toolbar), app->port_start, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(toolbar), label_with_style("to", "dim-label"), FALSE, FALSE, 0);
  app->port_end = gtk_spin_button_new_with_range(0, 65535, 1);
  gtk_widget_set_size_request(app->port_end, 90, -1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->port_end), 65535);
  g_signal_connect(app->port_end, "value-changed", G_CALLBACK(filter_changed), app);
  gtk_box_pack_start(GTK_BOX(toolbar), app->port_end, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);

  app->store = gtk_list_store_new(N_COLUMNS,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
                                  TVT_TYPE_DEVICE);
  app->filtered = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(app->store), NULL));
  gtk_tree_model_filter_set_visible_func(app->filtered, filter_visible, app, NULL);
  app->tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app->filtered));
  gtk_widget_set_tooltip_text(app->tree_view, "Double-click a device to open its web interface");
  g_signal_connect(app->tree_view, "row-activated", G_CALLBACK(row_activated), app);
  gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(app->tree_view), GTK_TREE_VIEW_GRID_LINES_BOTH);
  add_text_column(app, "Type", COL_TYPE, 70);
  add_text_column(app, "IP address", COL_IP, 115);
  add_text_column(app, "MAC address", COL_MAC, 135);
  add_text_column(app, "Product model", COL_MODEL, 135);
  add_text_column(app, "Device name", COL_NAME, 130);
  add_text_column(app, "Software version", COL_FIRMWARE, 125);
  add_text_column(app, "Data", COL_DATA_PORT, 55);
  add_text_column(app, "HTTP", COL_HTTP_PORT, 55);
  app->selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->tree_view));
  gtk_tree_selection_set_mode(app->selection, GTK_SELECTION_SINGLE);
  g_signal_connect(app->selection, "changed", G_CALLBACK(selection_changed), app);

  GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll), app->tree_view);
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_pack1(GTK_PANED(paned), scroll, TRUE, FALSE);
  gtk_paned_pack2(GTK_PANED(paned), build_editor(app), FALSE, FALSE);
  gtk_paned_set_position(GTK_PANED(paned), 870);
  gtk_box_pack_start(GTK_BOX(root), paned, TRUE, TRUE, 0);

  app->status_label = label_with_style("Ready", "dim-label");
  gtk_style_context_add_class(gtk_widget_get_style_context(app->status_label), "status-bar");
  gtk_box_pack_start(GTK_BOX(root), app->status_label, FALSE, FALSE, 0);
  gtk_widget_show_all(GTK_WIDGET(app->window));
  start_discovery(app);
}

int
tvt_application_run(const TvtAppOptions *options, int argc, char **argv)
{
  App *app = g_new0(App, 1);
  app->bind_address = g_strdup(options->bind_address);
  app->timeout_ms = options->timeout_ms;
#if GLIB_CHECK_VERSION(2, 74, 0)
  app->application = gtk_application_new("io.github.dannielperez.tvt-iptool", G_APPLICATION_DEFAULT_FLAGS);
#else
  app->application = gtk_application_new("io.github.dannielperez.tvt-iptool", G_APPLICATION_FLAGS_NONE);
#endif
  g_object_set_data_full(G_OBJECT(app->application), "tvt-app-state", app, (GDestroyNotify)app_free);
  g_signal_connect(app->application, "activate", G_CALLBACK(activate), app);
  int status = g_application_run(G_APPLICATION(app->application), argc, argv);
  g_object_unref(app->application);
  return status;
}
