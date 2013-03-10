/*
 * firewall3 - 3rd OpenWrt UCI firewall implementation
 *
 *   Copyright (C) 2013 Jo-Philipp Wich <jow@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include "defaults.h"


#define C(f, tbl, def, name) \
	{ FW3_FAMILY_##f, FW3_TABLE_##tbl, FW3_DEFAULT_##def, name }

struct chain {
	enum fw3_family family;
	enum fw3_table table;
	enum fw3_default flag;
	const char *name;
};

static const struct chain default_chains[] = {
	C(ANY, FILTER, UNSPEC,        "delegate_input"),
	C(ANY, FILTER, UNSPEC,        "delegate_output"),
	C(ANY, FILTER, UNSPEC,        "delegate_forward"),
	C(ANY, FILTER, CUSTOM_CHAINS, "input_rule"),
	C(ANY, FILTER, CUSTOM_CHAINS, "output_rule"),
	C(ANY, FILTER, CUSTOM_CHAINS, "forwarding_rule"),
	C(ANY, FILTER, UNSPEC,        "reject"),
	C(ANY, FILTER, SYN_FLOOD,     "syn_flood"),

	C(V4,  NAT,    UNSPEC,        "delegate_prerouting"),
	C(V4,  NAT,    UNSPEC,        "delegate_postrouting"),
	C(V4,  NAT,    CUSTOM_CHAINS, "prerouting_rule"),
	C(V4,  NAT,    CUSTOM_CHAINS, "postrouting_rule"),

	C(ANY, MANGLE, UNSPEC,        "mssfix"),
	C(ANY, RAW,    UNSPEC,        "notrack"),
};

static const struct chain toplevel_rules[] = {
	C(ANY, FILTER, UNSPEC,        "INPUT -j delegate_input"),
	C(ANY, FILTER, UNSPEC,        "OUTPUT -j delegate_output"),
	C(ANY, FILTER, UNSPEC,        "FORWARD -j delegate_forward"),

	C(V4,  NAT,    UNSPEC,        "PREROUTING -j delegate_prerouting"),
	C(V4,  NAT,    UNSPEC,        "POSTROUTING -j delegate_postrouting"),

	C(ANY, MANGLE, UNSPEC,        "FORWARD -j mssfix"),
	C(ANY, RAW,    UNSPEC,        "PREROUTING -j notrack"),
};

const struct fw3_option fw3_default_opts[] = {
	FW3_OPT("input",               target,   defaults, policy_input),
	FW3_OPT("forward",             target,   defaults, policy_forward),
	FW3_OPT("output",              target,   defaults, policy_output),

	FW3_OPT("drop_invalid",        bool,     defaults, drop_invalid),

	FW3_OPT("syn_flood",           bool,     defaults, syn_flood),
	FW3_OPT("synflood_protect",    bool,     defaults, syn_flood),
	FW3_OPT("synflood_rate",       limit,    defaults, syn_flood_rate),
	FW3_OPT("synflood_burst",      int,      defaults, syn_flood_rate.burst),

	FW3_OPT("tcp_syncookies",      bool,     defaults, tcp_syncookies),
	FW3_OPT("tcp_ecn",             bool,     defaults, tcp_ecn),
	FW3_OPT("tcp_window_scaling",  bool,     defaults, tcp_window_scaling),

	FW3_OPT("accept_redirects",    bool,     defaults, accept_redirects),
	FW3_OPT("accept_source_route", bool,     defaults, accept_source_route),

	FW3_OPT("custom_chains",       bool,     defaults, custom_chains),
	FW3_OPT("disable_ipv6",        bool,     defaults, disable_ipv6),

	{ }
};


static bool
print_chains(enum fw3_table table, enum fw3_family family,
             const char *fmt, uint32_t flags,
             const struct chain *chains, int n)
{
	bool rv = false;
	const struct chain *c;

	for (c = chains; n > 0; c++, n--)
	{
		if (!fw3_is_family(c, family))
			continue;

		if (c->table != table)
			continue;

		if ((c->flag != FW3_DEFAULT_UNSPEC) && !hasbit(flags, c->flag))
			continue;

		fw3_pr(fmt, c->name);

		rv = true;
	}

	return rv;
}

static void
check_policy(struct uci_element *e, enum fw3_target *pol, const char *name)
{
	if (*pol == FW3_TARGET_UNSPEC)
	{
		warn_elem(e, "has no %s policy specified, defaulting to DROP", name);
		*pol = FW3_TARGET_DROP;
	}
	else if (*pol > FW3_TARGET_DROP)
	{
		warn_elem(e, "has invalid %s policy, defaulting to DROP", name);
		*pol = FW3_TARGET_DROP;
	}
}

void
fw3_load_defaults(struct fw3_state *state, struct uci_package *p)
{
	struct uci_section *s;
	struct uci_element *e;
	struct fw3_defaults *defs = &state->defaults;

	bool seen = false;

	defs->syn_flood_rate.rate  = 25;
	defs->syn_flood_rate.burst = 50;
	defs->tcp_syncookies       = true;
	defs->tcp_window_scaling   = true;
	defs->custom_chains        = true;

	setbit(defs->flags, FW3_FAMILY_V4);

	uci_foreach_element(&p->sections, e)
	{
		s = uci_to_section(e);

		if (strcmp(s->type, "defaults"))
			continue;

		if (seen)
		{
			warn_elem(e, "ignoring duplicate section");
			continue;
		}

		fw3_parse_options(&state->defaults, fw3_default_opts, s);

		check_policy(e, &defs->policy_input, "input");
		check_policy(e, &defs->policy_output, "output");
		check_policy(e, &defs->policy_forward, "forward");

		if (!defs->disable_ipv6)
			setbit(defs->flags, FW3_FAMILY_V6);

		if (defs->custom_chains)
			setbit(defs->flags, FW3_DEFAULT_CUSTOM_CHAINS);

		if (defs->syn_flood)
			setbit(defs->flags, FW3_DEFAULT_SYN_FLOOD);
	}
}

void
fw3_print_default_chains(enum fw3_table table, enum fw3_family family,
                         struct fw3_state *state)
{
	struct fw3_defaults *defs = &state->defaults;
	uint32_t custom_mask = ~0;

#define policy(t) \
	((t == FW3_TARGET_REJECT) ? "DROP" : fw3_flag_names[t])

	if (table == FW3_TABLE_FILTER)
	{
		fw3_pr(":INPUT %s [0:0]\n", policy(defs->policy_input));
		fw3_pr(":FORWARD %s [0:0]\n", policy(defs->policy_forward));
		fw3_pr(":OUTPUT %s [0:0]\n", policy(defs->policy_output));
	}

	/* user chains already loaded, don't create again */
	if (hasbit(state->running_defaults.flags, FW3_DEFAULT_CUSTOM_CHAINS))
		delbit(custom_mask, FW3_DEFAULT_CUSTOM_CHAINS);

	print_chains(table, family, ":%s - [0:0]\n", defs->flags & custom_mask,
	             default_chains, ARRAY_SIZE(default_chains));
}

void
fw3_print_default_head_rules(enum fw3_table table, enum fw3_family family,
                             struct fw3_state *state)
{
	int i;
	struct fw3_defaults *defs = &state->defaults;
	const char *chains[] = {
		"input", "input",
		"output", "output",
		"forward", "forwarding",
	};

	print_chains(table, family, "-A %s\n", 0,
	             toplevel_rules, ARRAY_SIZE(toplevel_rules));

	switch (table)
	{
	case FW3_TABLE_FILTER:
		fw3_pr("-A delegate_input -i lo -j ACCEPT\n");
		fw3_pr("-A delegate_output -o lo -j ACCEPT\n");

		if (defs->custom_chains)
		{
			for (i = 0; i < ARRAY_SIZE(chains); i += 2)
			{
				fw3_pr("-A delegate_%s -m comment "
				       "--comment \"user chain for %s\" -j %s_rule\n",
					   chains[i], chains[i+1], chains[i+1]);
			}
		}

		for (i = 0; i < ARRAY_SIZE(chains); i += 2)
		{
			fw3_pr("-A delegate_%s -m conntrack --ctstate RELATED,ESTABLISHED "
			       "-j ACCEPT\n", chains[i]);

			if (defs->drop_invalid)
			{
				fw3_pr("-A delegate_%s -m conntrack --ctstate INVALID -j DROP\n",
				       chains[i]);
			}
		}

		if (defs->syn_flood)
		{
			fw3_pr("-A syn_flood -p tcp --syn");
			fw3_format_limit(&defs->syn_flood_rate);
			fw3_pr(" -j RETURN\n");

			fw3_pr("-A syn_flood -j DROP\n");
			fw3_pr("-A delegate_input -p tcp --syn -j syn_flood\n");
		}

		fw3_pr("-A reject -p tcp -j REJECT --reject-with tcp-reset\n");
		fw3_pr("-A reject -j REJECT --reject-with port-unreach\n");

		break;

	case FW3_TABLE_NAT:
		if (defs->custom_chains)
		{
			fw3_pr("-A delegate_prerouting "
			       "-m comment --comment \"user chain for prerouting\" "
			       "-j prerouting_rule\n");

			fw3_pr("-A delegate_postrouting "
			       "-m comment --comment \"user chain for postrouting\" "
			       "-j postrouting_rule\n");
		}
		break;

	default:
		break;
	}
}

void
fw3_print_default_tail_rules(enum fw3_table table, enum fw3_family family,
                             struct fw3_state *state)
{
	struct fw3_defaults *defs = &state->defaults;

	if (table != FW3_TABLE_FILTER)
		return;

	if (defs->policy_input == FW3_TARGET_REJECT)
		fw3_pr("-A delegate_input -j reject\n");

	if (defs->policy_output == FW3_TARGET_REJECT)
		fw3_pr("-A delegate_output -j reject\n");

	if (defs->policy_forward == FW3_TARGET_REJECT)
		fw3_pr("-A delegate_forward -j reject\n");
}

static void
set_default(const char *name, bool set)
{
	FILE *f;
	char path[sizeof("/proc/sys/net/ipv4/tcp_window_scaling\0")];

	snprintf(path, sizeof(path), "/proc/sys/net/ipv4/tcp_%s", name);

	info(" * Set tcp_%s to %s", name, set ? "on" : "off", name);

	if (!(f = fopen(path, "w")))
	{
		info("   ! Unable to write value: %s", strerror(errno));
		return;
	}

	fprintf(f, "%u\n", set);
	fclose(f);
}

void
fw3_set_defaults(struct fw3_state *state)
{
	info("Setting sysctl values");

	set_default("ecn",            state->defaults.tcp_ecn);
	set_default("syncookies",     state->defaults.tcp_syncookies);
	set_default("window_scaling", state->defaults.tcp_window_scaling);
}

static void
reset_policy(enum fw3_table table, enum fw3_target policy)
{
	if (table != FW3_TABLE_FILTER)
		return;

	fw3_pr(":INPUT %s [0:0]\n", fw3_flag_names[policy]);
	fw3_pr(":OUTPUT %s [0:0]\n", fw3_flag_names[policy]);
	fw3_pr(":FORWARD %s [0:0]\n", fw3_flag_names[policy]);
}

void
fw3_flush_rules(enum fw3_table table, enum fw3_family family,
                bool pass2, struct fw3_state *state, enum fw3_target policy)
{
	struct fw3_defaults *d = &state->running_defaults;
	uint32_t custom_mask = ~0;

	if (!hasbit(d->flags, family))
		return;

	/* don't touch user chains on selective stop */
	delbit(custom_mask, FW3_DEFAULT_CUSTOM_CHAINS);

	if (!pass2)
	{
		reset_policy(table, policy);

		print_chains(table, family, "-D %s\n", d->flags & custom_mask,
					 toplevel_rules, ARRAY_SIZE(toplevel_rules));

		print_chains(table, family, "-F %s\n", d->flags & custom_mask,
					 default_chains, ARRAY_SIZE(default_chains));
	}
	else
	{
		print_chains(table, family, "-X %s\n", d->flags & custom_mask,
					 default_chains, ARRAY_SIZE(default_chains));

		delbit(d->flags, family);
	}
}

void
fw3_flush_all(enum fw3_table table)
{
	reset_policy(table, FW3_TARGET_ACCEPT);

	fw3_pr("-F\n");
	fw3_pr("-X\n");
}
