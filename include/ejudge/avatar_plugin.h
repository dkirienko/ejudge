/* -*- c -*- */

#ifndef __AVATAR_PLUGIN_H__
#define __AVATAR_PLUGIN_H__

/* Copyright (C) 2017 Alexander Chernov <cher@ejudge.ru> */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "ejudge/ejudge_plugin.h"
#include "ejudge/common_plugin.h"

/* version of the plugin interface structure */
#define AVATAR_PLUGIN_IFACE_VERSION 1

struct avatar_plugin_iface
{
    struct common_plugin_iface b;
    int xuser_version;
};

#endif /* __AVATAR_PLUGIN_H__ */

/*
 * Local variables:
 *  c-basic-offset: 4
 * End:
 */
