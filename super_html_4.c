/* -*- mode: c -*- */

/* Copyright (C) 2008-2015 Alexander Chernov <cher@ejudge.ru> */

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
#include "ejudge/version.h"
#include "ejudge/ej_limits.h"
#include "ejudge/super_html.h"
#include "ejudge/super-serve.h"
#include "ejudge/meta/super-serve_meta.h"
#include "ejudge/super_proto.h"
#include "ejudge/copyright.h"
#include "ejudge/misctext.h"
#include "ejudge/contests.h"
#include "ejudge/meta/contests_meta.h"
#include "ejudge/l10n.h"
#include "ejudge/charsets.h"
#include "ejudge/fileutl.h"
#include "ejudge/xml_utils.h"
#include "ejudge/userlist.h"
#include "ejudge/ejudge_cfg.h"
#include "ejudge/mischtml.h"
#include "ejudge/prepare.h"
#include "ejudge/meta/prepare_meta.h"
#include "ejudge/meta_generic.h"
#include "ejudge/prepare_dflt.h"
#include "ejudge/cpu.h"
#include "ejudge/compat.h"
#include "ejudge/errlog.h"
#include "ejudge/external_action.h"

#include "ejudge/xalloc.h"
#include "ejudge/logger.h"

#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <dlfcn.h>

#if !defined CONF_STYLE_PREFIX
#define CONF_STYLE_PREFIX "/ejudge/"
#endif

#define ARMOR(s)  html_armor_buf(&ab, s)
#define URLARMOR(s)  url_armor_buf(&ab, s)
#define FAIL(c) do { retval = -(c); goto cleanup; } while (0)

const unsigned char *
veprintf(unsigned char *buf, size_t size, const char *format, va_list args)
{
  vsnprintf(buf, size, format, args);
  return buf;
}

const unsigned char *
eprintf(unsigned char *buf, size_t size, const char *format, ...)
  __attribute__((format(printf,3,4)));
const unsigned char *
eprintf(unsigned char *buf, size_t size, const char *format, ...)
{
  va_list args;
  va_start(args, format);
  vsnprintf(buf, size, format, args);
  va_end(args);
  return buf;
}

// size must be < 2GiB
int
parse_size(const unsigned char *valstr, size_t *p_size)
{
  unsigned long long val;
  char *eptr = 0;

  if (!valstr || !*valstr) return -1;

  errno = 0;
  val = strtoull(valstr, &eptr, 10);
  if (errno || valstr == (const unsigned char*) eptr) return -1;

  if (*eptr == 'G' || *eptr == 'g') {
    if (val >= 2) return -1;
    val *= 1 * 1024 * 1024 * 1024;
    eptr++;
  } else if (*eptr == 'M' || *eptr == 'm') {
    if (val >= 2 * 1024) return -1;
    val *= 1 * 1024 * 1024;
    eptr++;
  } else if (*eptr == 'K' || *eptr == 'k') {
    if (val >= 2 * 1024 * 1024) return -1;
    val *= 1 * 1024;
    eptr++;
  }
  if (*eptr) return -1;
  *p_size = (size_t) val;
  return 0;
}

static const char fancy_priv_header[] =
"Content-Type: %s; charset=%s\n"
"Cache-Control: no-cache\n"
"Pragma: no-cache\n\n"
"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\n"
"<html><head>\n<meta http-equiv=\"Content-type\" content=\"text/html; charset=%s\"/>\n"
"<link rel=\"stylesheet\" href=\"%spriv.css\" type=\"text/css\">\n"
  //"<link rel=\"shortcut icon\" type=\"image/x-icon\" href=\"/favicon.ico\">\n"
"<title>%s</title></head>\n"
"<body>\n";

static void
write_html_header(
        FILE *out_f,
        struct http_request_info *phr,
        const unsigned char *title,
        int use_dojo,
        const unsigned char *body_class)
{
  unsigned char cl[64];

  if (use_dojo && !body_class) body_class = "nihilo";

  fprintf(out_f, fancy_priv_header,
          "text/html", EJUDGE_CHARSET, EJUDGE_CHARSET, CONF_STYLE_PREFIX,
          title);

  if (use_dojo) {
    fprintf(out_f, "<link href=\"%sdijit/themes/%s/%s.css\" rel=\"stylesheet\" type=\"text/css\" />\n", CONF_STYLE_PREFIX, body_class, body_class);
    fprintf(out_f, "<link href=\"%sdojo/resources/dojo.css\" rel=\"stylesheet\" type=\"text/css\" />\n", CONF_STYLE_PREFIX);
    fprintf(out_f,
            "<style type=\"text/css\">\n"
            "  @import \"%sdojox/highlight/resources/highlight.css\";\n"
            "  @import \"%sdojox/highlight/resources/pygments/default.css\";\n"
            "  @import \"%sdojo/resources/dojo.css\";\n"
            "  @import \"%sdojox/grid/_grid/Grid.css\";\n"
            "  @import \"%sdojox/grid/_grid/nihiloGrid.css\";\n"
            "</style>\n",
            CONF_STYLE_PREFIX, CONF_STYLE_PREFIX, CONF_STYLE_PREFIX,
            CONF_STYLE_PREFIX, CONF_STYLE_PREFIX);
    fprintf(out_f, "<style type=\"text/css\" id=\"generatedStyles\"></style>\n");
  }

  if (use_dojo) {
    fprintf(out_f, "<script type=\"text/javascript\" src=\"%sdojo/dojo.js\" djConfig=\"isDebug: false, parseOnLoad: true, dojoIframeHistoryUrl:'%sdojo/resources/iframe_history.html'\"></script>\n",
            CONF_STYLE_PREFIX, CONF_STYLE_PREFIX);
  }

  if (use_dojo) {
    fprintf(out_f, "<script type=\"text/javascript\" src=\"" CONF_STYLE_PREFIX "priv.js\"></script>\n");

    fprintf(out_f,
            "<script type=\"text/javascript\">\n"
            "  dojo.require(\"dojo.parser\");\n"
            "  dojo.require(\"dijit.InlineEditBox\");\n"
            "  dojo.require(\"dijit.form.Button\");\n"
            "  dojo.require(\"dijit.form.DateTextBox\");\n"
            "  dojo.require(\"dijit.form.Textarea\");\n");
    fprintf(out_f,
            "  var SSERV_CMD_HTTP_REQUEST=%d;\n"
            "  var SID=\"%016llx\";\n"
            "  var self_url=\"%s\";\n"
            "  var script_name=\"%s\";\n",
            SSERV_CMD_HTTP_REQUEST,
            phr->session_id,
            phr->self_url,
            phr->script_name);
    fprintf(out_f, "</script>\n");
  }

  fprintf(out_f, "</head>");

  cl[0] = 0;
  if (body_class) snprintf(cl, sizeof(cl), " class=\"%s\"", body_class);
  fprintf(out_f, "<body%s>", cl);
}

void
ss_write_html_header(
        FILE *out_f,
        struct http_request_info *phr,
        const unsigned char *title,
        int use_dojo,
        const unsigned char *body_class)
{
  write_html_header(out_f, phr, title, use_dojo, body_class);
}

static const char fancy_priv_footer[] =
"<hr/>%s</body></html>\n";
static void
write_html_footer(FILE *out_f)
{
  fprintf(out_f, fancy_priv_footer, get_copyright(0));
}

void
ss_write_html_footer(FILE *out_f)
{
  write_html_footer(out_f);
}

static void
write_json_header(FILE *out_f)
{
  fprintf(out_f, "Content-type: text/plain; charset=%s\n\n",
          EJUDGE_CHARSET);
}

static void
refresh_page(
        FILE *out_f,
        struct http_request_info *phr,
        const char *format,
        ...)
  __attribute__((format(printf, 3, 4)));
static void
refresh_page(
        FILE *out_f,
        struct http_request_info *phr,
        const char *format,
        ...)
{
  va_list args;
  char buf[1024];
  char url[1024];

  buf[0] = 0;
  if (format && *format) {
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
  }

  if (!buf[0] && !phr->session_id) {
    snprintf(url, sizeof(url), "%s", phr->self_url);
  } else if (!buf[0]) {
    snprintf(url, sizeof(url), "%s?SID=%016llx", phr->self_url,
             phr->session_id);
  } else if (!phr->session_id) {
    snprintf(url, sizeof(url), "%s?%s", phr->self_url, buf);
  } else {
    snprintf(url, sizeof(url), "%s?SID=%016llx&%s", phr->self_url,
             phr->session_id, buf);
  }

  fprintf(out_f, "Location: %s\n", url);
  if (phr->client_key) {
    fprintf(out_f, "Set-Cookie: EJSID=%016llx; Path=/\n", phr->client_key);
  }
  putc('\n', out_f);
}

typedef int (*handler_func_t)(FILE *log_f, FILE *out_f, struct http_request_info *phr);

static int
cmd_cnts_details(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  int retval = 0;
  struct sid_state *ss = phr->ss;

  if (ss->edited_cnts) FAIL(SSERV_ERR_CONTEST_EDITED);

 cleanup:
  return retval;
}

static int
cmd_edited_cnts_back(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  refresh_page(out_f, phr, NULL);
  return 0;
}

static int
cmd_edited_cnts_continue(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  int new_edit = -1;

  if (hr_cgi_param_int(phr, "new_edit", &new_edit) >= 0 && new_edit == 1) {
    refresh_page(out_f, phr, "action=%d&op=%d", SSERV_CMD_HTTP_REQUEST,
                 0);
  } else {
    refresh_page(out_f, phr, "action=%d", SSERV_CMD_CNTS_EDIT_CUR_CONTEST_PAGE);
  }
  return 0;
}

static int
cmd_edited_cnts_start_new(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  int contest_id = 0;
  int new_edit = -1;

  hr_cgi_param_int_opt(phr, "new_edit", &new_edit, 0);
  if (hr_cgi_param_int_opt(phr, "contest_id", &contest_id, 0) < 0
      || contest_id < 0) contest_id = 0;
  super_serve_clear_edited_contest(phr->ss);
  if (new_edit == 1) {
    if (!contest_id) {
      refresh_page(out_f, phr, "action=%d&op=%d",
                   SSERV_CMD_HTTP_REQUEST, SSERV_CMD_CREATE_NEW_CONTEST_PAGE);
    } else {
      refresh_page(out_f, phr, "action=%d&op=%d&contest_id=%d",
                   SSERV_CMD_HTTP_REQUEST, 0,
                   contest_id);
    }
  } else {
    if (!contest_id) {
      refresh_page(out_f, phr, "action=%d", SSERV_CMD_CREATE_CONTEST_PAGE);
    } else {
      refresh_page(out_f, phr, "action=%d&contest_id=%d",
                   SSERV_CMD_CNTS_START_EDIT_ACTION, contest_id);
    }
  }

  return 0;
}

// forget the contest editing from the other session and return
// to the main page
// all errors are silently ignored
static int
cmd_locked_cnts_forget(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  struct sid_state *ss;
  int contest_id = -1;

  if (phr->ss->edited_cnts)
    goto done;
  if (hr_cgi_param_int(phr, "contest_id", &contest_id) < 0 || contest_id <= 0)
    goto done;
  if (!(ss = super_serve_sid_state_get_cnts_editor_nc(contest_id)))
    goto done;
  if (ss->user_id != phr->user_id)
    goto done;
  super_serve_clear_edited_contest(ss);

 done:;
  refresh_page(out_f, phr, NULL);
  return 0;
}

// move the editing information to this session and continue editing
// all errors are silently ignored
static int
cmd_locked_cnts_continue(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  struct sid_state *ss;
  int contest_id = 0;
  int new_edit = -1;

  if (phr->ss->edited_cnts)
    goto top_level;
  if (hr_cgi_param_int(phr, "contest_id", &contest_id) < 0 || contest_id <= 0)
    goto top_level;
  if (!(ss = super_serve_sid_state_get_cnts_editor_nc(contest_id)))
    goto top_level;
  if (ss->user_id != phr->user_id)
    goto top_level;

  super_serve_move_edited_contest(phr->ss, ss);

  if (hr_cgi_param_int(phr, "new_edit", &new_edit) >= 0 && new_edit == 1) {
    refresh_page(out_f, phr, "action=%d&op=%d", SSERV_CMD_HTTP_REQUEST,
                 0);
  } else {
    refresh_page(out_f, phr, "action=%d", SSERV_CMD_CNTS_EDIT_CUR_CONTEST_PAGE);
  }
  return 0;

 top_level:;
  refresh_page(out_f, phr, NULL);
  return 0;
}

static const unsigned char head_row_attr[] =
  " bgcolor=\"#dddddd\"";
static const unsigned char * const form_row_attrs[]=
{
  " bgcolor=\"#e4e4e4\"",
  " bgcolor=\"#eeeeee\"",
};

static int
cmd_op_create_new_contest_page(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  int retval = 0;
  unsigned char buf[1024];
  int contest_num = 0, recomm_id = 1, j, cnts_id;
  const int *contests = 0;
  struct html_armor_buffer ab = HTML_ARMOR_INITIALIZER;
  const struct contest_desc *cnts = 0;

  if (phr->ss->edited_cnts) {
    snprintf(buf, sizeof(buf), "serve-control: %s, another contest is edited",
             phr->html_name);
    write_html_header(out_f, phr, buf, 1, 0);
    fprintf(out_f, "<h1>%s</h1>\n", buf);

    snprintf(buf, sizeof(buf),
             "<input type=\"hidden\" name=\"SID\" value=\"%016llx\" />",
             phr->session_id);
    super_html_edited_cnts_dialog(out_f,
                                  phr->priv_level, phr->user_id, phr->login,
                                  phr->session_id, &phr->ip, phr->config,
                                  phr->ss, phr->self_url, buf,
                                  "", NULL, 1);

    write_html_footer(out_f);
    return 0;
  }

  if (phr->priv_level != PRIV_LEVEL_ADMIN)
    FAIL(SSERV_ERR_PERM_DENIED);
  if (ejudge_cfg_opcaps_find(phr->config, phr->login, &phr->caps) < 0)
    FAIL(SSERV_ERR_PERM_DENIED);
  if (opcaps_check(phr->caps, OPCAP_EDIT_CONTEST) < 0)
    FAIL(SSERV_ERR_PERM_DENIED);

  contest_num = contests_get_list(&contests);
  if (contest_num > 0) recomm_id = contests[contest_num - 1] + 1;
  j = super_serve_sid_state_get_max_edited_cnts();
  if (j >= recomm_id) recomm_id = j + 1;

  snprintf(buf, sizeof(buf), "serve-control: %s, create a new contest",
           phr->html_name);
  write_html_header(out_f, phr, buf, 1, 0);
  fprintf(out_f, "<h1>%s</h1>\n", buf);

  html_start_form(out_f, 1, phr->self_url, "");
  html_hidden(out_f, "SID", "%016llx", phr->session_id);
  html_hidden(out_f, "action", "%d", SSERV_CMD_HTTP_REQUEST);
  html_hidden(out_f, "op", "%d", SSERV_CMD_CREATE_NEW_CONTEST);

  fprintf(out_f, "<table border=\"0\">");
  fprintf(out_f, "<tr><td>Contest number:</td><td>%s</td></tr>\n",
          html_input_text(buf, sizeof(buf), "contest_id", 20, 0, "%d", recomm_id));
  fprintf(out_f, "<tr><td>Contest template:</td><td>");
  fprintf(out_f, "<select name=\"templ_id\">"
          "<option value=\"0\">From scratch</option>");
  for (j = 0; j < contest_num; j++) {
    cnts_id = contests[j];
    if (contests_get(cnts_id, &cnts) < 0) continue;
    fprintf(out_f, "<option value=\"%d\">%d - %s</option>", cnts_id, cnts_id,
            ARMOR(cnts->name));
  }
  fprintf(out_f, "</select></td></tr>\n");
  fprintf(out_f, "<tr><td>&nbsp;</td><td>");
  fprintf(out_f, "<input type=\"submit\" value=\"%s\"/>", "Create contest!");
  fprintf(out_f, "</td></tr>\n");
  fprintf(out_f, "</table></form>\n");
  write_html_footer(out_f);

 cleanup:
  html_armor_free(&ab);
  return retval;
}

static int
cmd_op_create_new_contest(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  int retval = 0;
  int contest_id = -1;
  int templ_id = -1;
  int contest_num, i;
  const int *contests = 0;
  const struct contest_desc *templ_cnts = 0;

  if (phr->ss->edited_cnts)
    FAIL(SSERV_ERR_CONTEST_EDITED);
  if (phr->priv_level != PRIV_LEVEL_ADMIN)
    FAIL(SSERV_ERR_PERM_DENIED);
  if (ejudge_cfg_opcaps_find(phr->config, phr->login, &phr->caps) < 0)
    FAIL(SSERV_ERR_PERM_DENIED);
  if (opcaps_check(phr->caps, OPCAP_EDIT_CONTEST) < 0)
    FAIL(SSERV_ERR_PERM_DENIED);
  if (hr_cgi_param_int(phr, "contest_id", &contest_id) < 0
      || contest_id < 0 || contest_id > EJ_MAX_CONTEST_ID)
    FAIL(SSERV_ERR_INV_VALUE);
  if (hr_cgi_param_int(phr, "templ_id", &templ_id) < 0 || templ_id < 0)
    FAIL(SSERV_ERR_INV_VALUE);

  contest_num = contests_get_list(&contests);
  if (contest_num < 0 || !contests)
    FAIL(SSERV_ERR_INTERNAL);

  if (!contest_id) {
    contest_id = contests[contest_num - 1] + 1;
    i = super_serve_sid_state_get_max_edited_cnts();
    if (i >= contest_id) contest_id = i + 1;
  }
  for (i = 0; i < contest_num && contests[i] != contest_id; i++);
  if (i < contest_num)
    FAIL(SSERV_ERR_CONTEST_ALREADY_EXISTS);
  if (super_serve_sid_state_get_cnts_editor(contest_id))
    FAIL(SSERV_ERR_CONTEST_ALREADY_EDITED);
  if (templ_id > 0) {
    for (i = 0; i < contest_num && contests[i] != templ_id; i++);
    if (i >= contest_num)
      FAIL(SSERV_ERR_INV_CONTEST);
    if (contests_get(templ_id, &templ_cnts) < 0 || !templ_cnts)
      FAIL(SSERV_ERR_INV_CONTEST);
  }

  if (!templ_cnts) {
    phr->ss->edited_cnts = contest_tmpl_new(contest_id, phr->login, phr->self_url, phr->system_login, &phr->ip, phr->ssl_flag, phr->config);
    phr->ss->global = prepare_new_global_section(contest_id, phr->ss->edited_cnts->root_dir, phr->config);
  } else {
    super_html_load_serve_cfg(templ_cnts, phr->config, phr->ss);
    super_html_fix_serve(phr->ss, templ_id, contest_id);
    phr->ss->edited_cnts = contest_tmpl_clone(phr->ss, contest_id, templ_id, phr->login, phr->system_login);
  }

  refresh_page(out_f, phr, "action=%d&op=%d", SSERV_CMD_HTTP_REQUEST,
               0);

 cleanup:
  return retval;
}

static int
cmd_op_forget_contest(
        FILE *log_f,
        FILE *out_f,
        struct http_request_info *phr)
{
  phr->json_reply = 1;
  super_serve_clear_edited_contest(phr->ss);
  return 1;
}

/* TODO list:
  { NS_GLOBAL, CNTSGLOB_unhandled_vars, 137, 0, 0, 0, 0, 0, 0, 0, "Global.unhandled_vars SidState.show_global_7 &&" },
 */

static const int global_int_min_val[CNTSGLOB_LAST_FIELD] =
{
  [CNTSGLOB_max_run_size] = 1,
  [CNTSGLOB_max_run_total] = 1,
  [CNTSGLOB_max_run_num] = 1,
  [CNTSGLOB_max_clar_size] = 1,
  [CNTSGLOB_max_clar_total] = 1,
  [CNTSGLOB_max_clar_num] = 1,
  [CNTSGLOB_team_page_quota] = 1,
  [CNTSGLOB_users_on_page] = 0,
  [CNTSGLOB_plog_update_time] = 0,
  [CNTSGLOB_external_xml_update_time] = 0,
  [CNTSGLOB_internal_xml_update_time] = 0,
  [CNTSGLOB_sleep_time] = 1,
  [CNTSGLOB_serve_sleep_time] = 1,
  [CNTSGLOB_max_file_length] = 1,
  [CNTSGLOB_max_line_length] = 1,
  [CNTSGLOB_inactivity_timeout] = 1,
  [CNTSGLOB_cr_serialization_key] = 1,
  [CNTSGLOB_cpu_bogomips] = 0
};

static const int global_int_max_val[CNTSGLOB_LAST_FIELD] =
{
  [CNTSGLOB_max_run_size] = 1*1024*1024*1024,
  [CNTSGLOB_max_run_total] = 1*1024*1024*1024,
  [CNTSGLOB_max_run_num] = 1*1024*1024*1024,
  [CNTSGLOB_max_clar_size] = 1*1024*1024*1024,
  [CNTSGLOB_max_clar_total] = 1*1024*1024*1024,
  [CNTSGLOB_max_clar_num] = 1*1024*1024*1024,
  [CNTSGLOB_team_page_quota] = 1*1024*1024*1024,
  [CNTSGLOB_users_on_page] = 1*1024*1024*1024,
  [CNTSGLOB_plog_update_time] = 1*1024*1024*1024,
  [CNTSGLOB_external_xml_update_time] = 1*1024*1024*1024,
  [CNTSGLOB_internal_xml_update_time] = 1*1024*1024*1024,
  [CNTSGLOB_sleep_time] = 1*1024*1024*1024,
  [CNTSGLOB_serve_sleep_time] = 1*1024*1024*1024,
  [CNTSGLOB_max_file_length] = 1*1024*1024*1024,
  [CNTSGLOB_max_line_length] = 1*1024*1024*1024,
  [CNTSGLOB_inactivity_timeout] = 1*1024*1024*1024,
  [CNTSGLOB_cr_serialization_key] = 1*1024*1024*1024,
  [CNTSGLOB_cpu_bogomips] = 1*1024*1024*1024
};

static const int global_int_default_val[CNTSGLOB_LAST_FIELD] =
{
  [CNTSGLOB_max_run_size] = DFLT_G_MAX_RUN_SIZE,
  [CNTSGLOB_max_run_total] = DFLT_G_MAX_RUN_TOTAL,
  [CNTSGLOB_max_run_num] = DFLT_G_MAX_RUN_NUM,
  [CNTSGLOB_max_clar_size] = DFLT_G_MAX_CLAR_SIZE,
  [CNTSGLOB_max_clar_total] = DFLT_G_MAX_CLAR_TOTAL,
  [CNTSGLOB_max_clar_num] = DFLT_G_MAX_CLAR_NUM,
  [CNTSGLOB_team_page_quota] = DFLT_G_TEAM_PAGE_QUOTA,
  [CNTSGLOB_sleep_time] = DFLT_G_SLEEP_TIME,
  [CNTSGLOB_serve_sleep_time] = DFLT_G_SERVE_SLEEP_TIME,
  [CNTSGLOB_max_file_length] = DFLT_G_MAX_FILE_LENGTH,
  [CNTSGLOB_max_line_length] = DFLT_G_MAX_LINE_LENGTH,
  [CNTSGLOB_inactivity_timeout] = DFLT_G_INACTIVITY_TIMEOUT,
};

static const unsigned char editable_sid_state_fields[SSSS_LAST_FIELD] =
{
  [SSSS_enable_stand2] = 1,
  [SSSS_enable_plog] = 1,
  [SSSS_enable_extra_col] = 1,
  [SSSS_enable_win32_languages] = 1,
};

static const unsigned char editable_sid_state_fields_neg[SSSS_LAST_FIELD] =
{
  [SSSS_disable_compilation_server] = 1,
};

static const unsigned char lang_editable_fields[CNTSLANG_LAST_FIELD] =
{
  [CNTSLANG_long_name] = 1,
  [CNTSLANG_disabled] = 1,
  [CNTSLANG_insecure] = 1,
  [CNTSLANG_disable_testing] = 1,
  [CNTSLANG_disable_auto_testing] = 1,
  [CNTSLANG_binary] = 1,
  [CNTSLANG_style_checker_cmd] = 1,
  [CNTSLANG_style_checker_env] = 1,
  [CNTSLANG_compiler_env] = 1,
  [CNTSLANG_unhandled_vars] = 1,
};

static const unsigned char lang_editable_details[CNTSLANG_LAST_FIELD] =
{
  [CNTSLANG_compiler_env] = 1,
  [CNTSLANG_style_checker_env] = 1,
  [CNTSLANG_unhandled_vars] = 1,
};

static const unsigned char prob_reloadable_set[CNTSPROB_LAST_FIELD] =
{
  [CNTSPROB_scoring_checker] = 0,
  [CNTSPROB_interactive_valuer] = 0,
  [CNTSPROB_disable_pe] = 0,
  [CNTSPROB_disable_wtl] = 0,
  [CNTSPROB_manual_checking] = 1,  
  [CNTSPROB_examinator_num] = 0,
  [CNTSPROB_check_presentation] = 1,
  [CNTSPROB_use_stdin] = 0,
  [CNTSPROB_use_stdout] = 0,
  [CNTSPROB_combined_stdin] = 0,
  [CNTSPROB_combined_stdout] = 0,
  [CNTSPROB_binary_input] = 0,
  [CNTSPROB_binary] = 0,
  [CNTSPROB_ignore_exit_code] = 0,
  [CNTSPROB_olympiad_mode] = 1,
  [CNTSPROB_score_latest] = 0,
  [CNTSPROB_score_latest_or_unmarked] = 0,
  [CNTSPROB_score_latest_marked] = 0,
  [CNTSPROB_time_limit] = 1,
  [CNTSPROB_time_limit_millis] = 1,
  [CNTSPROB_real_time_limit] = 1,
  [CNTSPROB_use_ac_not_ok] = 0,
  [CNTSPROB_ignore_prev_ac] = 0,
  [CNTSPROB_team_enable_rep_view] = 1,
  [CNTSPROB_team_enable_ce_view] = 1,
  [CNTSPROB_team_show_judge_report] = 1,
  [CNTSPROB_show_checker_comment] = 0,
  [CNTSPROB_ignore_compile_errors] = 0,
  [CNTSPROB_full_score] = 0,
  [CNTSPROB_test_score] = 0,
  [CNTSPROB_run_penalty] = 0,
  [CNTSPROB_acm_run_penalty] = 0,
  [CNTSPROB_disqualified_penalty] = 0,
  [CNTSPROB_ignore_penalty] = 0,
  [CNTSPROB_use_corr] = 1,
  [CNTSPROB_use_info] = 1,
  [CNTSPROB_use_tgz] = 1,
  [CNTSPROB_tests_to_accept] = 0,
  [CNTSPROB_accept_partial] = 0,
  [CNTSPROB_min_tests_to_accept] = 0,
  [CNTSPROB_checker_real_time_limit] = 0,
  [CNTSPROB_interactor_time_limit] = 0,
  [CNTSPROB_disable_auto_testing] = 1,
  [CNTSPROB_disable_testing] = 1,
  [CNTSPROB_disable_user_submit] = 1,
  [CNTSPROB_disable_tab] = 1,
  [CNTSPROB_unrestricted_statement] = 1,
  [CNTSPROB_hide_file_names] = 1,
  [CNTSPROB_disable_submit_after_ok] = 1,
  [CNTSPROB_disable_security] = 1,
  [CNTSPROB_enable_compilation] = 1,
  [CNTSPROB_skip_testing] = 1,
  [CNTSPROB_variable_full_score] = 1,
  [CNTSPROB_hidden] = 1,
  [CNTSPROB_priority_adjustment] = 0,
  [CNTSPROB_spelling] = 0,
  [CNTSPROB_stand_hide_time] = 0,
  [CNTSPROB_advance_to_next] = 0,
  [CNTSPROB_disable_ctrl_chars] = 0,
  [CNTSPROB_valuer_sets_marked] = 0,
  [CNTSPROB_ignore_unmarked] = 0,
  [CNTSPROB_disable_stderr] = 0,
  [CNTSPROB_enable_process_group] = 0,
  [CNTSPROB_enable_text_form] = 0,
  [CNTSPROB_stand_ignore_score] = 0,
  [CNTSPROB_stand_last_column] = 0,
  [CNTSPROB_score_multiplier] = 0,
  [CNTSPROB_prev_runs_to_show] = 0,
  [CNTSPROB_max_vm_size] = 0,
  [CNTSPROB_max_stack_size] = 0,
  [CNTSPROB_max_data_size] = 0,
  [CNTSPROB_max_core_size] = 0,
  [CNTSPROB_max_file_size] = 0,
  [CNTSPROB_max_open_file_count] = 0,
  [CNTSPROB_max_process_count] = 0,
  [CNTSPROB_super] = 1,
  [CNTSPROB_short_name] = 1,
  [CNTSPROB_long_name] = 1,
  [CNTSPROB_group_name] = 0,
  [CNTSPROB_stand_name] = 0,
  [CNTSPROB_stand_column] = 0,
  [CNTSPROB_internal_name] = 1,
  [CNTSPROB_test_dir] = 1,
  [CNTSPROB_test_sfx] = 1,
  [CNTSPROB_corr_dir] = 1,
  [CNTSPROB_corr_sfx] = 1,
  [CNTSPROB_info_dir] = 1,
  [CNTSPROB_info_sfx] = 1,
  [CNTSPROB_tgz_dir] = 1,
  [CNTSPROB_tgz_sfx] = 1,
  [CNTSPROB_tgzdir_sfx] = 1,
  [CNTSPROB_input_file] = 0,
  [CNTSPROB_output_file] = 0,
  [CNTSPROB_test_score_list] = 0,
  [CNTSPROB_score_tests] = 0,
  [CNTSPROB_test_sets] = 0,
  [CNTSPROB_deadline] = 0,
  [CNTSPROB_start_date] = 0,
  [CNTSPROB_variant_num] = 1,
  [CNTSPROB_date_penalty] = 0,
  [CNTSPROB_group_start_date] = 0,
  [CNTSPROB_group_deadline] = 0,
  [CNTSPROB_disable_language] = 0,
  [CNTSPROB_enable_language] = 0,
  [CNTSPROB_require] = 0,
  [CNTSPROB_provide_ok] = 0,
  [CNTSPROB_standard_checker] = 1,
  [CNTSPROB_lang_compiler_env] = 0,
  [CNTSPROB_checker_env] = 0,
  [CNTSPROB_valuer_env] = 0,
  [CNTSPROB_interactor_env] = 0,
  [CNTSPROB_style_checker_env] = 0,
  [CNTSPROB_test_checker_env] = 0,
  [CNTSPROB_init_env] = 0,
  [CNTSPROB_start_env] = 0,
  [CNTSPROB_lang_time_adj] = 0,
  [CNTSPROB_lang_time_adj_millis] = 0,
  [CNTSPROB_lang_max_vm_size] = 0,
  [CNTSPROB_lang_max_stack_size] = 0,
  [CNTSPROB_check_cmd] = 0,
  [CNTSPROB_valuer_cmd] = 0,
  [CNTSPROB_interactor_cmd] = 0,
  [CNTSPROB_style_checker_cmd] = 0,
  [CNTSPROB_test_checker_cmd] = 0,
  [CNTSPROB_init_cmd] = 0,
  [CNTSPROB_solution_src] = 0,
  [CNTSPROB_solution_cmd] = 0,
  [CNTSPROB_test_pat] = 1,
  [CNTSPROB_corr_pat] = 1,
  [CNTSPROB_info_pat] = 1,
  [CNTSPROB_tgz_pat] = 1,
  [CNTSPROB_tgzdir_pat] = 1,
  [CNTSPROB_personal_deadline] = 0,
  [CNTSPROB_score_bonus] = 0,
  [CNTSPROB_open_tests] = 0,
  [CNTSPROB_final_open_tests] = 0,
  [CNTSPROB_statement_file] = 1,
  [CNTSPROB_alternatives_file] = 0,
  [CNTSPROB_plugin_file] = 1,
  [CNTSPROB_xml_file] = 1,
  [CNTSPROB_type] = 1,
  [CNTSPROB_alternative] = 0,
  [CNTSPROB_stand_attr] = 0,
  [CNTSPROB_source_header] = 0,
  [CNTSPROB_source_footer] = 0,
  [CNTSPROB_score_view] = 0,
};

const unsigned char prob_editable_details[CNTSPROB_LAST_FIELD] =
{
  [CNTSPROB_lang_compiler_env] = 1,
  [CNTSPROB_checker_env] = 1,
  [CNTSPROB_valuer_env] = 1,
  [CNTSPROB_interactor_env] = 1,
  [CNTSPROB_style_checker_env] = 1,
  [CNTSPROB_test_checker_env] = 1,
  [CNTSPROB_init_env] = 1,
  [CNTSPROB_start_env] = 1,
  [CNTSPROB_score_view] = 1,
  [CNTSPROB_lang_time_adj] = 1,
  [CNTSPROB_lang_time_adj_millis] = 1,
  [CNTSPROB_lang_max_vm_size] = 1,
  [CNTSPROB_lang_max_stack_size] = 1,
  [CNTSPROB_disable_language] = 1,
  [CNTSPROB_enable_language] = 1,
  [CNTSPROB_require] = 1,
  [CNTSPROB_provide_ok] = 1,
  [CNTSPROB_unhandled_vars] = 1,
};

static handler_func_t op_handlers[SSERV_CMD_LAST] =
{
  [SSERV_CMD_VIEW_CNTS_DETAILS] = cmd_cnts_details,
  [SSERV_CMD_EDITED_CNTS_BACK] = cmd_edited_cnts_back,
  [SSERV_CMD_EDITED_CNTS_CONTINUE] = cmd_edited_cnts_continue,
  [SSERV_CMD_EDITED_CNTS_START_NEW] = cmd_edited_cnts_start_new,
  [SSERV_CMD_LOCKED_CNTS_FORGET] = cmd_locked_cnts_forget,
  [SSERV_CMD_LOCKED_CNTS_CONTINUE] = cmd_locked_cnts_continue,
  [SSERV_CMD_CREATE_NEW_CONTEST_PAGE] = cmd_op_create_new_contest_page,
  [SSERV_CMD_CREATE_NEW_CONTEST] = cmd_op_create_new_contest,
  [SSERV_CMD_FORGET_CONTEST] = cmd_op_forget_contest,

  [SSERV_CMD_BROWSE_PROBLEM_PACKAGES] = super_serve_op_browse_problem_packages,
  [SSERV_CMD_CREATE_PACKAGE] = super_serve_op_package_operation,
  [SSERV_CMD_CREATE_PROBLEM] = super_serve_op_edit_problem,
  [SSERV_CMD_DELETE_ITEM] = super_serve_op_package_operation,
  [SSERV_CMD_EDIT_PROBLEM] = super_serve_op_edit_problem,

  /* Note: operations SSERV_CMD_USER_*, SSERV_CMD_GROUP_* are loaded using dlsym */
};

extern void super_html_6_force_link(void);
void *super_html_6_force_link_ptr = super_html_6_force_link;
extern void super_html_7_force_link(void);
void *super_html_7_force_link_ptr = super_html_7_force_link;

static int
parse_opcode(struct http_request_info *phr, int *p_opcode)
{
  const unsigned char *s = NULL;
  if (hr_cgi_param(phr, "op", &s) <= 0 || !s || !*s) {
    *p_opcode = 0;
    return 0;
  }
  const unsigned char *q;
  for (q = s; isdigit(*q); ++q) {}
  if (!*q) {
    char *eptr = NULL;
    errno = 0;
    long val = strtol(s, &eptr, 10);
    if (errno || *eptr) return SSERV_ERR_INV_OPER;
    if (val < 0 || val >= SSERV_CMD_LAST) return SSERV_ERR_INV_OPER;
    *p_opcode = val;
    return 0;
  }

  for (int i = 1; i < SSERV_CMD_LAST; ++i) {
    if (!strcasecmp(super_proto_cmd_names[i], s)) {
      *p_opcode = i;
      return 0;
    }
  }
  *p_opcode = 0;
  return 0;
}

static int
parse_action(struct http_request_info *phr)
{
  int action = 0;
  int n = 0, r = 0;
  const unsigned char *s = 0;

  if ((s = hr_cgi_nname(phr, "action_", 7))) {
    if (sscanf(s, "action_%d%n", &action, &n) != 1 || s[n] || action < 0 || action >= SSERV_CMD_LAST) {
      return -1;
    }
  } else if ((r = hr_cgi_param(phr, "action", &s)) < 0 || !s || !*s) {
    phr->action = 0;
    return 0;
  } else {
    if (sscanf(s, "%d%n", &action, &n) != 1 || s[n] || action < 0 || action >= SSERV_CMD_LAST) {
      return -1;
    }
  }

  if (action == SSERV_CMD_HTTP_REQUEST) {
    // compatibility option: parse op
    if ((s = hr_cgi_nname(phr, "op_", 3))) {
      if (sscanf(s, "op_%d%n", &action, &n) != 1 || s[n] || action < 0 || action >= SSERV_CMD_LAST)
        return -1;
    } else if (parse_opcode(phr, &action) < 0) {
      return -1;
    }
  }
  phr->action = action;
  return action;
}

static void *self_dl_handle = 0;
static int
do_http_request(FILE *log_f, FILE *out_f, struct http_request_info *phr)
{
  int action = 0;
  int retval = 0;

  if ((action = parse_action(phr)) < 0) {
    FAIL(SSERV_ERR_INV_OPER);
  }

  if (!super_proto_cmd_names[action]) FAIL(SSERV_ERR_INV_OPER);
  if (op_handlers[action] == (handler_func_t) 1) FAIL(SSERV_ERR_NOT_IMPLEMENTED);

  if (!op_handlers[action]) {
    if (self_dl_handle == (void*) 1) FAIL(SSERV_ERR_NOT_IMPLEMENTED);
    self_dl_handle = dlopen(NULL, RTLD_NOW);
    if (!self_dl_handle) {
      err("do_http_request: dlopen failed: %s", dlerror());
      self_dl_handle = (void*) 1;
      FAIL(SSERV_ERR_NOT_IMPLEMENTED);
    }

    int redir_action = action;
    if (super_proto_op_redirect[action] > 0) {
      redir_action = super_proto_op_redirect[action];
      if (redir_action <= 0 || redir_action >= SSERV_CMD_LAST || !super_proto_cmd_names[redir_action]) {
        err("do_http_request: invalid action redirect %d->%d", action, redir_action);
        op_handlers[action] = (handler_func_t) 1;
        FAIL(SSERV_ERR_NOT_IMPLEMENTED);
      }
      if (op_handlers[redir_action] == (handler_func_t) 1) {
        err("do_http_request: not implemented action redirect %d->%d", action, redir_action);
        op_handlers[action] = (handler_func_t) 1;
        FAIL(SSERV_ERR_NOT_IMPLEMENTED);
      }
    }

    if (op_handlers[redir_action]) {
      op_handlers[action] = op_handlers[redir_action];
    } else {
      unsigned char func_name[512];
      snprintf(func_name, sizeof(func_name), "super_serve_op_%s", super_proto_cmd_names[redir_action]);
      void *void_func = dlsym(self_dl_handle, func_name);
      if (!void_func) {
        err("do_http_request: function %s is not found", func_name);
        op_handlers[action] = (handler_func_t) 1;
        FAIL(SSERV_ERR_NOT_IMPLEMENTED);
      }
      op_handlers[action] = (handler_func_t) void_func;
    }
  }

  retval = (*op_handlers[action])(log_f, out_f, phr);

 cleanup:
  return retval;
}

static void
parse_cookie(struct http_request_info *phr)
{
  const unsigned char *cookies = hr_getenv(phr, "HTTP_COOKIE");
  if (!cookies) return;
  const unsigned char *s = cookies;
  ej_cookie_t client_key = 0;
  while (1) {
    while (isspace(*s)) ++s;
    if (strncmp(s, "EJSID=", 6) != 0) {
      while (*s && *s != ';') ++s;
      if (!*s) return;
      ++s;
      continue;
    }
    int n = 0;
    if (sscanf(s + 6, "%llx%n", &client_key, &n) == 1) {
      s += 6 + n;
      if (!*s || isspace(*s) || *s == ';') {
        phr->client_key = client_key;
        return;
      }
    }
    phr->client_key = 0;
    return;
  }
}

static const int external_action_aliases[SSERV_CMD_LAST] =
{
  [SSERV_CMD_SERVE_CFG_PAGE] = SSERV_CMD_CONTEST_XML_PAGE,
  [SSERV_CMD_CNTS_EDIT_USERS_ACCESS_PAGE] = SSERV_CMD_CNTS_EDIT_REGISTER_ACCESS_PAGE,
  [SSERV_CMD_CNTS_EDIT_MASTER_ACCESS_PAGE] = SSERV_CMD_CNTS_EDIT_REGISTER_ACCESS_PAGE,
  [SSERV_CMD_CNTS_EDIT_JUDGE_ACCESS_PAGE] = SSERV_CMD_CNTS_EDIT_REGISTER_ACCESS_PAGE,
  [SSERV_CMD_CNTS_EDIT_TEAM_ACCESS_PAGE] = SSERV_CMD_CNTS_EDIT_REGISTER_ACCESS_PAGE,
  [SSERV_CMD_CNTS_EDIT_SERVE_CONTROL_ACCESS_PAGE] = SSERV_CMD_CNTS_EDIT_REGISTER_ACCESS_PAGE,
  [SSERV_CMD_CNTS_EDIT_RESERVE_FIELDS_PAGE] = SSERV_CMD_CNTS_EDIT_CONTESTANT_FIELDS_PAGE,
  [SSERV_CMD_CNTS_EDIT_COACH_FIELDS_PAGE] = SSERV_CMD_CNTS_EDIT_CONTESTANT_FIELDS_PAGE,
  [SSERV_CMD_CNTS_EDIT_ADVISOR_FIELDS_PAGE] = SSERV_CMD_CNTS_EDIT_CONTESTANT_FIELDS_PAGE,
  [SSERV_CMD_CNTS_EDIT_GUEST_FIELDS_PAGE] = SSERV_CMD_CNTS_EDIT_CONTESTANT_FIELDS_PAGE,
  [SSERV_CMD_GLOB_EDIT_CONTEST_STOP_CMD_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_GLOB_EDIT_STAND_HEADER_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_GLOB_EDIT_STAND_FOOTER_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_GLOB_EDIT_STAND2_HEADER_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_GLOB_EDIT_STAND2_FOOTER_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_GLOB_EDIT_PLOG_HEADER_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_GLOB_EDIT_PLOG_FOOTER_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_EDIT_USERS_HEADER_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_EDIT_USERS_FOOTER_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_EDIT_COPYRIGHT_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_EDIT_WELCOME_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_EDIT_REG_WELCOME_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_EDIT_REGISTER_EMAIL_FILE_PAGE] = SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE,
  [SSERV_CMD_CNTS_START_EDIT_VARIANT_ACTION] = SSERV_CMD_CNTS_EDIT_CUR_VARIANT_PAGE,
};
static const unsigned char * const external_action_names[SSERV_CMD_LAST] =
{
  [SSERV_CMD_BROWSE_PROBLEM_PACKAGES] = "problem_packages_page",
  [SSERV_CMD_LOGIN_PAGE] = "login_page",
  [SSERV_CMD_MAIN_PAGE] = "main_page",
  [SSERV_CMD_CONTEST_PAGE] = "contest_page",
  [SSERV_CMD_CONTEST_XML_PAGE] = "contest_xml_page",
  [SSERV_CMD_CREATE_CONTEST_PAGE] = "create_contest_page",
  [SSERV_CMD_CREATE_CONTEST_2_ACTION] = "create_contest_2_action",
  [SSERV_CMD_CONTEST_ALREADY_EDITED_PAGE] = "contest_already_edited_page",
  [SSERV_CMD_CONTEST_LOCKED_PAGE] = "contest_locked_page",
  [SSERV_CMD_CHECK_TESTS_PAGE] = "check_tests_page",
  [SSERV_CMD_CNTS_EDIT_PERMISSIONS_PAGE] = "cnts_edit_permissions_page",
  [SSERV_CMD_CNTS_EDIT_REGISTER_ACCESS_PAGE] = "cnts_edit_access_page",
  [SSERV_CMD_CNTS_EDIT_USER_FIELDS_PAGE] = "cnts_edit_user_fields_page",
  [SSERV_CMD_CNTS_EDIT_CONTESTANT_FIELDS_PAGE] = "cnts_edit_member_fields_page",
  [SSERV_CMD_CNTS_START_EDIT_ACTION] = "cnts_start_edit_action",
  [SSERV_CMD_CNTS_EDIT_CUR_CONTEST_PAGE] = "cnts_edit_cur_contest_page",
  [SSERV_CMD_GLOB_EDIT_CONTEST_START_CMD_PAGE] = "cnts_edit_file_page",
  [SSERV_CMD_CNTS_RELOAD_FILE_ACTION] = "cnts_reload_file_action",
  [SSERV_CMD_CNTS_CLEAR_FILE_ACTION] = "cnts_clear_file_action",
  [SSERV_CMD_CNTS_SAVE_FILE_ACTION] = "cnts_save_file_action",
  [SSERV_CMD_CNTS_EDIT_CUR_GLOBAL_PAGE] = "cnts_edit_cur_global_page",
  [SSERV_CMD_CNTS_EDIT_CUR_LANGUAGE_PAGE] = "cnts_edit_cur_language_page",
  [SSERV_CMD_CNTS_EDIT_CUR_PROBLEM_PAGE] = "cnts_edit_cur_problem_page",
  [SSERV_CMD_CNTS_START_EDIT_PROBLEM_ACTION] = "cnts_start_edit_problem_action",
  [SSERV_CMD_CNTS_EDIT_CUR_VARIANT_PAGE] = "cnts_edit_cur_variant_page",
  [SSERV_CMD_CNTS_NEW_SERVE_CFG_PAGE] = "cnts_new_serve_cfg_page",
  [SSERV_CMD_CNTS_COMMIT_PAGE] = "cnts_commit_page",
  [SSERV_CMD_USER_BROWSE_PAGE] = "user_browse_page",
  [SSERV_CMD_USER_BROWSE_DATA] = "user_browse_data",
  [SSERV_CMD_GET_CONTEST_LIST] = "get_contest_list",
  [SSERV_CMD_CNTS_SAVE_BASIC_FORM] = "cnts_save_basic_form",
  [SSERV_CMD_CNTS_SAVE_FLAGS_FORM] = "cnts_save_flags_form",
  [SSERV_CMD_CNTS_SAVE_REGISTRATION_FORM] = "cnts_save_registration_form",
  [SSERV_CMD_CNTS_SAVE_TIMING_FORM] = "cnts_save_timing_form",
  [SSERV_CMD_CNTS_SAVE_URLS_FORM] = "cnts_save_urls_form",
  [SSERV_CMD_CNTS_SAVE_HEADERS_FORM] = "cnts_save_headers_form",
  [SSERV_CMD_CNTS_SAVE_ATTRS_FORM] = "cnts_save_attrs_form",
  [SSERV_CMD_CNTS_SAVE_NOTIFICATIONS_FORM] = "cnts_save_notifications_form",
  [SSERV_CMD_CNTS_SAVE_ADVANCED_FORM] = "cnts_save_advanced_form",
};

static const unsigned char * const external_error_names[SSERV_ERR_LAST] = 
{
  [1] = "error_unknown_page", // here comes the default error handler
};

static ExternalActionState *external_action_states[SSERV_CMD_LAST];
static ExternalActionState *external_error_states[SSERV_ERR_LAST];

static void
default_error_page(
        char **p_out_t,
        size_t *p_out_z,
        struct http_request_info *phr)
{
  if (phr->log_f) {
    fclose(phr->log_f); phr->log_f = NULL;
  }
  FILE *out_f = open_memstream(p_out_t, p_out_z);

  if (phr->error_code < 0) phr->error_code = -phr->error_code;
  unsigned char buf[32];
  const unsigned char *errmsg = 0;
  if (phr->error_code > 0 && phr->error_code < SSERV_ERR_LAST) {
    errmsg = super_proto_error_messages[phr->error_code];
  }
  if (!errmsg) {
    snprintf(buf, sizeof(buf), "%d", phr->error_code);
    errmsg = buf;
  }

  fprintf(out_f, "Content-type: text/html; charset=%s\n\n", EJUDGE_CHARSET);
  fprintf(out_f,
          "<html>\n"
          "<head>\n"
          "<title>Error: %s</title>\n"
          "</head>\n"
          "<body>\n"
          "<h1>Error: %s</h1>\n",
          errmsg, errmsg);
  if (phr->log_t && *phr->log_t) {
    fprintf(out_f, "<p>Additional messages:</p>\n");
    unsigned char *s = html_armor_string_dup(phr->log_t);
    fprintf(out_f, "<pre><font color=\"red\">%s</font></pre>\n", s);
    xfree(s); s = NULL;
    xfree(phr->log_t); phr->log_t = NULL;
    phr->log_z = 0;
  }
  fprintf(out_f, "</body>\n</html>\n");
  fclose(out_f); out_f = NULL;
}

typedef PageInterface *(*external_action_handler_t)(void);

static void
external_error_page(
        char **p_out_t,
        size_t *p_out_z,
        struct http_request_info *phr,
        int error_code)
{
  if (error_code < 0) error_code = -error_code;
  if (error_code <= 0 || error_code >= SSERV_ERR_LAST) error_code = 1;
  phr->error_code = error_code;

  if (!external_error_names[error_code]) error_code = 1;
  if (!external_error_names[error_code]) {
    default_error_page(p_out_t, p_out_z, phr);
    return;
  }

  external_error_states[error_code] = external_action_load(external_error_states[error_code],
                                                           "csp/super-server",
                                                           external_error_names[error_code],
                                                           "csp_get_",
                                                           phr->current_time);
  if (!external_error_states[error_code] || !external_error_states[error_code]->action_handler) {
    default_error_page(p_out_t, p_out_z, phr);
    return;
  }
  PageInterface *pg = ((external_action_handler_t) external_error_states[error_code]->action_handler)();
  if (!pg) {
    default_error_page(p_out_t, p_out_z, phr);
    return;
  }

  phr->out_f = open_memstream(p_out_t, p_out_z);
  fprintf(phr->out_f, "Content-type: text/html; charset=%s\n\n", EJUDGE_CHARSET);
  pg->ops->render(pg, NULL, phr->out_f, phr);
  xfree(phr->log_t); phr->log_t = NULL;
  phr->log_z = 0;
  fclose(phr->out_f); phr->out_f = NULL;
}

void
super_html_http_request(
        char **p_out_t,
        size_t *p_out_z,
        struct http_request_info *phr)
{
  int r = 0, n;
  struct html_armor_buffer ab = HTML_ARMOR_INITIALIZER;
  const unsigned char *script_name = 0;
  const unsigned char *protocol = "http";
  const unsigned char *s = 0;
  unsigned char self_url_buf[4096];
  unsigned char context_url[4096];
  unsigned char hid_buf[4096];
  int ext_action = 0;

  if (hr_getenv(phr, "SSL_PROTOCOL") || hr_getenv(phr, "HTTPS")) {
    phr->ssl_flag = 1;
    protocol = "https";
  }
  if (!(phr->http_host = hr_getenv(phr, "HTTP_HOST"))) phr->http_host = "localhost";
  if (!(script_name = hr_getenv(phr, "SCRIPT_NAME")))
    script_name = "/cgi-bin/serve-control";
  snprintf(self_url_buf, sizeof(self_url_buf), "%s://%s%s", protocol, phr->http_host, script_name);
  phr->self_url = self_url_buf;
  phr->script_name = script_name;

  snprintf(context_url, sizeof(context_url), "%s", phr->self_url);
  unsigned char *rs = strrchr(context_url, '/');
  if (rs) *rs = 0;
  phr->context_url = context_url;

  if (phr->anonymous_mode) {
    phr->action = SSERV_CMD_LOGIN_PAGE;
  } else {
    parse_cookie(phr);
    if (parse_action(phr) < 0) {
      r = -SSERV_ERR_INV_OPER;
    } else {
      r = 0;
    }

    if (!r) {
      if ((r = hr_cgi_param(phr, "SID", &s)) < 0) {
        r = -SSERV_ERR_INV_SID;
      }
      if (r > 0) {
        r = 0;
        if (sscanf(s, "%llx%n", &phr->session_id, &n) != 1
            || s[n] || !phr->session_id) {
          r = -SSERV_ERR_INV_SID;
        }
      }
    }
  }

  hr_cgi_param_int_opt(phr, "contest_id", &phr->contest_id, 0);

  if (!r) {
    // try external actions
    ext_action = phr->action;

redo_action:
    // main_page by default
    if (!super_proto_cmd_names[ext_action]) ext_action = SSERV_CMD_MAIN_PAGE;

    if (ext_action < 0 || ext_action >= SSERV_CMD_LAST) ext_action = 0;
    if (external_action_aliases[ext_action] > 0) ext_action = external_action_aliases[ext_action];
    if (external_action_names[ext_action]) {
      if (phr->current_time <= 0) phr->current_time = time(NULL);
      external_action_states[ext_action] = external_action_load(external_action_states[ext_action],
                                                                "csp/super-server",
                                                                external_action_names[ext_action],
                                                                "csp_get_",
                                                                phr->current_time);
      if (!external_action_states[ext_action] || !external_action_states[ext_action]->action_handler) {
        external_error_page(p_out_t, p_out_z, phr, SSERV_ERR_INV_OPER);
        return;
      }

      snprintf(hid_buf, sizeof(hid_buf),
               "<input type=\"hidden\" name=\"SID\" value=\"%016llx\"/>",
               phr->session_id);
      phr->hidden_vars = hid_buf;

      phr->log_f = open_memstream(&phr->log_t, &phr->log_z);
      phr->out_f = open_memstream(&phr->out_t, &phr->out_z);
      PageInterface *pg = ((external_action_handler_t) external_action_states[ext_action]->action_handler)();
      if (pg->ops->execute) {
        r = pg->ops->execute(pg, phr->log_f, phr);
        if (r < 0) {
          fclose(phr->out_f); phr->out_f = NULL;
          xfree(phr->out_t); phr->out_t = NULL;
          phr->out_z = 0;
          external_error_page(p_out_t, p_out_z, phr, -r);
          return;
        }
      }
      if (pg->ops->render) {
        snprintf(phr->content_type, sizeof(phr->content_type), "text/html; charset=%s", EJUDGE_CHARSET);
        r = pg->ops->render(pg, phr->log_f, phr->out_f, phr);
        if (r < 0) {
          fclose(phr->out_f); phr->out_f = NULL;
          xfree(phr->out_t); phr->out_t = NULL;
          phr->out_z = 0;
          external_error_page(p_out_t, p_out_z, phr, -r);
          return;
        }
        if (r > 0) {
          ext_action = r;
          if (pg->ops->destroy) pg->ops->destroy(pg);
          pg = NULL;
          fclose(phr->out_f); phr->out_f = NULL;
          xfree(phr->out_t); phr->out_t = NULL;
          goto redo_action;
        }
      }
      if (pg->ops->destroy) {
        pg->ops->destroy(pg);
      }
      pg = NULL;

      fclose(phr->log_f); phr->log_f = NULL;
      xfree(phr->log_t); phr->log_t = NULL;
      phr->log_z = 0;
      fclose(phr->out_f); phr->out_f = NULL;

      if (phr->redirect) {
        xfree(phr->out_t); phr->out_t = NULL;
        phr->out_z = 0;

        FILE *tmp_f = open_memstream(p_out_t, p_out_z);
        if (phr->client_key) {
          fprintf(tmp_f, "Set-Cookie: EJSID=%016llx; Path=/\n", phr->client_key);
        }
        fprintf(tmp_f, "Location: %s\n\n", phr->redirect);
        fclose(tmp_f); tmp_f = NULL;

        xfree(phr->redirect); phr->redirect = NULL;
      } else {
        FILE *tmp_f = open_memstream(p_out_t, p_out_z);
        fprintf(tmp_f, "Content-type: %s\n\n", phr->content_type);
        fwrite(phr->out_t, 1, phr->out_z, tmp_f);
        fclose(tmp_f); tmp_f = NULL;

        xfree(phr->out_t); phr->out_t = NULL;
        phr->out_z = 0;
      }
      return;
    }
  }

  if (!r) {
    phr->out_f = open_memstream(&phr->out_t, &phr->out_z);
    phr->log_f = open_memstream(&phr->log_t, &phr->log_z);
    r = do_http_request(phr->log_f, phr->out_f, phr);
    if (r >= 0 && phr->suspend_reply) {
      html_armor_free(&ab);
      return;
    }
    close_memstream(phr->out_f); phr->out_f = 0;
    close_memstream(phr->log_f); phr->log_f = 0;
  }

  if (r < 0) {
    xfree(phr->out_t); phr->out_t = 0; phr->out_z = 0;
    phr->out_f = open_memstream(&phr->out_t, &phr->out_z);
    if (phr->json_reply) {
      write_json_header(phr->out_f);
      fprintf(phr->out_f, "{ \"status\": %d, \"text\": \"%s\" }",
              r, super_proto_error_messages[-r]);
    } else {
      write_html_header(phr->out_f, phr, "Request failed", 0, 0);
      if (r < -1 && r > -SSERV_ERR_LAST) {
        fprintf(phr->out_f, "<h1>Request failed: error %d</h1>\n", -r);
        fprintf(phr->out_f, "<h2>%s</h2>\n", super_proto_error_messages[-r]);
      } else {
        fprintf(phr->out_f, "<h1>Request failed</h1>\n");
      }
      fprintf(phr->out_f, "<pre><font color=\"red\">%s</font></pre>\n",
              ARMOR(phr->log_t));
      write_html_footer(phr->out_f);
    }
    close_memstream(phr->out_f); phr->out_f = 0;
  }
  xfree(phr->log_t); phr->log_t = 0; phr->log_z = 0;

  if (!phr->out_t || !*phr->out_t) {
    xfree(phr->out_t); phr->out_t = 0; phr->out_z = 0;
    phr->out_f = open_memstream(&phr->out_t, &phr->out_z);
    if (phr->json_reply) {
      write_json_header(phr->out_f);
      fprintf(phr->out_f, "{ \"status\": %d }", r);
    } else {
      write_html_header(phr->out_f, phr, "Empty output", 0, 0);
      fprintf(phr->out_f, "<h1>Empty output</h1>\n");
      fprintf(phr->out_f, "<p>The output page is empty!</p>\n");
      write_html_footer(phr->out_f);
    }
    close_memstream(phr->out_f); phr->out_f = 0;
  }

  /*
  if (phr->json_reply) {
    fprintf(stderr, "json: %s\n", out_t);
  }
  */

  *p_out_t = phr->out_t;
  *p_out_z = phr->out_z;
  html_armor_free(&ab);
}

void *super_html_forced_link[] =
{
  html_date_select
};
