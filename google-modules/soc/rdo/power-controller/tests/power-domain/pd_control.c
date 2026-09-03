// SPDX-License-Identifier: GPL-2.0-only

#include "pd_control.h"
#include "pd_defs.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define DEBUGFS_PWR_CTRL "/d/power_controller"
#define DT_PWR_CTRL "/proc/device-tree/power_controller"
#define DEBUGFS_PM_GENPD "/d/pm_genpd"
#define MBFS_ROOT "/sys/firmware/mbfs_root"
#define MBFS_CHERRY_PICK "/sys/firmware/mbfs_root/cherry_pick"
#define MBFS_CLIENT_BASE "/sys/firmware/mbfs_root/cpm/syspm/clients/vm1"

static struct power_domain g_domains[MAX_DOMAINS];
static int g_domain_count;

static void trim_str(char *str)
{
	int len;
	char *start;

	if (!str)
		return;

	len = strlen(str);
	while (len > 0 && isspace((unsigned char)str[len - 1]))
		str[--len] = '\0';

	start = str;
	while (isspace((unsigned char)*start))
		start++;

	if (start != str)
		memmove(str, start, strlen(start) + 1);
}

static int file_exists(const char *path)
{
	struct stat st;

	if (!path)
		return 0;

	return stat(path, &st) == 0;
}

static int is_dir(const char *path)
{
	struct stat st;

	if (!path)
		return 0;

	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void read_file(const char *path, char *out, size_t out_size)
{
	size_t total = 0;
	int fd;
	ssize_t n;

	if (!path || !out || out_size == 0)
		return;

	out[0] = '\0';
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;

	while (total < out_size - 1) {
		n = read(fd, out + total, out_size - 1 - total);
		if (n < 0) {
			fprintf(stderr, "ERROR: Failed reading from %s\n", path);
			break;
		}
		if (n == 0)
			break;
		total += n;
	}
	close(fd);

	if (total > 0) {
		out[total] = '\0';
		trim_str(out);
	}
}

static int write_file(const char *path, const char *data)
{
	const char *buf;
	size_t len;
	int fd;
	ssize_t n;

	if (!path || !data)
		return 0;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return 0;

	len = strlen(data);
	buf = data;
	while (len > 0) {
		n = write(fd, buf, len);
		if (n < 0) {
			fprintf(stderr, "ERROR: Failed writing '%s' to %s\n", data, path);
			close(fd);
			return 0;
		}
		if (n == 0) {
			fprintf(stderr, "ERROR: Zero bytes written to %s\n", path);
			close(fd);
			return 0;
		}
		len -= n;
		buf += n;
	}
	close(fd);
	return 1;
}

struct power_domain *pd_find_domain(const char *name)
{
	if (!name)
		return NULL;

	for (int i = 0; i < g_domain_count; i++) {
		if (!strcmp(g_domains[i].name, name))
			return &g_domains[i];
	}
	return NULL;
}

static int pd_in_blocklist(const char *name)
{
	if (!name)
		return 0;

	for (int i = 0; g_pd_blocklist[i] != NULL; i++) {
		if (!strcmp(name, g_pd_blocklist[i]))
			return 1;
	}
	return 0;
}

static struct power_domain *resolve_domain_tree(const char *dname)
{
	char path[512];
	char buf[256];
	struct power_domain *pd;
	struct power_domain *child;
	FILE *fp;
	int idx;

	if (!dname) {
		fprintf(stderr, "ERROR: Domain name passed to %s is NULL\n", __func__);
		return NULL;
	}

	if (pd_in_blocklist(dname))
		return NULL;

	pd = pd_find_domain(dname);
	if (pd)
		return pd;

	snprintf(path, sizeof(path), "%s/%s", DEBUGFS_PWR_CTRL, dname);
	if (!is_dir(path)) {
		fprintf(stderr, "ERROR: Missing debugfs node: %s\n", path);
		return NULL;
	}

	if (g_domain_count >= MAX_DOMAINS) {
		fprintf(stderr, "ERROR: Exceeded MAX_DOMAINS limit (%d)\n", MAX_DOMAINS);
		return NULL;
	}

	idx = g_domain_count++;
	pd = &g_domains[idx];
	memset(pd, 0, sizeof(*pd));
	snprintf(pd->name, sizeof(pd->name), "%s", dname);

	snprintf(path, sizeof(path), "%s/%s/mbfs-node-name", DT_PWR_CTRL, dname);
	read_file(path, pd->mbfs_name, sizeof(pd->mbfs_name));
	if (pd->mbfs_name[0] == '\0') {
		fprintf(stderr, "ERROR: Missing or empty mbfs-node-name for domain: %s\n", dname);
		g_domain_count = idx;
		memset(pd, 0, sizeof(*pd));
		return NULL;
	}

	snprintf(path, sizeof(path), "%s/%s/rpm-always-on", DT_PWR_CTRL, dname);
	if (file_exists(path))
		pd->is_always_on = 1;

	snprintf(path, sizeof(path), "%s/%s/sub_domains", DEBUGFS_PM_GENPD, dname);
	fp = fopen(path, "r");
	if (!fp)
		return pd;

	while (fgets(buf, sizeof(buf), fp)) {
		trim_str(buf);
		if (buf[0] == '\0' || pd_in_blocklist(buf))
			continue;

		snprintf(path, sizeof(path), "%s/%s", DEBUGFS_PWR_CTRL, buf);
		if (!is_dir(path))
			continue;

		if (pd->child_count >= MAX_CHILDREN) {
			fprintf(stderr, "ERROR: Exceeded MAX_CHILDREN limit (%d) on %s\n",
					MAX_CHILDREN, pd->name);
			fclose(fp);
			g_domain_count = idx;
			memset(pd, 0, sizeof(*pd));
			return NULL;
		}

		child = resolve_domain_tree(buf);
		if (!child) {
			fclose(fp);
			g_domain_count = idx;
			memset(pd, 0, sizeof(*pd));
			return NULL;
		}
		pd->children[pd->child_count++] = child;
	}
	fclose(fp);

	return pd;
}

int pd_control_init(const char *root_name)
{
	struct power_domain *root;

	if (!root_name) {
		fprintf(stderr, "ERROR: root_name passed to %s is NULL\n", __func__);
		return 0;
	}

	if (!is_dir(DEBUGFS_PWR_CTRL))
		mount("none", "/sys/kernel/debug", "debugfs", 0, NULL);

	if (!is_dir(DEBUGFS_PWR_CTRL)) {
		fprintf(stderr, "ERROR: Debugfs directory missing: %s\n", DEBUGFS_PWR_CTRL);
		return 0;
	}

	g_domain_count = 0;
	root = resolve_domain_tree(root_name);
	if (!root) {
		fprintf(stderr, "ERROR: Failed to resolve subtree for root: %s\n", root_name);
		return 0;
	}
	return 1;
}

static enum pd_state parse_state_str(const char *buf)
{
	if (!buf || buf[0] == '\0')
		return PD_STATE_ERROR;

	for (int i = 0; g_pd_state_strings[i] != NULL; i++) {
		if (!strcmp(buf, g_pd_state_strings[i]))
			return (enum pd_state)i;
	}
	return PD_STATE_ERROR;
}

enum pd_state pd_get_debugfs_state(const char *domain_name)
{
	char sfile[512];
	char buf[64];

	if (!domain_name)
		return PD_STATE_ERROR;

	snprintf(sfile, sizeof(sfile), "%s/%s/state", DEBUGFS_PWR_CTRL, domain_name);
	if (access(sfile, R_OK) != 0)
		return PD_STATE_ERROR;

	read_file(sfile, buf, sizeof(buf));
	return parse_state_str(buf);
}

enum pd_state pd_get_mbfs_state(const char *mbfs_name)
{
	char mpath[512];
	char buf[64];

	if (!mbfs_name || mbfs_name[0] == '\0')
		return PD_STATE_ERROR;

	snprintf(mpath, sizeof(mpath), "%s/%s", MBFS_CLIENT_BASE, mbfs_name);
	if (!file_exists(mpath) && is_dir(MBFS_ROOT))
		write_file(MBFS_CHERRY_PICK, "cpm/syspm");
	if (access(mpath, R_OK) != 0)
		return PD_STATE_ERROR;

	read_file(mpath, buf, sizeof(buf));
	return parse_state_str(buf);
}

int pd_set_debugfs_state(const char *domain_name, enum pd_state expected_state)
{
	const char *state_str;
	char sfile[512];

	if (!domain_name || expected_state < 0)
		return 0;

	state_str = g_pd_state_strings[expected_state];
	if (!state_str)
		return 0;

	if (pd_get_debugfs_state(domain_name) == expected_state)
		return 1;

	snprintf(sfile, sizeof(sfile), "%s/%s/state", DEBUGFS_PWR_CTRL, domain_name);
	return write_file(sfile, state_str);
}
