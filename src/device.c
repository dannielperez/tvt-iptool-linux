#include "device.h"

struct _TvtDevice {
  GObject parent_instance;
  char *ip;
  char *mac;
  char *name;
  char *device_type;
  char *model;
  char *series;
  char *firmware;
  char *build_date;
  char *kernel;
  char *subnet_mask;
  char *gateway;
  char *dns1;
  char *dns2;
  guint data_port;
  guint http_port;
  gboolean dhcp;
};

G_DEFINE_TYPE(TvtDevice, tvt_device, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_IP,
  PROP_MAC,
  PROP_NAME,
  PROP_DEVICE_TYPE,
  PROP_MODEL,
  PROP_SERIES,
  PROP_FIRMWARE,
  PROP_BUILD_DATE,
  PROP_KERNEL,
  PROP_SUBNET_MASK,
  PROP_GATEWAY,
  PROP_DNS1,
  PROP_DNS2,
  PROP_DATA_PORT,
  PROP_HTTP_PORT,
  PROP_DHCP,
  N_PROPERTIES,
};

static GParamSpec *properties[N_PROPERTIES];

static void
set_string(char **target, const GValue *value)
{
  g_free(*target);
  *target = g_value_dup_string(value);
}

static void
tvt_device_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
  TvtDevice *self = TVT_DEVICE(object);

  switch (property_id) {
    case PROP_IP: set_string(&self->ip, value); break;
    case PROP_MAC: set_string(&self->mac, value); break;
    case PROP_NAME: set_string(&self->name, value); break;
    case PROP_DEVICE_TYPE: set_string(&self->device_type, value); break;
    case PROP_MODEL: set_string(&self->model, value); break;
    case PROP_SERIES: set_string(&self->series, value); break;
    case PROP_FIRMWARE: set_string(&self->firmware, value); break;
    case PROP_BUILD_DATE: set_string(&self->build_date, value); break;
    case PROP_KERNEL: set_string(&self->kernel, value); break;
    case PROP_SUBNET_MASK: set_string(&self->subnet_mask, value); break;
    case PROP_GATEWAY: set_string(&self->gateway, value); break;
    case PROP_DNS1: set_string(&self->dns1, value); break;
    case PROP_DNS2: set_string(&self->dns2, value); break;
    case PROP_DATA_PORT: self->data_port = g_value_get_uint(value); break;
    case PROP_HTTP_PORT: self->http_port = g_value_get_uint(value); break;
    case PROP_DHCP: self->dhcp = g_value_get_boolean(value); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
  }
}

static void
tvt_device_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
  TvtDevice *self = TVT_DEVICE(object);

  switch (property_id) {
    case PROP_IP: g_value_set_string(value, self->ip); break;
    case PROP_MAC: g_value_set_string(value, self->mac); break;
    case PROP_NAME: g_value_set_string(value, self->name); break;
    case PROP_DEVICE_TYPE: g_value_set_string(value, self->device_type); break;
    case PROP_MODEL: g_value_set_string(value, self->model); break;
    case PROP_SERIES: g_value_set_string(value, self->series); break;
    case PROP_FIRMWARE: g_value_set_string(value, self->firmware); break;
    case PROP_BUILD_DATE: g_value_set_string(value, self->build_date); break;
    case PROP_KERNEL: g_value_set_string(value, self->kernel); break;
    case PROP_SUBNET_MASK: g_value_set_string(value, self->subnet_mask); break;
    case PROP_GATEWAY: g_value_set_string(value, self->gateway); break;
    case PROP_DNS1: g_value_set_string(value, self->dns1); break;
    case PROP_DNS2: g_value_set_string(value, self->dns2); break;
    case PROP_DATA_PORT: g_value_set_uint(value, self->data_port); break;
    case PROP_HTTP_PORT: g_value_set_uint(value, self->http_port); break;
    case PROP_DHCP: g_value_set_boolean(value, self->dhcp); break;
    default: G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
  }
}

static void
tvt_device_finalize(GObject *object)
{
  TvtDevice *self = TVT_DEVICE(object);
  g_free(self->ip);
  g_free(self->mac);
  g_free(self->name);
  g_free(self->device_type);
  g_free(self->model);
  g_free(self->series);
  g_free(self->firmware);
  g_free(self->build_date);
  g_free(self->kernel);
  g_free(self->subnet_mask);
  g_free(self->gateway);
  g_free(self->dns1);
  g_free(self->dns2);
  G_OBJECT_CLASS(tvt_device_parent_class)->finalize(object);
}

static GParamSpec *
string_property(const char *name)
{
  return g_param_spec_string(name, name, name, "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
}

static void
tvt_device_class_init(TvtDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->set_property = tvt_device_set_property;
  object_class->get_property = tvt_device_get_property;
  object_class->finalize = tvt_device_finalize;

  properties[PROP_IP] = string_property("ip");
  properties[PROP_MAC] = string_property("mac");
  properties[PROP_NAME] = string_property("name");
  properties[PROP_DEVICE_TYPE] = string_property("device-type");
  properties[PROP_MODEL] = string_property("model");
  properties[PROP_SERIES] = string_property("series");
  properties[PROP_FIRMWARE] = string_property("firmware");
  properties[PROP_BUILD_DATE] = string_property("build-date");
  properties[PROP_KERNEL] = string_property("kernel");
  properties[PROP_SUBNET_MASK] = string_property("subnet-mask");
  properties[PROP_GATEWAY] = string_property("gateway");
  properties[PROP_DNS1] = string_property("dns1");
  properties[PROP_DNS2] = string_property("dns2");
  properties[PROP_DATA_PORT] = g_param_spec_uint("data-port", "data-port", "data-port", 0, 65535, 0,
                                                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_HTTP_PORT] = g_param_spec_uint("http-port", "http-port", "http-port", 0, 65535, 0,
                                                G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  properties[PROP_DHCP] = g_param_spec_boolean("dhcp", "dhcp", "dhcp", FALSE,
                                              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties(object_class, N_PROPERTIES, properties);
}

static void
tvt_device_init(TvtDevice *self)
{
  self->ip = g_strdup("");
  self->mac = g_strdup("");
  self->name = g_strdup("");
  self->device_type = g_strdup("Unknown");
  self->model = g_strdup("");
  self->series = g_strdup("");
  self->firmware = g_strdup("");
  self->build_date = g_strdup("");
  self->kernel = g_strdup("");
  self->subnet_mask = g_strdup("");
  self->gateway = g_strdup("");
  self->dns1 = g_strdup("");
  self->dns2 = g_strdup("");
}

TvtDevice *tvt_device_new(void) { return g_object_new(TVT_TYPE_DEVICE, NULL); }

#define STRING_GETTER(name, field) \
  const char *tvt_device_get_##name(TvtDevice *self) { return self->field ? self->field : ""; }

STRING_GETTER(ip, ip)
STRING_GETTER(mac, mac)
STRING_GETTER(name, name)
STRING_GETTER(device_type, device_type)
STRING_GETTER(model, model)
STRING_GETTER(series, series)
STRING_GETTER(firmware, firmware)
STRING_GETTER(build_date, build_date)
STRING_GETTER(kernel, kernel)
STRING_GETTER(subnet_mask, subnet_mask)
STRING_GETTER(gateway, gateway)
STRING_GETTER(dns1, dns1)
STRING_GETTER(dns2, dns2)

guint tvt_device_get_data_port(TvtDevice *self) { return self->data_port; }
guint tvt_device_get_http_port(TvtDevice *self) { return self->http_port; }
gboolean tvt_device_get_dhcp(TvtDevice *self) { return self->dhcp; }

const char *
tvt_device_get_column(TvtDevice *self, guint column)
{
  static char port_buffers[2][12];
  switch (column) {
    case 0: return tvt_device_get_device_type(self);
    case 1: return tvt_device_get_ip(self);
    case 2: return tvt_device_get_mac(self);
    case 3: return tvt_device_get_model(self);
    case 4: return tvt_device_get_name(self);
    case 5: return tvt_device_get_firmware(self);
    case 6:
      g_snprintf(port_buffers[0], sizeof(port_buffers[0]), "%u", self->data_port);
      return self->data_port ? port_buffers[0] : "";
    case 7:
      g_snprintf(port_buffers[1], sizeof(port_buffers[1]), "%u", self->http_port);
      return self->http_port ? port_buffers[1] : "";
    default: return "";
  }
}

static gboolean
contains_folded(const char *haystack, const char *needle_folded)
{
  g_autofree char *folded = g_utf8_casefold(haystack ? haystack : "", -1);
  return g_strstr_len(folded, -1, needle_folded) != NULL;
}

gboolean
tvt_device_matches(TvtDevice *self, const char *query, const char *device_type)
{
  if (device_type && *device_type && g_strcmp0(device_type, "All devices") != 0 &&
      g_ascii_strcasecmp(device_type, self->device_type) != 0)
    return FALSE;

  if (!query || !*query)
    return TRUE;

  g_autofree char *needle = g_utf8_casefold(query, -1);
  return contains_folded(self->ip, needle) || contains_folded(self->mac, needle) ||
         contains_folded(self->name, needle) || contains_folded(self->model, needle) ||
         contains_folded(self->series, needle) || contains_folded(self->firmware, needle) ||
         contains_folded(self->device_type, needle);
}
