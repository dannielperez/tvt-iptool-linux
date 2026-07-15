#pragma once

#include <gio/gio.h>

#include "device.h"

G_BEGIN_DECLS

char *tvt_device_web_uri(TvtDevice *device, GError **error);

G_END_DECLS
