/*	$OpenBSD: ruleset.c,v 1.26 2012/11/12 14:58:53 eric Exp $ */

/*
 * Copyright (c) 2009 Gilles Chehade <gilles@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "includes.h"

#include <sys/types.h>
#include "sys-queue.h"
#include "sys-tree.h"
#include <sys/param.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <errno.h>
#include <event.h>
#include <imsg.h>
#include <stdio.h>
#include <string.h>

#include "smtpd.h"
#include "log.h"


static int ruleset_check_source(struct table *,
    const struct sockaddr_storage *);

struct rule *
ruleset_match(const struct envelope *evp)
{
	const struct mailaddr *maddr = &evp->rcpt;
	const struct sockaddr_storage *ss = &evp->ss;
	struct rule	*r;
	int		 ret;

	if (evp->flags & DF_INTERNAL)
		ss = NULL;

	TAILQ_FOREACH(r, env->sc_rules, r_entry) {

		if (r->r_tag[0] != '\0' && strcmp(r->r_tag, evp->tag) != 0)
			continue;

		if (ss != NULL && !(evp->flags & DF_AUTHENTICATED)) {
			ret = ruleset_check_source(r->r_sources, ss);
			if (ret == -1) {
				errno = EAGAIN;
				return (NULL);
			}
			if (ret == 0)
				continue;
		}

		ret = r->r_destination == NULL ? 1 :
		    table_lookup(r->r_destination, maddr->domain, K_DOMAIN,
			NULL);
		if (ret == -1) {
			errno = EAGAIN;
			return NULL;
		}
		if (ret) {
			if (r->r_desttype == DEST_VDOM &&
			    (r->r_action == A_RELAY || r->r_action == A_RELAYVIA)) {
				if (! aliases_virtual_check(r->r_mapping,
					&evp->rcpt)) {
					return NULL;
				}
			}
			goto matched;
		}
	}

	errno = 0;
	return (NULL);

matched:
	log_trace(TRACE_RULES, "rule matched: %s", rule_to_text(r));
	return r;
}

static int
ruleset_check_source(struct table *table, const struct sockaddr_storage *ss)
{
	char   *key;

	if (ss == NULL) {
		/* This happens when caller is part of an internal
		 * lookup (ie: alias resolved to a remote address)
		 */
		return 1;
	}

	key = ss->ss_family == AF_LOCAL ? "local" : ss_to_text(ss);
	switch (table_lookup(table, key, K_NETADDR, NULL)) {
	case 1:
		return 1;
	case -1:
		log_warnx("warn: failure to perform a table lookup on table %s",
		    table->t_name);
	default:
		break;
	}

	return 0;
}
