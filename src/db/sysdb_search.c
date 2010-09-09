/*
   SSSD

   System Database

   Copyright (C) Simo Sorce <ssorce@redhat.com>	2008

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "util/util.h"
#include "db/sysdb_private.h"
#include "confdb/confdb.h"
#include <time.h>
#include <ctype.h>

/* users */

int sysdb_getpwnam(TALLOC_CTX *mem_ctx,
                   struct sysdb_ctx *ctx,
                   struct sss_domain_info *domain,
                   const char *name,
                   struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    static const char *attrs[] = SYSDB_PW_ATTRS;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                             SYSDB_TMPL_USER_BASE, domain->name);
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attrs, SYSDB_PWNAM_FILTER, name);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

int sysdb_getpwuid(TALLOC_CTX *mem_ctx,
                   struct sysdb_ctx *ctx,
                   struct sss_domain_info *domain,
                   uid_t uid,
                   struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    unsigned long int ul_uid = uid;
    static const char *attrs[] = SYSDB_PW_ATTRS;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                             SYSDB_TMPL_USER_BASE, domain->name);
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attrs, SYSDB_PWUID_FILTER, ul_uid);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

int sysdb_enumpwent(TALLOC_CTX *mem_ctx,
                    struct sysdb_ctx *ctx,
                    struct sss_domain_info *domain,
                    struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    static const char *attrs[] = SYSDB_PW_ATTRS;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                             SYSDB_TMPL_USER_BASE, domain->name);
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attrs, SYSDB_PWENT_FILTER);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

/* groups */

static int mpg_convert(struct ldb_message *msg)
{
    struct ldb_message_element *el;
    struct ldb_val *val;
    int i;

    el = ldb_msg_find_element(msg, "objectClass");
    if (!el) return EINVAL;

    /* see if this is a user to convert to a group */
    for (i = 0; i < el->num_values; i++) {
        val = &(el->values[i]);
        if (strncasecmp(SYSDB_USER_CLASS,
                        (char *)val->data, val->length) == 0) {
            break;
        }
    }
    /* no, leave as is */
    if (i == el->num_values) return EOK;

    /* yes, convert */
    val->data = (uint8_t *)talloc_strdup(msg, SYSDB_GROUP_CLASS);
    if (val->data == NULL) return ENOMEM;
    val->length = strlen(SYSDB_GROUP_CLASS);

    return EOK;
}

static int mpg_res_convert(struct ldb_result *res)
{
    int ret;
    int i;

    for (i = 0; i < res->count; i++) {
        ret = mpg_convert(res->msgs[i]);
        if (ret) {
            return ret;
        }
    }
    return EOK;
}

int sysdb_getgrnam(TALLOC_CTX *mem_ctx,
                   struct sysdb_ctx *ctx,
                   struct sss_domain_info *domain,
                   const char *name,
                   struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    static const char *attrs[] = SYSDB_GRSRC_ATTRS;
    const char *fmt_filter;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    if (ctx->mpg) {
        fmt_filter = SYSDB_GRNAM_MPG_FILTER;
        base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                                 SYSDB_DOM_BASE, domain->name);
    } else {
        fmt_filter = SYSDB_GRNAM_FILTER;
        base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                                 SYSDB_TMPL_GROUP_BASE, domain->name);
    }
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attrs, fmt_filter, name);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    ret = mpg_res_convert(res);
    if (ret) {
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

int sysdb_getgrgid(TALLOC_CTX *mem_ctx,
                   struct sysdb_ctx *ctx,
                   struct sss_domain_info *domain,
                   gid_t gid,
                   struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    unsigned long int ul_gid = gid;
    static const char *attrs[] = SYSDB_GRSRC_ATTRS;
    const char *fmt_filter;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    if (ctx->mpg) {
        fmt_filter = SYSDB_GRGID_MPG_FILTER;
        base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                                 SYSDB_DOM_BASE, domain->name);
    } else {
        fmt_filter = SYSDB_GRGID_FILTER;
        base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                                 SYSDB_TMPL_GROUP_BASE, domain->name);
    }
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attrs, fmt_filter, ul_gid);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    ret = mpg_res_convert(res);
    if (ret) {
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

int sysdb_enumgrent(TALLOC_CTX *mem_ctx,
                    struct sysdb_ctx *ctx,
                    struct sss_domain_info *domain,
                    struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    static const char *attrs[] = SYSDB_GRSRC_ATTRS;
    const char *fmt_filter;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    if (ctx->mpg) {
        fmt_filter = SYSDB_GRENT_MPG_FILTER;
        base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                                 SYSDB_DOM_BASE, domain->name);
    } else {
        fmt_filter = SYSDB_GRENT_FILTER;
        base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                                 SYSDB_TMPL_GROUP_BASE, domain->name);
    }
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attrs, fmt_filter);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    ret = mpg_res_convert(res);
    if (ret) {
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

int sysdb_initgroups(TALLOC_CTX *mem_ctx,
                     struct sysdb_ctx *ctx,
                     struct sss_domain_info *domain,
                     const char *name,
                     struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    struct ldb_result *res;
    struct ldb_dn *user_dn;
    struct ldb_request *req;
    struct ldb_control **ctrl;
    struct ldb_asq_control *control;
    static const char *attrs[] = SYSDB_INITGR_ATTRS;
    int ret;

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    ret = sysdb_getpwnam(tmpctx, ctx, domain, name, &res);
    if (ret != EOK) {
        DEBUG(1, ("sysdb_getpwnam failed: [%d][%s]\n",
                  ret, strerror(ret)));
        goto done;
    }

    if (res->count == 0) {
        /* User is not cached yet */
        *_res = talloc_steal(mem_ctx, res);
        ret = EOK;
        goto done;

    } else if (res->count != 1) {
        ret = EIO;
        DEBUG(1, ("sysdb_getpwnam returned count: [%d]\n", res->count));
        goto done;
    }

    /* no need to steal the dn, we are not freeing the result */
    user_dn = res->msgs[0]->dn;

    /* note we count on the fact that the default search callback
     * will just keep appending values. This is by design and can't
     * change so it is ok to already have a result (from the getpwnam)
     * even before we call the next search */

    ctrl = talloc_array(tmpctx, struct ldb_control *, 2);
    if (!ctrl) {
        ret = ENOMEM;
        goto done;
    }
    ctrl[1] = NULL;
    ctrl[0] = talloc(ctrl, struct ldb_control);
    if (!ctrl[0]) {
        ret = ENOMEM;
        goto done;
    }
    ctrl[0]->oid = LDB_CONTROL_ASQ_OID;
    ctrl[0]->critical = 1;
    control = talloc(ctrl[0], struct ldb_asq_control);
    if (!control) {
        ret = ENOMEM;
        goto done;
    }
    control->request = 1;
    control->source_attribute = talloc_strdup(control, SYSDB_INITGR_ATTR);
    if (!control->source_attribute) {
        ret = ENOMEM;
        goto done;
    }
    control->src_attr_len = strlen(control->source_attribute);
    ctrl[0]->data = control;

    ret = ldb_build_search_req(&req, ctx->ldb, tmpctx,
                               user_dn, LDB_SCOPE_BASE,
                               SYSDB_INITGR_FILTER, attrs, ctrl,
                               res, ldb_search_default_callback,
                               NULL);
    if (ret != LDB_SUCCESS) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    ret = ldb_request(ctx->ldb, req);
    if (ret == LDB_SUCCESS) {
        ret = ldb_wait(req->handle, LDB_WAIT_ALL);
    }
    if (ret != LDB_SUCCESS) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

int sysdb_get_user_attr(TALLOC_CTX *mem_ctx,
                        struct sysdb_ctx *ctx,
                        struct sss_domain_info *domain,
                        const char *name,
                        const char **attributes,
                        struct ldb_result **_res)
{
    TALLOC_CTX *tmpctx;
    struct ldb_dn *base_dn;
    struct ldb_result *res;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                             SYSDB_TMPL_USER_BASE, domain->name);
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &res, base_dn,
                     LDB_SCOPE_SUBTREE, attributes,
                     SYSDB_PWNAM_FILTER, name);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    *_res = talloc_steal(mem_ctx, res);

done:
    talloc_zfree(tmpctx);
    return ret;
}

/* This function splits a three-tuple into three strings
 * It assumes that any whitespace between the parentheses
 * and commas are intentional and does not attempt to
 * strip them out. Leading and trailing whitespace is
 * ignored.
 *
 * This behavior is compatible with nss_ldap's
 * implementation.
 */
static errno_t sysdb_netgr_split_triple(TALLOC_CTX *mem_ctx,
                                        const char *triple,
                                        char **hostname,
                                        char **username,
                                        char **domainname)
{
    errno_t ret;
    TALLOC_CTX *tmp_ctx;
    const char *p = triple;
    const char *p_host;
    const char *p_user;
    const char *p_domain;
    size_t len;

    char *host = NULL;
    char *user = NULL;
    char *domain = NULL;

    /* Pre-set the values to NULL here so if they are not
     * copied, we don't return garbage below.
     */
    *hostname = NULL;
    *username = NULL;
    *domainname = NULL;

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) {
        return ENOMEM;
    }

    /* Remove any leading whitespace */
    while (*p && isspace(*p)) p++;

    if (*p != '(') {
        /* Triple must start and end with parentheses */
        ret = EINVAL;
        goto done;
    }
    p++;
    p_host = p;

    /* Find the first comma */
    while (*p && *p != ',') p++;

    if (!*p) {
        /* No comma was found: parse error */
        ret = EINVAL;
        goto done;
    }

    len = p - p_host;

    if (len > 0) {
        /* Copy the host string */
        host = talloc_strndup(tmp_ctx, p_host, len);
        if (!host) {
            ret = ENOMEM;
            goto done;
        }
    }
    p++;
    p_user = p;

    /* Find the second comma */
    while (*p && *p != ',') p++;

    if (!*p) {
        /* No comma was found: parse error */
        ret = EINVAL;
        goto done;
    }

    len = p - p_user;

    if (len > 0) {
        /* Copy the user string */
        user = talloc_strndup(tmp_ctx, p_user, len);
        if (!user) {
            ret = ENOMEM;
            goto done;
        }
    }
    p++;
    p_domain = p;

    /* Find the closing parenthesis */
    while (*p && *p != ')') p++;
    if (*p != ')') {
        /* No trailing parenthesis: parse error */
        ret = EINVAL;
        goto done;
    }

    len = p - p_domain;

    if (len > 0) {
        /* Copy the domain string */
        domain = talloc_strndup(tmp_ctx, p_domain, len);
        if (!domain) {
            ret = ENOMEM;
            goto done;
        }
    }
    p++;

    /* skip trailing whitespace */
    while (*p && isspace(*p)) p++;

    if (*p) {
        /* Extra data after the closing parenthesis
         * is a parse error
         */
        ret = EINVAL;
        goto done;
    }

    /* Return any non-NULL values */
    if (host) {
        *hostname = talloc_steal(mem_ctx, host);
    }

    if (user) {
        *username = talloc_steal(mem_ctx, user);
    }

    if (domain) {
        *domainname = talloc_steal(mem_ctx, domain);
    }

    ret = EOK;

done:
    talloc_free(tmp_ctx);
    return ret;
}

errno_t sysdb_netgr_to_triples(TALLOC_CTX *mem_ctx,
                               struct ldb_result *res,
                               struct sysdb_netgroup_ctx ***triples)
{
    errno_t ret;
    size_t size = 0;
    char *triple_str;
    TALLOC_CTX *tmp_ctx;
    struct sysdb_netgroup_ctx **tmp_triples = NULL;
    struct ldb_message_element *el;
    int i, j;

    if(!res || res->count == 0) {
        return ENOENT;
    }

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) {
        return ENOMEM;
    }

    for (i=0; i < res->count; i++) {
        el = ldb_msg_find_element(res->msgs[i], SYSDB_NETGROUP_TRIPLE);
        if (!el) {
            /* No triples in this netgroup. It might be a nesting
             * container only.
             * Skip it and continue on.
             */
            continue;
        }

        /* Enlarge the array by the value count
         * Always keep one extra entry for the NULL terminator
         */
        tmp_triples = talloc_realloc(tmp_ctx, tmp_triples,
                                     struct sysdb_netgroup_ctx *,
                                     size+el->num_values+1);
        if (!tmp_triples) {
            ret = ENOMEM;
            goto done;
        }

        /* Copy in all of the triples */
        for(j = 0; j < el->num_values; j++) {
            triple_str = talloc_strndup(tmp_ctx,
                                       (const char *)el->values[j].data,
                                       el->values[j].length);
            if (!triple_str) {
                ret = ENOMEM;
                goto done;
            }

            tmp_triples[size] = talloc_zero(tmp_triples,
                                            struct sysdb_netgroup_ctx);
            if (!tmp_triples[size]) {
                ret = ENOMEM;
                goto done;
            }

            ret = sysdb_netgr_split_triple(tmp_triples[size],
                                           triple_str,
                                           &tmp_triples[size]->hostname,
                                           &tmp_triples[size]->username,
                                           &tmp_triples[size]->domainname);
            if (ret != EOK) {
                goto done;
            }

            size++;
        }
    }

    if (!tmp_triples) {
        /* No entries were found
         * Create a dummy reply
         */
        tmp_triples = talloc_array(tmp_ctx, struct sysdb_netgroup_ctx *, 1);
        if (!tmp_triples) {
            ret = ENOMEM;
            goto done;
        }
    }
    /* Add NULL terminator */
    tmp_triples[size] = NULL;

    *triples = talloc_steal(mem_ctx, tmp_triples);
    ret = EOK;

done:
    talloc_free(tmp_ctx);
    return ret;
}

errno_t sysdb_getnetgr(TALLOC_CTX *mem_ctx,
                       struct sysdb_ctx *ctx,
                       struct sss_domain_info *domain,
                       const char *netgroup,
                       struct ldb_result **res)
{
    TALLOC_CTX *tmp_ctx;
    static const char *attrs[] = SYSDB_NETGR_ATTRS;
    struct ldb_dn *base_dn;
    struct ldb_result *result;
    char *netgroup_dn;
    int lret;
    errno_t ret;

    if (!domain) {
        return EINVAL;
    }

    tmp_ctx = talloc_new(NULL);
    if (!tmp_ctx) {
        return ENOMEM;
    }

    base_dn = ldb_dn_new_fmt(tmp_ctx, ctx->ldb,
                             SYSDB_TMPL_NETGROUP_BASE,
                             domain->name);
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    netgroup_dn = talloc_asprintf(tmp_ctx, SYSDB_TMPL_NETGROUP,
                                  netgroup, domain->name);
    if (!netgroup_dn) {
        ret = ENOMEM;
        goto done;
    }

    lret = ldb_search(ctx->ldb, tmp_ctx, &result, base_dn,
                     LDB_SCOPE_SUBTREE, attrs,
                     SYSDB_NETGR_TRIPLES_FILTER,
                     netgroup, netgroup_dn);
    ret = sysdb_error_to_errno(lret);
    if (ret != EOK) {
        goto done;
    }

    *res = talloc_steal(mem_ctx, result);
    ret = EOK;

done:
    talloc_zfree(tmp_ctx);
    return ret;
}

int sysdb_get_netgroup_attr(TALLOC_CTX *mem_ctx,
                            struct sysdb_ctx *ctx,
                            struct sss_domain_info *domain,
                            const char *netgrname,
                            const char **attributes,
                            struct ldb_result **res)
{
    TALLOC_CTX *tmpctx;
    struct ldb_dn *base_dn;
    struct ldb_result *result;
    int ret;

    if (!domain) {
        return EINVAL;
    }

    tmpctx = talloc_new(mem_ctx);
    if (!tmpctx) {
        return ENOMEM;
    }

    base_dn = ldb_dn_new_fmt(tmpctx, ctx->ldb,
                             SYSDB_TMPL_NETGROUP_BASE, domain->name);
    if (!base_dn) {
        ret = ENOMEM;
        goto done;
    }

    ret = ldb_search(ctx->ldb, tmpctx, &result, base_dn,
                     LDB_SCOPE_SUBTREE, attributes,
                     SYSDB_NETGR_FILTER, netgrname);
    if (ret) {
        ret = sysdb_error_to_errno(ret);
        goto done;
    }

    *res = talloc_steal(mem_ctx, result);
done:
    talloc_zfree(tmpctx);
    return ret;
}
