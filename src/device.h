#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define TVT_TYPE_DEVICE (tvt_device_get_type())
G_DECLARE_FINAL_TYPE(TvtDevice, tvt_device, TVT, DEVICE, GObject)

TvtDevice *tvt_device_new(void);

const char *tvt_device_get_ip(TvtDevice *self);
const char *tvt_device_get_mac(TvtDevice *self);
const char *tvt_device_get_name(TvtDevice *self);
const char *tvt_device_get_device_type(TvtDevice *self);
const char *tvt_device_get_model(TvtDevice *self);
const char *tvt_device_get_series(TvtDevice *self);
const char *tvt_device_get_firmware(TvtDevice *self);
const char *tvt_device_get_build_date(TvtDevice *self);
const char *tvt_device_get_kernel(TvtDevice *self);
const char *tvt_device_get_subnet_mask(TvtDevice *self);
const char *tvt_device_get_gateway(TvtDevice *self);
const char *tvt_device_get_dns1(TvtDevice *self);
const char *tvt_device_get_dns2(TvtDevice *self);
guint tvt_device_get_data_port(TvtDevice *self);
guint tvt_device_get_http_port(TvtDevice *self);
guint tvt_device_get_protocol_version(TvtDevice *self);
gboolean tvt_device_get_dhcp(TvtDevice *self);

const char *tvt_device_get_column(TvtDevice *self, guint column);
gboolean tvt_device_matches(TvtDevice *self, const char *query, const char *device_type);

G_END_DECLS
