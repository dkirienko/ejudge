/* -*- mode: c; c-basic-offset: 4 -*- */

/* Copyright (C) 2022 Alexander Chernov <cher@ejudge.ru> */

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

#include "ejudge/config.h"
#include "ejudge/common_plugin.h"
#include "ejudge/userprob_plugin.h"
#include "../common-mysql/common_mysql.h"
#include "ejudge/xalloc.h"
#include "ejudge/errlog.h"

struct userprob_mysql_data
{
    struct userprob_plugin_data b;

    int nref;

    // mysql access
    struct common_mysql_iface *mi;
    struct common_mysql_state *md;

    int is_db_checked;
};

extern struct userprob_plugin_iface plugin_userprob_mysql;

static struct common_plugin_data *
init_func(void)
{
    struct userprob_mysql_data *state = NULL;
    XCALLOC(state, 1);
    state->b.vt = &plugin_userprob_mysql;
    return &state->b.b;
}

static int
finish_func(struct common_plugin_data *data)
{
    return 0;
}

static int
prepare_func(
        struct common_plugin_data *data,
        const struct ejudge_cfg *config,
        struct xml_tree *tree)
{
    struct userprob_mysql_data *smd = (struct userprob_mysql_data *) data;
    const struct common_loaded_plugin *mplg;

    // load common_mysql plugin
    if (!(mplg = plugin_load_external(0, "common", "mysql", config))) {
        err("cannot load common_mysql plugin");
        return -1;
    }
    smd->mi = (struct common_mysql_iface*) mplg->iface;
    smd->md = (struct common_mysql_state*) mplg->data;

    return 0;
}

static const char create_query[] =
"CREATE TABLE `%suserprobs` (\n"
"    serial_id BIGINT NOT NULL PRIMARY KEY AUTO_INCREMENT,\n"
"    contest_id INT NOT NULL,\n"
"    user_id INT UNSIGNED NOT NULL,\n"
"    prob_id INT UNSIGNED NOT NULL DEFAULT 0,\n"
"    hook_id CHAR(64) NOT NULL,\n"
"    secret VARCHAR(64) DEFAULT NULL,\n"
"    vcs_type VARCHAR(16) DEFAULT NULL,\n"
"    vcs_url VARCHAR(1024) DEFAULT NULL,\n"
"    vcs_subdir VARCHAR(1024) DEFAULT NULL,\n"
"    ssh_private_key VARCHAR(1024) DEFAULT NULL,\n"
"    last_event VARCHAR(128) DEFAULT NULL,\n"
"    last_revision VARCHAR(128) DEFAULT NULL,\n"
"    create_time DATETIME(6) NOT NULL,\n"
"    last_change_time DATETIME(6) DEFAULT NULL,\n"
"    UNIQUE KEY up_unique_k(contest_id,user_id,prob_id),\n"
"    UNIQUE KEY up_hook_id_k(hook_id),\n"
"    KEY up_contest_id_idx(contest_id),\n"
"    KEY up_user_id_idx(user_id),\n"
"    KEY up_cu_ids_idx(contest_id,user_id),\n"
"    FOREIGN KEY up_user_id_fk(user_id) REFERENCES %slogins(user_id)\n"
") ENGINE=InnoDB DEFAULT CHARSET=utf8 COLLATE=utf8_bin;\n";

static int
create_database(
        struct userprob_mysql_data *umd)
{
    struct common_mysql_iface *mi = umd->mi;
    struct common_mysql_state *md = umd->md;

    if (mi->simple_fquery(md, create_query,
                          md->table_prefix, md->table_prefix) < 0)
        db_error_fail(md);

    if (mi->simple_fquery(md, "INSERT INTO %sconfig VALUES ('userprob_version', '%d') ;", md->table_prefix, 1) < 0)
        db_error_fail(md);

    umd->is_db_checked = 1;
    return 0;

fail:
    return -1;
}

static int
check_database(
        struct userprob_mysql_data *smd)
{
    int userprob_version = 0;
    struct common_mysql_iface *mi = smd->mi;
    struct common_mysql_state *md = smd->md;

    if (mi->connect(md) < 0)
        goto fail;

    if (mi->fquery(md, 1, "SELECT config_val FROM %sconfig WHERE config_key = 'userprob_version' ;", md->table_prefix) < 0) {
        err("probably the database is not created, please, create it");
        goto fail;
    }
    if (md->row_count > 1) {
        err("userprob_version key is not unique");
        goto fail;
    }
    if (!md->row_count) return create_database(smd);
    if (mi->next_row(md) < 0) db_error_fail(md);
    if (!md->row[0] || mi->parse_int(md, md->row[0], &userprob_version) < 0)
        db_error_inv_value_fail(md, "config_val");
    mi->free_res(md);
    if (userprob_version < 1) {
        err("userprob_version == %d is not supported", userprob_version);
        goto fail;
    }

    while (userprob_version >= 0) {
        switch (userprob_version) {
        default:
            userprob_version = -1;
            break;
        }
        if (userprob_version >= 0) {
            ++userprob_version;
            if (mi->simple_fquery(md, "UPDATE %sconfig SET config_val = '%d' WHERE config_key = 'userprob_version' ;", md->table_prefix, userprob_version) < 0)
                return -1;
        }
    }

    smd->is_db_checked = 1;
    return 0;

fail:
    return -1;
}

struct userprob_entry_internal
{
    int64_t serial_id;
    int contest_id;
    int user_id;
    int prob_id;
    unsigned char *hook_id;
    unsigned char *secret;
    unsigned char *vcs_type;
    unsigned char *vcs_url;
    unsigned char *vcs_subdir;
    unsigned char *ssh_private_key;
    unsigned char *last_event;
    unsigned char *last_revision;
    struct timeval create_time;
    struct timeval last_change_time;
};

enum { USERPROB_ENTRY_ROW_WIDTH = 14 };
#define USERPROB_ENTRY_OFFSET(f) XOFFSET(struct userprob_entry_internal, f)
[[gnu::unused]]
static const struct common_mysql_parse_spec userprob_entry_spec[USERPROB_ENTRY_ROW_WIDTH] =
{
    { 0, 'l', "serial_id", USERPROB_ENTRY_OFFSET(serial_id), 0 },
    { 0, 'd', "contest_id", USERPROB_ENTRY_OFFSET(contest_id), 0 },
    { 0, 'd', "user_id", USERPROB_ENTRY_OFFSET(user_id), 0 },
    { 0, 'd', "prob_id", USERPROB_ENTRY_OFFSET(prob_id), 0 },
    { 1, 's', "hook_id", USERPROB_ENTRY_OFFSET(hook_id), 0 },
    { 1, 's', "secret", USERPROB_ENTRY_OFFSET(secret), 0 },
    { 1, 's', "vcs_type", USERPROB_ENTRY_OFFSET(vcs_type), 0 },
    { 1, 's', "vcs_url", USERPROB_ENTRY_OFFSET(vcs_url), 0 },
    { 1, 's', "vcs_subdir", USERPROB_ENTRY_OFFSET(vcs_url), 0 },
    { 1, 's', "ssh_private_key", USERPROB_ENTRY_OFFSET(vcs_url), 0 },
    { 1, 's', "last_event", USERPROB_ENTRY_OFFSET(vcs_url), 0 },
    { 1, 's', "last_revision", USERPROB_ENTRY_OFFSET(vcs_url), 0 },
    { 1, 'T', "create_time", USERPROB_ENTRY_OFFSET(create_time), 0 },
    { 1, 'T', "last_change_time", USERPROB_ENTRY_OFFSET(last_change_time), 0 },
};

static int
open_func(
        struct userprob_plugin_data *data)
{
    struct userprob_mysql_data *umd = (struct userprob_mysql_data *) data;

    if (!umd->is_db_checked) {
        check_database(umd);
    }

    return 0;
}

struct userprob_entry *
fetch_by_hook_id_func(
        struct userprob_plugin_data *data,
        const unsigned char *hook_id)
{
    struct userprob_mysql_data *umd = (struct userprob_mysql_data *) data;
    struct common_mysql_iface *mi = umd->mi;
    struct common_mysql_state *md = umd->md;
    char *cmd_s = NULL;
    size_t cmd_z = 0;
    FILE *cmd_f = NULL;
    struct userprob_entry_internal uei = {};
    struct userprob_entry *ue = NULL;

    cmd_f = open_memstream(&cmd_s, &cmd_z);
    fprintf(cmd_f, "SELECT * FROM `%suserprobs` WHERE hook_id = '",
            md->table_prefix);
    mi->escape_string(md, cmd_f, hook_id);
    fprintf(cmd_f, "';");
    fclose(cmd_f); cmd_f = NULL;

    if (mi->query(md, cmd_s, cmd_z, USERPROB_ENTRY_ROW_WIDTH) < 0)
        db_error_fail(md);
    free(cmd_s); cmd_s = NULL; cmd_z = 0;

    if (md->row_count == 1) {
        XCALLOC(ue, 1);
        if (mi->next_row(md) < 0) db_error_fail(md);
        if (mi->parse_spec(md, -1, md->row, md->lengths, USERPROB_ENTRY_ROW_WIDTH, userprob_entry_spec, &uei) < 0) goto fail;

        ue->serial_id = uei.serial_id;
        ue->contest_id = uei.contest_id;
        ue->user_id = uei.user_id;
        ue->prob_id = uei.prob_id;
        ue->hook_id = uei.hook_id; uei.hook_id = NULL;
        ue->secret = uei.secret; uei.secret = NULL;
        ue->vcs_type = uei.vcs_type; uei.vcs_type = NULL;
        ue->vcs_url = uei.vcs_url; uei.vcs_url = NULL;
        ue->vcs_subdir = uei.vcs_subdir; uei.vcs_subdir = NULL;
        ue->ssh_private_key = uei.ssh_private_key; uei.ssh_private_key = NULL;
        ue->last_event = uei.last_event; uei.last_event = NULL;
        ue->last_revision = uei.last_revision; uei.last_revision = NULL;
        ue->create_time_us = uei.create_time.tv_sec * 1000000LL + uei.create_time.tv_usec;
        ue->last_change_time_us = uei.last_change_time.tv_sec * 1000000LL + uei.last_change_time.tv_usec;
    }
    return ue;

fail:
    if (cmd_f) fclose(cmd_f);
    free(cmd_s);
    return NULL;
}

struct userprob_plugin_iface plugin_userprob_mysql =
{
    {
        {
            sizeof (struct userprob_plugin_iface),
            EJUDGE_PLUGIN_IFACE_VERSION,
            "userprob",
            "mysql",
        },
        COMMON_PLUGIN_IFACE_VERSION,
        init_func,
        finish_func,
        prepare_func,
    },
    USERPROB_PLUGIN_IFACE_VERSION,
    open_func,
    fetch_by_hook_id_func,
};
