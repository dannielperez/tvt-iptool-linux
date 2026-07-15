#include "application.h"

#include "device.h"
#include "discovery.h"
#include "l2-provision.h"
#include "network.h"
#include "web-uri.h"

#include <string.h>

typedef struct {
  GtkApplication *application;
  GtkWindow *window;
  GListStore *store;
  GtkFilterListModel *filtered;
  GtkCustomFilter *filter;
  GtkSingleSelection *selection;
  GtkWidget *column_view;
  GtkWidget *search_entry;
  GtkWidget *type_dropdown;
  GtkWidget *port_start;
  GtkWidget *port_end;
  GtkWidget *auto_dropdown;
  GtkWidget *refresh_button;
  GtkWidget *status_label;
  GtkWidget *identity_label;
  GtkWidget *model_label;
  GtkWidget *firmware_label;
  GtkWidget *ip_entry;
  GtkWidget *mask_entry;
  GtkWidget *gateway_entry;
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
  char *bind_address;
  guint timeout_ms;
} ModifyJob;

typedef struct {
  gboolean verified;
  char *observed_ip;
} ModifyWorkResult;

typedef struct {
  App *app;
  ModifyJob *job;
} ConfirmContext;

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
    gtk_widget_add_css_class(label, style_class);
  return label;
}

static void
show_notice(App *app, const char *title, const char *message, gboolean destructive)
{
  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), title);
  gtk_window_set_transient_for(GTK_WINDOW(dialog), app->window);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_top(box, 22);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 22);
  gtk_widget_set_margin_end(box, 22);
  gtk_window_set_child(GTK_WINDOW(dialog), box);
  GtkWidget *heading = label_with_style(title, destructive ? "error" : "title-3");
  gtk_box_append(GTK_BOX(box), heading);
  GtkWidget *body = label_with_style(message, NULL);
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_label_set_max_width_chars(GTK_LABEL(body), 62);
  gtk_box_append(GTK_BOX(box), body);
  GtkWidget *close = gtk_button_new_with_label("Close");
  gtk_widget_set_halign(close, GTK_ALIGN_END);
  g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), dialog);
  gtk_box_append(GTK_BOX(box), close);
  gtk_window_present(GTK_WINDOW(dialog));
}

static gboolean
filter_device(gpointer item, gpointer user_data)
{
  App *app = user_data;
  const char *query = gtk_editable_get_text(GTK_EDITABLE(app->search_entry));
  guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->type_dropdown));
  const char *type = selected < G_N_ELEMENTS(device_types) - 1 ? device_types[selected] : device_types[0];
  TvtDevice *device = TVT_DEVICE(item);
  if (!tvt_device_matches(device, query, type))
    return FALSE;
  guint start = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->port_start));
  guint end = (guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app->port_end));
  guint data_port = tvt_device_get_data_port(device);
  guint http_port = tvt_device_get_http_port(device);
  return (data_port >= start && data_port <= end) || (http_port >= start && http_port <= end);
}

static void
filter_changed(GtkWidget *widget, gpointer user_data)
{
  App *app = user_data;
  (void)widget;
  gtk_filter_changed(GTK_FILTER(app->filter), GTK_FILTER_CHANGE_DIFFERENT);
}

static void
factory_setup(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
  GtkWidget *label = gtk_label_new(NULL);
  (void)factory;
  (void)user_data;
  gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
  gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
  gtk_widget_set_margin_start(label, 6);
  gtk_widget_set_margin_end(label, 6);
  gtk_list_item_set_child(list_item, label);
}

static void
factory_bind(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer user_data)
{
  TvtDevice *device = gtk_list_item_get_item(list_item);
  GtkWidget *label = gtk_list_item_get_child(list_item);
  guint column = GPOINTER_TO_UINT(user_data);
  (void)factory;
  gtk_label_set_text(GTK_LABEL(label), tvt_device_get_column(device, column));
  gtk_widget_set_tooltip_text(label, tvt_device_get_column(device, column));
}

static void
add_column(App *app, const char *title, guint field, int width)
{
  GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "setup", G_CALLBACK(factory_setup), GUINT_TO_POINTER(field));
  g_signal_connect(factory, "bind", G_CALLBACK(factory_bind), GUINT_TO_POINTER(field));
  GtkColumnViewColumn *column = gtk_column_view_column_new(title, factory);
  gtk_column_view_column_set_fixed_width(column, width);
  gtk_column_view_append_column(GTK_COLUMN_VIEW(app->column_view), column);
}

static void
set_entry(GtkWidget *entry, const char *text)
{
  gtk_editable_set_text(GTK_EDITABLE(entry), text ? text : "");
}

static void
selection_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
  App *app = user_data;
  TvtDevice *device = gtk_single_selection_get_selected_item(app->selection);
  (void)object;
  (void)pspec;
  if (!device) {
    gtk_label_set_text(GTK_LABEL(app->identity_label), "Select a device");
    gtk_label_set_text(GTK_LABEL(app->model_label), "");
    gtk_label_set_text(GTK_LABEL(app->firmware_label), "");
    set_entry(app->ip_entry, "");
    set_entry(app->mask_entry, "");
    set_entry(app->gateway_entry, "");
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
  set_entry(app->password_entry, "");
  gtk_widget_set_sensitive(app->apply_button, *tvt_device_get_mac(device) != '\0');
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
row_activated(GtkColumnView *column_view, guint position, gpointer user_data)
{
  App *app = user_data;
  (void)column_view;
  TvtDevice *device = g_list_model_get_item(G_LIST_MODEL(app->filtered), position);
  if (!device)
    return;
  open_device_web(app, device);
  g_object_unref(device);
}

static gboolean
has_duplicate_ip(App *app, const char *new_ip, const char *selected_mac)
{
  guint count = g_list_model_get_n_items(G_LIST_MODEL(app->store));
  for (guint i = 0; i < count; i++) {
    TvtDevice *device = g_list_model_get_item(G_LIST_MODEL(app->store), i);
    gboolean duplicate = g_str_equal(tvt_device_get_ip(device), new_ip) &&
                         g_ascii_strcasecmp(tvt_device_get_mac(device), selected_mac) != 0;
    g_object_unref(device);
    if (duplicate)
      return TRUE;
  }
  return FALSE;
}

static void
modify_worker(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  ModifyJob *job = task_data;
  (void)source_object;
  g_autoptr(GError) error = NULL;
  if (!tvt_l2_send_set_ip(job->bind_address, job->mac, job->password, job->new_ip,
                          job->subnet_mask, job->gateway, FALSE, &error)) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  ModifyWorkResult *result = g_new0(ModifyWorkResult, 1);
  for (guint attempt = 0; attempt < 12 && (!cancellable || !g_cancellable_is_cancelled(cancellable)); attempt++) {
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
          if (g_str_equal(result->observed_ip, job->new_ip)) {
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
      g_autofree char *message = g_strdup_printf(
        "The device is still advertising %s. The Layer-2 request was sent, but the camera/NVR "
        "did not apply the requested address. Check the administrator password and retry.",
        result->observed_ip);
      show_notice(app, "Network change not applied", message, TRUE);
    } else {
      gtk_label_set_text(GTK_LABEL(app->status_label), "Change sent; device not rediscovered");
      show_notice(app, "Verification required",
                  "The Layer-2 request was sent, but the device did not reappear during verification. "
                  "Refresh and locate the same MAC before changing NVR channel mappings.", TRUE);
    }
  } else {
    gtk_label_set_text(GTK_LABEL(app->status_label), "Network change verified");
    show_notice(app, "Network change verified",
                "The same device MAC reappeared at the requested IP address.", FALSE);
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
confirm_apply(GtkButton *button, gpointer user_data)
{
  GtkWindow *dialog = GTK_WINDOW(user_data);
  ConfirmContext *context = g_object_get_data(G_OBJECT(dialog), "confirm-context");
  (void)button;
  ModifyJob *job = context->job;
  context->job = NULL;
  start_modify(context->app, job);
  gtk_window_destroy(dialog);
}

static void
confirm_cancel(GtkButton *button, gpointer user_data)
{
  (void)button;
  gtk_window_destroy(GTK_WINDOW(user_data));
}

static void
confirm_context_free(ConfirmContext *context)
{
  if (!context)
    return;
  modify_job_free(context->job);
  g_free(context);
}

static void
show_confirmation(App *app, ModifyJob *job, const char *warning)
{
  GtkWidget *dialog = gtk_window_new();
  gtk_window_set_title(GTK_WINDOW(dialog), "Confirm network change");
  gtk_window_set_transient_for(GTK_WINDOW(dialog), app->window);
  gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
  ConfirmContext *context = g_new0(ConfirmContext, 1);
  context->app = app;
  context->job = job;
  g_object_set_data_full(G_OBJECT(dialog), "confirm-context", context, (GDestroyNotify)confirm_context_free);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
  gtk_widget_set_margin_top(box, 22);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 22);
  gtk_widget_set_margin_end(box, 22);
  gtk_window_set_child(GTK_WINDOW(dialog), box);
  gtk_box_append(GTK_BOX(box), label_with_style("Confirm network change", "title-3"));
  g_autofree char *summary = g_strdup_printf(
    "Device: %s\nCurrent IP: %s\nNew IP: %s\nSubnet mask: %s\nGateway: %s%s%s",
    job->mac, job->old_ip, job->new_ip, job->subnet_mask, job->gateway,
    warning ? "\n\nWarning: " : "", warning ? warning : "");
  GtkWidget *body = label_with_style(summary, NULL);
  gtk_label_set_wrap(GTK_LABEL(body), TRUE);
  gtk_label_set_selectable(GTK_LABEL(body), TRUE);
  gtk_box_append(GTK_BOX(box), body);
  GtkWidget *buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(buttons, GTK_ALIGN_END);
  GtkWidget *cancel = gtk_button_new_with_label("Cancel");
  GtkWidget *apply = gtk_button_new_with_label("Apply and verify");
  gtk_widget_add_css_class(apply, "suggested-action");
  g_signal_connect(cancel, "clicked", G_CALLBACK(confirm_cancel), dialog);
  g_signal_connect(apply, "clicked", G_CALLBACK(confirm_apply), dialog);
  gtk_box_append(GTK_BOX(buttons), cancel);
  gtk_box_append(GTK_BOX(buttons), apply);
  gtk_box_append(GTK_BOX(box), buttons);
  gtk_window_present(GTK_WINDOW(dialog));
}

static void
apply_clicked(GtkButton *button, gpointer user_data)
{
  App *app = user_data;
  TvtDevice *device = gtk_single_selection_get_selected_item(app->selection);
  (void)button;
  if (!device)
    return;
  const char *new_ip = gtk_editable_get_text(GTK_EDITABLE(app->ip_entry));
  const char *mask = gtk_editable_get_text(GTK_EDITABLE(app->mask_entry));
  const char *gateway = gtk_editable_get_text(GTK_EDITABLE(app->gateway_entry));
  g_autofree char *warning = NULL;
  g_autoptr(GError) error = NULL;
  if (!tvt_network_validate(new_ip, mask, gateway, &warning, &error)) {
    show_notice(app, "Invalid network settings", error->message, TRUE);
    return;
  }
  if (has_duplicate_ip(app, new_ip, tvt_device_get_mac(device))) {
    show_notice(app, "IP address already discovered",
                "Another discovered device already uses this IP address. Resolve that conflict before applying changes.",
                TRUE);
    return;
  }
  ModifyJob *job = g_new0(ModifyJob, 1);
  job->mac = g_strdup(tvt_device_get_mac(device));
  job->old_ip = g_strdup(tvt_device_get_ip(device));
  job->new_ip = g_strdup(new_ip);
  job->subnet_mask = g_strdup(mask);
  job->gateway = g_strdup(gateway);
  job->password = g_strdup(gtk_editable_get_text(GTK_EDITABLE(app->password_entry)));
  set_entry(app->password_entry, "");
  job->bind_address = g_strdup(app->bind_address);
  job->timeout_ms = app->timeout_ms;
  show_confirmation(app, job, warning);
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

  TvtDevice *selected = gtk_single_selection_get_selected_item(app->selection);
  g_autofree char *selected_mac = selected ? g_strdup(tvt_device_get_mac(selected)) : NULL;
  g_list_store_remove_all(app->store);
  for (guint i = 0; i < devices->len; i++)
    g_list_store_append(app->store, g_ptr_array_index(devices, i));
  g_ptr_array_unref(devices);
  gtk_filter_changed(GTK_FILTER(app->filter), GTK_FILTER_CHANGE_DIFFERENT);

  guint filtered_count = g_list_model_get_n_items(G_LIST_MODEL(app->filtered));
  if (selected_mac && *selected_mac) {
    for (guint i = 0; i < filtered_count; i++) {
      TvtDevice *candidate = g_list_model_get_item(G_LIST_MODEL(app->filtered), i);
      gboolean match = g_ascii_strcasecmp(tvt_device_get_mac(candidate), selected_mac) == 0;
      g_object_unref(candidate);
      if (match) {
        gtk_single_selection_set_selected(app->selection, i);
        break;
      }
    }
  }
  g_autofree char *status = g_strdup_printf("%u device%s discovered", filtered_count,
                                            filtered_count == 1 ? "" : "s");
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
auto_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
  App *app = user_data;
  (void)object;
  (void)pspec;
  if (app->auto_source) {
    g_source_remove(app->auto_source);
    app->auto_source = 0;
  }
  guint selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(app->auto_dropdown));
  if (selected < G_N_ELEMENTS(auto_seconds) && auto_seconds[selected] > 0)
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
  gtk_box_append(GTK_BOX(box), label_with_style("Network parameters", "title-3"));
  app->identity_label = label_with_style("Select a device", "heading");
  app->model_label = label_with_style("", NULL);
  app->firmware_label = label_with_style("", "dim-label");
  gtk_label_set_ellipsize(GTK_LABEL(app->firmware_label), PANGO_ELLIPSIZE_END);
  gtk_box_append(GTK_BOX(box), app->identity_label);
  gtk_box_append(GTK_BOX(box), app->model_label);
  gtk_box_append(GTK_BOX(box), app->firmware_label);

  GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_widget_set_margin_top(separator, 6);
  gtk_widget_set_margin_bottom(separator, 6);
  gtk_box_append(GTK_BOX(box), separator);
  GtkWidget *grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
  gtk_box_append(GTK_BOX(box), grid);
  app->ip_entry = editor_row(GTK_GRID(grid), 0, "IP address", gtk_entry_new());
  app->mask_entry = editor_row(GTK_GRID(grid), 1, "Subnet mask", gtk_entry_new());
  app->gateway_entry = editor_row(GTK_GRID(grid), 2, "Gateway", gtk_entry_new());
  app->password_entry = gtk_password_entry_new();
  gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(app->password_entry), TRUE);
  editor_row(GTK_GRID(grid), 3, "Admin password", app->password_entry);
  GtkWidget *hint = label_with_style(
    "Uses TVT Layer-2 provisioning, then verifies the same MAC at the requested IP.",
    "dim-label");
  gtk_box_append(GTK_BOX(box), hint);
  app->apply_button = gtk_button_new_with_label("Apply and verify");
  gtk_widget_add_css_class(app->apply_button, "suggested-action");
  gtk_widget_set_sensitive(app->apply_button, FALSE);
  g_signal_connect(app->apply_button, "clicked", G_CALLBACK(apply_clicked), app);
  gtk_box_append(GTK_BOX(box), app->apply_button);
  return box;
}

static void
install_css(void)
{
  static const char css[] =
    ".app-toolbar { padding: 10px; }"
    ".status-bar { padding: 7px 12px; border-top: 1px solid alpha(@borders, .7); }"
    ".heading { font-weight: 700; }"
    ".dim-label { opacity: .72; }"
    "columnview { border-top: 1px solid alpha(@borders, .7); }";
  GtkCssProvider *provider = gtk_css_provider_new();
#if GTK_CHECK_VERSION(4, 12, 0)
  gtk_css_provider_load_from_string(provider, css);
#else
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  gtk_css_provider_load_from_data(provider, css, -1);
  G_GNUC_END_IGNORE_DEPRECATIONS
#endif
  gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
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
  GtkWidget *title = gtk_label_new("TVT IPTool");
  gtk_widget_add_css_class(title, "title");
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), title);
  app->refresh_button = gtk_button_new_from_icon_name("view-refresh-symbolic");
  gtk_widget_set_tooltip_text(app->refresh_button, "Search now");
  g_signal_connect(app->refresh_button, "clicked", G_CALLBACK(refresh_clicked), app);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(header), app->refresh_button);
  app->auto_dropdown = gtk_drop_down_new_from_strings(auto_labels);
  g_signal_connect(app->auto_dropdown, "notify::selected", G_CALLBACK(auto_changed), app);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(header), app->auto_dropdown);
  gtk_window_set_titlebar(app->window, header);

  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_window_set_child(app->window, root);
  GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_add_css_class(toolbar, "app-toolbar");
  app->search_entry = gtk_search_entry_new();
  gtk_widget_set_hexpand(app->search_entry, TRUE);
  gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(app->search_entry),
                                        "Filter by IP, MAC, model, name, or firmware");
  g_signal_connect(app->search_entry, "search-changed", G_CALLBACK(filter_changed), app);
  gtk_box_append(GTK_BOX(toolbar), app->search_entry);
  app->type_dropdown = gtk_drop_down_new_from_strings(device_types);
  g_signal_connect(app->type_dropdown, "notify::selected", G_CALLBACK(filter_changed), app);
  gtk_box_append(GTK_BOX(toolbar), app->type_dropdown);
  gtk_box_append(GTK_BOX(toolbar), label_with_style("Port", "dim-label"));
  app->port_start = gtk_spin_button_new_with_range(0, 65535, 1);
  gtk_widget_set_tooltip_text(app->port_start, "Start port");
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->port_start), 0);
  g_signal_connect(app->port_start, "value-changed", G_CALLBACK(filter_changed), app);
  gtk_box_append(GTK_BOX(toolbar), app->port_start);
  gtk_box_append(GTK_BOX(toolbar), label_with_style("to", "dim-label"));
  app->port_end = gtk_spin_button_new_with_range(0, 65535, 1);
  gtk_widget_set_tooltip_text(app->port_end, "End port");
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->port_end), 65535);
  g_signal_connect(app->port_end, "value-changed", G_CALLBACK(filter_changed), app);
  gtk_box_append(GTK_BOX(toolbar), app->port_end);
  gtk_box_append(GTK_BOX(root), toolbar);

  app->store = g_list_store_new(TVT_TYPE_DEVICE);
  app->filter = gtk_custom_filter_new(filter_device, app, NULL);
  app->filtered = gtk_filter_list_model_new(G_LIST_MODEL(app->store), GTK_FILTER(app->filter));
  app->selection = gtk_single_selection_new(G_LIST_MODEL(app->filtered));
  gtk_single_selection_set_autoselect(app->selection, FALSE);
  g_signal_connect(app->selection, "notify::selected-item", G_CALLBACK(selection_changed), app);
  app->column_view = gtk_column_view_new(GTK_SELECTION_MODEL(app->selection));
  gtk_widget_set_tooltip_text(app->column_view, "Double-click a device to open its web interface");
  g_signal_connect(app->column_view, "activate", G_CALLBACK(row_activated), app);
  gtk_column_view_set_show_column_separators(GTK_COLUMN_VIEW(app->column_view), TRUE);
  gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(app->column_view), TRUE);
  add_column(app, "Type", 0, 80);
  add_column(app, "IP address", 1, 130);
  add_column(app, "MAC address", 2, 150);
  add_column(app, "Product model", 3, 155);
  add_column(app, "Device name", 4, 150);
  add_column(app, "Software version", 5, 145);
  add_column(app, "Data", 6, 65);
  add_column(app, "HTTP", 7, 65);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), app->column_view);
  gtk_widget_set_hexpand(scroll, TRUE);
  gtk_widget_set_vexpand(scroll, TRUE);
  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_set_start_child(GTK_PANED(paned), scroll);
  gtk_paned_set_end_child(GTK_PANED(paned), build_editor(app));
  gtk_paned_set_position(GTK_PANED(paned), 870);
  gtk_paned_set_resize_end_child(GTK_PANED(paned), FALSE);
  gtk_box_append(GTK_BOX(root), paned);

  app->status_label = label_with_style("Ready", "dim-label");
  gtk_widget_add_css_class(app->status_label, "status-bar");
  gtk_box_append(GTK_BOX(root), app->status_label);
  gtk_window_present(app->window);
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
