/*
 *   Razer daemon
 *   Daemon to keep track of Razer device state.
 *
 *   Copyright (C) 2008-2009 Michael Buesch <mb@bu3sch.de>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either version 2
 *   of the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#include "librazer.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <getopt.h>
#include <syslog.h>
#include <stdarg.h>

#ifdef __DragonFly__
#include <sys/endian.h>
#else
#include <byteswap.h>
#endif


typedef _Bool bool;
#undef min
#undef max
#undef offsetof
#define offsetof(type, member)	((size_t)&((type *)0)->member)
#define min(x, y)		({ __typeof__(x) __x = (x); \
				   __typeof__(y) __y = (y); \
				   __x < __y ? __x : __y; })
#define max(x, y)		({ __typeof__(x) __x = (x); \
				   __typeof__(y) __y = (y); \
				   __x > __y ? __x : __y; })

enum {
	LOGLEVEL_ERROR = 0,
	LOGLEVEL_WARNING,
	LOGLEVEL_INFO,
	LOGLEVEL_DEBUG,
};

struct commandline_args {
	bool background;
	const char *pidfile;
	int loglevel;
} cmdargs = {
#ifdef DEBUG
	.loglevel	= LOGLEVEL_DEBUG,
#else
	.loglevel	= LOGLEVEL_INFO,
#endif
};


#define VAR_RUN			"/var/run"
#define VAR_RUN_RAZERD		VAR_RUN "/razerd"
#define SOCKPATH		VAR_RUN_RAZERD "/socket"
#define PRIV_SOCKPATH		VAR_RUN_RAZERD "/socket.privileged"

#define INTERFACE_REVISION	1

#define COMMAND_MAX_SIZE	512
#define COMMAND_HDR_SIZE	sizeof(struct command_hdr)
#define BULK_CHUNK_SIZE		128

#define MAX_FIRMWARE_SIZE	0x400000

enum {
	COMMAND_ID_GETREV = 0,		/* Get the revision number of the socket interface. */
	COMMAND_ID_RESCANMICE,		/* Rescan mice. */
	COMMAND_ID_GETMICE,		/* Get a list of detected mice. */
	COMMAND_ID_GETFWVER,		/* Get the firmware rev of a mouse. */
	COMMAND_ID_SUPPFREQS,		/* Get a list of supported frequencies. */
	COMMAND_ID_SUPPRESOL,		/* Get a list of supported resolutions. */
	COMMAND_ID_SUPPDPIMAPPINGS,	/* Get a list of supported DPI mappings. */
	COMMAND_ID_CHANGEDPIMAPPING,	/* Modify a DPI mapping. */
	COMMAND_ID_GETDPIMAPPING,	/* Get the active DPI mapping for a profile. */
	COMMAND_ID_SETDPIMAPPING,	/* Set the active DPI mapping for a profile. */
	COMMAND_ID_GETLEDS,		/* Get a list of LEDs on the device. */
	COMMAND_ID_SETLED,		/* Set the state of a LED. */
	COMMAND_ID_GETFREQ,		/* Get the current frequency. */
	COMMAND_ID_SETFREQ,		/* Set the frequency. */
	COMMAND_ID_GETPROFILES,		/* Get a list of supported profiles. */
	COMMAND_ID_GETACTIVEPROF,	/* Get the active profile. */
	COMMAND_ID_SETACTIVEPROF,	/* Set the active profile. */
	COMMAND_ID_SUPPBUTTONS,		/* Get a list of physical buttons. */
	COMMAND_ID_SUPPBUTFUNCS,	/* Get a list of supported button functions. */
	COMMAND_ID_GETBUTFUNC,		/* Get the current function of a button. */
	COMMAND_ID_SETBUTFUNC,		/* Set the current function of a button. */

	/* Privileged commands */
	COMMAND_PRIV_FLASHFW = 128,	/* Upload and flash a firmware image */
	COMMAND_PRIV_CLAIM,		/* Claim the device. */
	COMMAND_PRIV_RELEASE,		/* Release the device. */
};

enum {
	ERR_NONE = 0,		/* No error */
	ERR_CMDSIZE,
	ERR_NOMEM,
	ERR_NOMOUSE,
	ERR_NOLED,
	ERR_CLAIM,
	ERR_FAIL,
	ERR_PAYLOAD,
};

struct command_hdr {
	uint8_t id;
} __attribute__((packed));

struct command {
	struct command_hdr hdr;
	char idstr[RAZER_IDSTR_MAX_SIZE];
	union {
		struct {
		} __attribute__((packed)) getfwver;

		struct {
		} __attribute__((packed)) suppfreqs;

		struct {
		} __attribute__((packed)) suppresol;

		struct {
		} __attribute__((packed)) suppdpimappings;

		struct {
			uint32_t id;
			uint32_t new_resolution;
		} __attribute__((packed)) changedpimapping;

		struct {
			uint32_t profile_id;
		} __attribute__((packed)) getdpimapping;

		struct {
			uint32_t profile_id;
			uint32_t mapping_id;
		} __attribute__((packed)) setdpimapping;

		struct {
		} __attribute__((packed)) getleds;

		struct {
			char led_name[RAZER_LEDNAME_MAX_SIZE];
			uint8_t new_state;
		} __attribute__((packed)) setled;

		struct {
			uint32_t profile_id;
			uint32_t new_frequency;
		} __attribute__((packed)) setfreq;

		struct {
		} __attribute__((packed)) getprofiles;

		struct {
		} __attribute__((packed)) getactiveprof;

		struct {
			uint32_t id;
		} __attribute__((packed)) setactiveprof;

		struct {
			uint32_t profile_id;
		} __attribute__((packed)) getfreq;

		struct {
		} __attribute__((packed)) suppbuttons;

		struct {
		} __attribute__((packed)) suppbutfuncs;

		struct {
			uint32_t profile_id;
			uint32_t button_id;
		} __attribute__((packed)) getbutfunc;

		struct {
			uint32_t profile_id;
			uint32_t button_id;
			uint32_t function_id;
		} __attribute__((packed)) setbutfunc;

		struct {
			uint32_t imagesize;
		} __attribute__((packed)) flashfw;

		struct {
		} __attribute__((packed)) claim;

		struct {
		} __attribute__((packed)) release;

	} __attribute__((packed));
} __attribute__((packed));

#define CMD_SIZE(name)	(offsetof(struct command, name) + \
			 sizeof(((struct command *)0)->name))

enum {
	REPLY_ID_U32 = 0,		/* An unsigned 32bit integer. */
	REPLY_ID_STR,			/* A string */

	/* Asynchonous notifications. */
	NOTIFY_ID_NEWMOUSE = 128,	/* New mouse was connected. */
	NOTIFY_ID_DELMOUSE,		/* A mouse was removed. */
};

struct reply_hdr {
	uint8_t id;
} __attribute__((packed));

struct reply {
	struct reply_hdr hdr;
	union {
		struct {
			uint32_t val;
		} __attribute__((packed)) u32;
		struct {
			uint16_t len;
			char str[0];
		} __attribute__((packed)) string;

		struct {
		} __attribute__((packed)) notify_newmouse;
		struct {
		} __attribute__((packed)) notify_delmouse;
	} __attribute__((packed));
} __attribute__((packed));

#define REPLY_SIZE(name)	(offsetof(struct reply, name) + \
				 sizeof(((struct reply *)0)->name))

struct client {
	struct client *next;
	struct sockaddr_un sockaddr;
	socklen_t socklen;
	int fd;
};

/* Control socket FDs. */
static int ctlsock = -1;
static int privsock = -1;
/* FD set we wait on in the main loop. */
static fd_set wait_fdset;
/* Linked list of connected clients. */
static struct client *clients;
static struct client *privileged_clients;
/* Linked list of detected mice. */
static struct razer_mouse *mice;


static void loginfo(const char *fmt, ...)
{
	va_list args;

	if (cmdargs.loglevel < LOGLEVEL_INFO)
		return;
	va_start(args, fmt);
	if (cmdargs.background)
		vsyslog(LOG_MAKEPRI(LOG_DAEMON, LOG_INFO), fmt, args);
	else
		vfprintf(stdout, fmt, args);
	va_end(args);
}

static void logerr(const char *fmt, ...)
{
	va_list args;

	if (cmdargs.loglevel < LOGLEVEL_ERROR)
		return;
	va_start(args, fmt);
	if (cmdargs.background)
		vsyslog(LOG_MAKEPRI(LOG_DAEMON, LOG_ERR), fmt, args);
	else
		vfprintf(stderr, fmt, args);
	va_end(args);
}

static void logdebug(const char *fmt, ...)
{
	va_list args;

	if (cmdargs.loglevel < LOGLEVEL_DEBUG)
		return;
	va_start(args, fmt);
	if (cmdargs.background) {
		vsyslog(LOG_MAKEPRI(LOG_DAEMON, LOG_DEBUG), fmt, args);
	} else {
		fprintf(stdout, "[razerd debug]: ");
		vfprintf(stdout, fmt, args);
	}
	va_end(args);
}

static void remove_pidfile(void)
{
	if (!cmdargs.pidfile)
		return;
	if (unlink(cmdargs.pidfile)) {
		logerr("Failed to remove PID-file %s: %s\n",
		       cmdargs.pidfile, strerror(errno));
	}
}

static int create_pidfile(void)
{
	char buf[32] = { 0, };
	pid_t pid = getpid();
	int fd;
	ssize_t res;

	if (!cmdargs.pidfile)
		return 0;

	fd = open(cmdargs.pidfile, O_RDWR | O_CREAT | O_TRUNC, 0444);
	if (fd < 0) {
		logerr("Failed to create PID-file %s: %s\n",
		       cmdargs.pidfile, strerror(errno));
		return -1;
	}
	snprintf(buf, sizeof(buf), "%lu",
		 (unsigned long)pid);
	res = write(fd, buf, strlen(buf));
	close(fd);
	if (res != strlen(buf)) {
		logerr("Failed to write PID-file %s: %s\n",
		       cmdargs.pidfile, strerror(errno));
		return -1;
	}

	return 0;
}

static void cleanup_var_run(void)
{
	unlink(SOCKPATH);
	close(ctlsock);
	ctlsock = -1;

	unlink(PRIV_SOCKPATH);
	close(privsock);
	privsock = -1;

	remove_pidfile();
	rmdir(VAR_RUN_RAZERD);
}

static int create_socket(const char *path, unsigned int perm,
			 unsigned int nrlisten)
{
	struct sockaddr_un sockaddr;
	int err, fd;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		logerr("Failed to create socket %s: %s\n",
		       path, strerror(errno));
		goto error;
	}
	err = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (err) {
		logerr("Failed to set O_NONBLOCK on socket %s: %s\n",
		       path, strerror(errno));
		goto error_close_sock;
	}
	sockaddr.sun_family = AF_UNIX;
	strncpy(sockaddr.sun_path, path, sizeof(sockaddr.sun_path) - 1);
	err = bind(fd, (struct sockaddr *)&sockaddr, SUN_LEN(&sockaddr));
	if (err) {
		logerr("Failed to bind socket to %s: %s\n",
		       path, strerror(errno));
		goto error_close_sock;
	}
	err = chmod(path, perm);
	if (err) {
		logerr("Failed to set %s socket permissions: %s\n",
		       path, strerror(errno));
		goto error_unlink_sock;
	}
	err = listen(fd, nrlisten);
	if (err) {
		logerr("Failed to listen on socket %s: %s\n",
		       path, strerror(errno));
		goto error_unlink_sock;
	}

	return fd;

error_unlink_sock:
	unlink(path);
error_close_sock:
	close(fd);
error:
	return -1;
}

static int setup_var_run(void)
{
	int err;

	/* Create /var/run subdirectory. */
	err = mkdir(VAR_RUN_RAZERD, 0755);
	if (err && errno != EEXIST) {
		logerr("Failed to create directory %s: %s\n",
		       VAR_RUN_RAZERD, strerror(errno));
		return err;
	}

	create_pidfile();

	/* Create the control socket. */
	ctlsock = create_socket(SOCKPATH, 0666, 25);
	if (ctlsock == -1)
		goto err_remove_pidfile;

	/* Create the socket for privileged operations. */
	privsock = create_socket(PRIV_SOCKPATH, 0660, 15);
	if (privsock == -1)
		goto err_remove_ctlsock;

	return 0;

err_remove_ctlsock:
	unlink(SOCKPATH);
	close(ctlsock);
	ctlsock = -1;

err_remove_pidfile:
	remove_pidfile();
	rmdir(VAR_RUN_RAZERD);

	return -1;
}

static int setup_environment(void)
{
	int err;

	err = razer_init();
	if (err) {
		logerr("librazer initialization failed. (%d)\n", err);
		return err;
	}
	err = setup_var_run();
	if (err) {
		razer_exit();
		return err;
	}

	return 0;
}

static void cleanup_environment(void)
{
	cleanup_var_run();
	razer_exit();
	remove_pidfile();
}

static void signal_handler(int signum)
{
	switch (signum) {
	case SIGINT:
	case SIGTERM:
		loginfo("Terminating razerd.\n");
		cleanup_environment();
		exit(0);
		break;
	case SIGPIPE:
		logerr("Broken pipe.\n");
		break;
	default:
		logerr("Received unknown signal %d\n", signum);
	}
}

static void setup_sighandler(void)
{
	struct sigaction act;

	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = signal_handler;

	sigaction(SIGINT, &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGPIPE, &act, NULL);
}

static void free_client(struct client *client)
{
	free(client);
}

static struct client * new_client(const struct sockaddr_un *sockaddr,
				  socklen_t socklen, int fd)
{
	struct client *client;

	client = malloc(sizeof(*client));
	if (!client)
		return NULL;
	memset(client, 0, sizeof(*client));

	memcpy(&client->sockaddr, sockaddr, sizeof(client->sockaddr));
	client->socklen = socklen;
	client->fd = fd;

	return client;
}

static void client_list_add(struct client **base, struct client *new_entry)
{
	struct client *i;

	new_entry->next = NULL;
	if (!(*base)) {
		*base = new_entry;
		return;
	}
	for (i = *base; i->next; i = i->next)
		;
	i->next = new_entry;
}

static void client_list_del(struct client **base, struct client *del_entry)
{
	struct client *i;

	if (del_entry == *base) {
		*base = (*base)->next;
		return;
	}
	for (i = *base; i && (i->next != del_entry); i = i->next)
		;
	if (i)
		i->next = del_entry->next;
}

static void check_control_socket(int socket_fd, struct client **client_list)
{
	socklen_t socklen;
	struct client *client;
	struct sockaddr_un remoteaddr;
	int err, fd;

	socklen = sizeof(remoteaddr);
	fd = accept(socket_fd, (struct sockaddr *)&remoteaddr, &socklen);
	if (fd == -1)
		return;
	/* Connected */
	err = fcntl(fd, F_SETFL, O_NONBLOCK);
	if (err) {
		logerr("Failed to set O_NONBLOCK on client: %s\n",
		       strerror(errno));
		close(fd);
		return;
	}
	client = new_client(&remoteaddr, socklen, fd);
	if (!client) {
		close(fd);
		return;
	}
	client_list_add(client_list, client);
	if (client_list == &privileged_clients)
		logdebug("Privileged client connected (fd=%d)\n", fd);
	else
		logdebug("Client connected (fd=%d)\n", fd);
}

static void disconnect_client(struct client **client_list, struct client *client)
{
	client_list_del(client_list, client);
	if (client_list == &privileged_clients)
		logdebug("Privileged client disconnected (fd=%d)\n", client->fd);
	else
		logdebug("Client disconnected (fd=%d)\n", client->fd);
	free_client(client);
}

static inline uint32_t cpu_to_be32(uint32_t v)
{
#ifdef BIG_ENDIAN_HOST
	return v;
#else
	return bswap_32(v);
#endif
}


static inline uint16_t cpu_to_be16(uint16_t v)
{
#ifdef BIG_ENDIAN_HOST
	return v;
#else
	return bswap_16(v);
#endif
}

static inline uint32_t be32_to_cpu(uint32_t v)
{
	return cpu_to_be32(v);
}

static inline uint16_t be16_to_cpu(uint16_t v)
{
	return cpu_to_be16(v);
}

static int send_reply(struct client *client, struct reply *r, size_t len)
{
	return send(client->fd, r, len, 0);
}

static int send_u32(struct client *client, uint32_t v)
{
	struct reply r;

	r.hdr.id = REPLY_ID_U32;
	r.u32.val = cpu_to_be32(v);

	return send_reply(client, &r, REPLY_SIZE(u32));
}

static int send_string(struct client *client, const char *str)
{
	char buf[2048];
	struct reply *r = (struct reply *)buf;
	size_t len;

	r->hdr.id = REPLY_ID_STR;
	len = min(sizeof(buf) - REPLY_SIZE(string) - 1,
		  strlen(str));
	r->string.len = cpu_to_be16(len);
	strncpy(r->string.str, str, len);

	return send_reply(client, r, REPLY_SIZE(string) + len);
}

static int recv_bulk(struct client *client, char *buf, unsigned int len)
{
	unsigned int next_len, i;
	int nr;

//FIXME timeout
	for (i = 0; i < len; i += BULK_CHUNK_SIZE) {
		next_len = BULK_CHUNK_SIZE;
		if (i + next_len > len)
			next_len = len - i;
		while (1) {
			nr = recv(client->fd, buf + i, next_len, 0);
			if (nr < 0) {
				if (errno == EAGAIN)
					continue;
			}
			if (nr > 0)
				break;
		}
		if (nr != next_len) {
			send_u32(client, ERR_PAYLOAD);
			return -1;
		}
		send_u32(client, ERR_NONE);
	}

	return 0;
}

static struct razer_mouse_profile * find_mouse_profile(struct razer_mouse *mouse,
						       unsigned int profile_id)
{
	struct razer_mouse_profile *list;
	unsigned int i;

	if (!mouse->get_profiles)
		return NULL;
	list = mouse->get_profiles(mouse);
	if (!list)
		return NULL;
	for (i = 0; i < mouse->nr_profiles; i++) {
		if (list[i].nr == profile_id)
			return &list[i];
	}

	return NULL;
}

static struct razer_mouse_dpimapping * find_mouse_dpimapping(struct razer_mouse *mouse,
							     unsigned int mapping_id)
{
	struct razer_mouse_dpimapping *list;
	int i, count;

	if (!mouse->supported_dpimappings)
		return NULL;
	count = mouse->supported_dpimappings(mouse, &list);
	if (count <= 0)
		return NULL;
	for (i = 0; i < count; i++) {
		if (list[i].nr == mapping_id)
			return &list[i];
	}

	return NULL;
}

static struct razer_button * find_mouse_button(struct razer_mouse *mouse,
					       unsigned int button_id)
{
	struct razer_button *list;
	int i, count;

	if (!mouse->supported_buttons)
		return NULL;
	count = mouse->supported_buttons(mouse, &list);
	if (count <= 0)
		return NULL;
	for (i = 0; i < count; i++) {
		if (list[i].id == button_id)
			return &list[i];
	}

	return NULL;
}

static struct razer_button_function * find_mouse_button_function(struct razer_mouse *mouse,
								 unsigned int button_id)
{
	struct razer_button_function *list;
	int i, count;

	if (!mouse->supported_button_functions)
		return NULL;
	count = mouse->supported_button_functions(mouse, &list);
	if (count <= 0)
		return NULL;
	for (i = 0; i < count; i++) {
		if (list[i].id == button_id)
			return &list[i];
	}

	return NULL;
}

static void command_getmice(struct client *client, const struct command *cmd, unsigned int len)
{
	unsigned int count;
	char str[RAZER_IDSTR_MAX_SIZE + 1];
	struct razer_mouse *mouse;

	count = 0;
	razer_for_each_mouse(mouse, mice)
		count++;
	send_u32(client, count);
	razer_for_each_mouse(mouse, mice) {
		snprintf(str, sizeof(str), "%s", mouse->idstr);
		send_string(client, str);
	}
}

static void command_getfwver(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	uint32_t fwver = 0xFFFFFFFF;
	int err;

	if (len < CMD_SIZE(getfwver))
		goto out;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->get_fw_version)
		goto out;
	err = mouse->claim(mouse);
	if (err)
		goto out;
	fwver = mouse->get_fw_version(mouse);
	mouse->release(mouse);
out:
	send_u32(client, fwver);
}

static void command_getfreq(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	enum razer_mouse_freq freq;

	if (len < CMD_SIZE(getfreq))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse)
		goto error;
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->getfreq.profile_id));
	if (!profile || !profile->get_freq)
		goto error;
	freq = profile->get_freq(profile);

	send_u32(client, freq);

	return;
error:
	send_u32(client, RAZER_MOUSE_FREQ_UNKNOWN);
}

static void command_suppfreqs(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	enum razer_mouse_freq *freq_list;
	int i, count;

	if (len < CMD_SIZE(suppfreqs))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->supported_freqs)
		goto error;
	count = mouse->supported_freqs(mouse, &freq_list);
	if (count <= 0)
		goto error;

	send_u32(client, count);
	for (i = 0; i < count; i++)
		send_u32(client, freq_list[i]);
	razer_free_freq_list(freq_list, count);

	return;
error:
	count = 0;
	send_u32(client, count);
}

static void command_suppresol(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	enum razer_mouse_res *res_list;
	int i, count;

	if (len < CMD_SIZE(suppresol))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->supported_resolutions)
		goto error;
	count = mouse->supported_resolutions(mouse, &res_list);
	if (count <= 0)
		goto error;

	send_u32(client, count);
	for (i = 0; i < count; i++)
		send_u32(client, res_list[i]);
	razer_free_resolution_list(res_list, count);

	return;
error:
	count = 0;
	send_u32(client, count);
}

static void command_suppdpimappings(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_dpimapping *list;
	int i, count;

	if (len < CMD_SIZE(suppdpimappings))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->supported_dpimappings)
		goto error;
	count = mouse->supported_dpimappings(mouse, &list);
	if (count <= 0)
		goto error;

	send_u32(client, count);
	for (i = 0; i < count; i++) {
		send_u32(client, list[i].nr);
		send_u32(client, list[i].res);
		send_u32(client, list[i].change ? 1 : 0);
	}
	/* No need to free list. It's statically allocated. */

	return;
error:
	count = 0;
	send_u32(client, count);
}

static void command_changedpimapping(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_dpimapping *mapping;
	int err;
	uint32_t errorcode = ERR_NONE;

	if (len < CMD_SIZE(changedpimapping)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	mapping = find_mouse_dpimapping(mouse, be32_to_cpu(cmd->changedpimapping.id));
	if (!mapping || !mapping->change) {
		errorcode = ERR_FAIL;
		goto error;
	}

	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = mapping->change(mapping,
			      be32_to_cpu(cmd->changedpimapping.new_resolution));
	if (err)
		errorcode = ERR_FAIL;
	mouse->release(mouse);

error:
	send_u32(client, errorcode);
}

static void command_getdpimapping(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	struct razer_mouse_dpimapping *mapping;

	if (len < CMD_SIZE(getdpimapping))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse)
		goto error;
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->getdpimapping.profile_id));
	if (!profile)
		goto error;
	mapping = profile->get_dpimapping(profile);
	if (!mapping)
		goto error;

	send_u32(client, mapping->nr);

	return;
error:
	send_u32(client, 0xFFFFFFFF);
}

static void command_setdpimapping(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	struct razer_mouse_dpimapping *mapping;
	uint32_t errorcode = ERR_NONE;
	int err;

	if (len < CMD_SIZE(setdpimapping)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->setdpimapping.profile_id));
	if (!profile || !profile->set_dpimapping) {
		errorcode = ERR_FAIL;
		goto error;
	}
	mapping = find_mouse_dpimapping(mouse, be32_to_cpu(cmd->setdpimapping.mapping_id));
	if (!mapping) {
		errorcode = ERR_FAIL;
		goto error;
	}

	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = profile->set_dpimapping(profile, mapping);
	mouse->release(mouse);
	if (err) {
		errorcode = ERR_FAIL;
		goto error;
	}

error:
	send_u32(client, errorcode);
}

static void command_rescanmice(struct client *client, const struct command *cmd, unsigned int len)
{
	mice = razer_rescan_mice();
}

static void command_getleds(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_led *leds_list, *led;
	int count;

	if (len < CMD_SIZE(getleds))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->get_leds)
		goto error;
	count = mouse->get_leds(mouse, &leds_list);
	if (count <= 0)
		goto error;

	send_u32(client, count);
	for (led = leds_list; led; led = led->next) {
		send_string(client, led->name);
		send_u32(client, led->state);
	}
	razer_free_leds(leds_list);

	return;
error:
	count = 0;
	send_u32(client, count);
}

static struct razer_led * razer_mouse_find_led(struct razer_led *leds_list,
					       const char *led_name)
{
	struct razer_led *led;

	for (led = leds_list; led; led = led->next) {
		if (strncmp(led->name, led_name, RAZER_LEDNAME_MAX_SIZE) == 0)
			return led;
	}

	return NULL;
}

static void command_setled(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_led *leds_list, *led;
	int err, count;
	uint32_t errorcode = ERR_NONE;

	if (len < CMD_SIZE(setled)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	count = mouse->get_leds(mouse, &leds_list);
	if (count <= 0) {
		errorcode = ERR_NOMEM;
		goto error;
	}
	led = razer_mouse_find_led(leds_list, cmd->setled.led_name);
	if (!led) {
		errorcode = ERR_NOLED;
		goto error;
	}
	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = led->toggle_state(led, cmd->setled.new_state ? RAZER_LED_ON : RAZER_LED_OFF);
	mouse->release(mouse);
	if (err) {
		errorcode = ERR_FAIL;
		goto error;
	}

error:
	send_u32(client, errorcode);
}

static void command_setfreq(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	int err;
	uint32_t errorcode = ERR_NONE;

	if (len < CMD_SIZE(setfreq)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->setfreq.profile_id));
	if (!profile || !profile->set_freq) {
		errorcode = ERR_FAIL;
		goto error;
	}

	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = profile->set_freq(profile, be32_to_cpu(cmd->setfreq.new_frequency));
	mouse->release(mouse);
	if (err) {
		errorcode = ERR_FAIL;
		goto error;
	}

error:
	send_u32(client, errorcode);
}

static void command_getprofiles(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *list;
	unsigned int i;

	if (len < CMD_SIZE(getprofiles))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse)
		goto error;
	list = mouse->get_profiles(mouse);
	if (!list)
		goto error;

	send_u32(client, mouse->nr_profiles);
	for (i = 0; i < mouse->nr_profiles; i++)
		send_u32(client, list[i].nr);

	return;
error:
	send_u32(client, 0);
}

static void command_getactiveprof(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *activeprof;

	if (len < CMD_SIZE(getactiveprof))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse)
		goto error;
	activeprof = mouse->get_active_profile(mouse);
	if (!activeprof)
		goto error;

	send_u32(client, activeprof->nr);

	return;
error:
	send_u32(client, 0xFFFFFFFF);
}

static void command_setactiveprof(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	int err;
	uint32_t errorcode = ERR_NONE;

	if (len < CMD_SIZE(setactiveprof)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->setactiveprof.id));
	if (!profile) {
		errorcode = ERR_FAIL;
		goto error;
	}

	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = mouse->set_active_profile(mouse, profile);
	if (err)
		errorcode = ERR_FAIL;
	mouse->release(mouse);

error:
	send_u32(client, errorcode);
}

static void command_suppbuttons(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_button *list;
	int count, i;

	if (len < CMD_SIZE(suppbuttons))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->supported_buttons)
		goto error;
	count = mouse->supported_buttons(mouse, &list);
	if (count <= 0)
		goto error;

	send_u32(client, count);
	for (i = 0; i < count; i++) {
		send_u32(client, list[i].id);
		send_string(client, list[i].name);
	}

	return;
error:
	send_u32(client, 0);
}

static void command_suppbutfuncs(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_button_function *list;
	int count, i;

	if (len < CMD_SIZE(suppbutfuncs))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse || !mouse->supported_button_functions)
		goto error;
	count = mouse->supported_button_functions(mouse, &list);
	if (count <= 0)
		goto error;

	send_u32(client, count);
	for (i = 0; i < count; i++) {
		send_u32(client, list[i].id);
		send_string(client, list[i].name);
	}

	return;
error:
	send_u32(client, 0);

}

static void command_getbutfunc(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	struct razer_button_function *func;
	struct razer_button *button;

	if (len < CMD_SIZE(getbutfunc))
		goto error;
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse)
		goto error;
	button = find_mouse_button(mouse, be32_to_cpu(cmd->getbutfunc.button_id));
	if (!button)
		goto error;
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->getbutfunc.profile_id));
	if (!profile || !profile->get_button_function)
		goto error;
	func = profile->get_button_function(profile, button);
	if (!func)
		goto error;

	send_u32(client, func->id);
	send_string(client, func->name);

	return;
error:
	send_u32(client, 0);
	send_string(client, "");
}

static void command_setbutfunc(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	struct razer_mouse_profile *profile;
	struct razer_button_function *func;
	struct razer_button *button;
	uint32_t errorcode = ERR_NONE;
	int err;

	if (len < CMD_SIZE(setbutfunc)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	button = find_mouse_button(mouse, be32_to_cpu(cmd->setbutfunc.button_id));
	if (!button) {
		errorcode = ERR_FAIL;
		goto error;
	}
	func = find_mouse_button_function(mouse, be32_to_cpu(cmd->setbutfunc.function_id));
	if (!func) {
		errorcode = ERR_FAIL;
		goto error;
	}
	profile = find_mouse_profile(mouse, be32_to_cpu(cmd->setbutfunc.profile_id));
	if (!profile || !profile->set_button_function) {
		errorcode = ERR_FAIL;
		goto error;
	}

	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = profile->set_button_function(profile, button, func);
	mouse->release(mouse);
	if (err) {
		errorcode = ERR_FAIL;
		goto error;
	}
error:
	send_u32(client, errorcode);
}

static void command_flashfw(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	uint32_t image_size;
	int err;
	uint32_t errorcode = ERR_NONE;
	char *image = NULL;

	if (len < CMD_SIZE(flashfw)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	image_size = be32_to_cpu(cmd->flashfw.imagesize);
	if (image_size > MAX_FIRMWARE_SIZE) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}

	image = malloc(image_size);
	if (!image) {
		errorcode = ERR_NOMEM;
		goto error;
	}

	err = recv_bulk(client, image, image_size);
	if (err) {
		errorcode = ERR_PAYLOAD;
		goto error;
	}

	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_CLAIM;
		goto error;
	}
	err = mouse->flash_firmware(mouse, image, image_size,
				    RAZER_FW_FLASH_MAGIC);
	mouse->release(mouse);
	if (err) {
		errorcode = ERR_FAIL;
		goto error;
	}

error:
	send_u32(client, errorcode);
	free(image);
}

static void command_claim(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	uint32_t errorcode = ERR_NONE;
	int err;

	if (len < CMD_SIZE(claim)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	err = mouse->claim(mouse);
	if (err) {
		errorcode = ERR_FAIL;
		goto error;
	}

error:
	send_u32(client, errorcode);
}

static void command_release(struct client *client, const struct command *cmd, unsigned int len)
{
	struct razer_mouse *mouse;
	uint32_t errorcode = ERR_NONE;

	if (len < CMD_SIZE(release)) {
		errorcode = ERR_CMDSIZE;
		goto error;
	}
	mouse = razer_mouse_list_find(mice, cmd->idstr);
	if (!mouse) {
		errorcode = ERR_NOMOUSE;
		goto error;
	}
	mouse->release(mouse);

error:
	send_u32(client, errorcode);
}

static void handle_received_command(struct client *client, const char *_cmd, unsigned int len)
{
	const struct command *cmd = (const struct command *)_cmd;

	if (len < COMMAND_HDR_SIZE)
		return;
	switch (cmd->hdr.id) {
	case COMMAND_ID_GETREV:
		send_u32(client, INTERFACE_REVISION);
		break;
	case COMMAND_ID_RESCANMICE:
		command_rescanmice(client, cmd, len);
		break;
	case COMMAND_ID_GETMICE:
		command_getmice(client, cmd, len);
		break;
	case COMMAND_ID_GETFWVER:
		command_getfwver(client, cmd, len);
		break;
	case COMMAND_ID_SUPPFREQS:
		command_suppfreqs(client, cmd, len);
		break;
	case COMMAND_ID_SUPPRESOL:
		command_suppresol(client, cmd, len);
		break;
	case COMMAND_ID_SUPPDPIMAPPINGS:
		command_suppdpimappings(client, cmd, len);
		break;
	case COMMAND_ID_CHANGEDPIMAPPING:
		command_changedpimapping(client, cmd, len);
		break;
	case COMMAND_ID_GETDPIMAPPING:
		command_getdpimapping(client, cmd, len);
		break;
	case COMMAND_ID_SETDPIMAPPING:
		command_setdpimapping(client, cmd, len);
		break;
	case COMMAND_ID_GETLEDS:
		command_getleds(client, cmd, len);
		break;
	case COMMAND_ID_SETLED:
		command_setled(client, cmd, len);
		break;
	case COMMAND_ID_SETFREQ:
		command_setfreq(client, cmd, len);
		break;
	case COMMAND_ID_GETPROFILES:
		command_getprofiles(client, cmd, len);
		break;
	case COMMAND_ID_GETACTIVEPROF:
		command_getactiveprof(client, cmd, len);
		break;
	case COMMAND_ID_SETACTIVEPROF:
		command_setactiveprof(client, cmd, len);
		break;
	case COMMAND_ID_GETFREQ:
		command_getfreq(client, cmd, len);
		break;
	case COMMAND_ID_SUPPBUTTONS:
		command_suppbuttons(client, cmd, len);
		break;
	case COMMAND_ID_SUPPBUTFUNCS:
		command_suppbutfuncs(client, cmd, len);
		break;
	case COMMAND_ID_GETBUTFUNC:
		command_getbutfunc(client, cmd, len);
		break;
	case COMMAND_ID_SETBUTFUNC:
		command_setbutfunc(client, cmd, len);
		break;
	default:
		/* Unknown command. */
		break;
	}
}

static void handle_received_privileged_command(struct client *client,
					       const char *_cmd, unsigned int len)
{
	const struct command *cmd = (const struct command *)_cmd;

	if (len < COMMAND_HDR_SIZE)
		return;
	switch (cmd->hdr.id) {
	case COMMAND_PRIV_FLASHFW:
		command_flashfw(client, cmd, len);
		break;
	case COMMAND_PRIV_CLAIM:
		command_claim(client, cmd, len);
		break;
	case COMMAND_PRIV_RELEASE:
		command_release(client, cmd, len);
		break;
	default:
		/* Unknown command. */
		break;
	}
}

static void check_client_connections(void)
{
	char command[COMMAND_MAX_SIZE + 1] = { 0, };
	int nr;
	struct client *client, *next;

	for (client = clients; client; ) {
		next = client->next;
		nr = recv(client->fd, command, COMMAND_MAX_SIZE, 0);
		if (nr < 0)
			goto next_client;
		if (nr == 0) {
			disconnect_client(&clients, client);
			goto next_client;
		}
		handle_received_command(client, command, nr);
  next_client:
		client = next;
	}
}

static void check_privileged_connections(void)
{
	char command[COMMAND_MAX_SIZE + 1] = { 0, };
	int nr;
	struct client *client, *next;

	for (client = privileged_clients; client; ) {
		next = client->next;
		nr = recv(client->fd, command, COMMAND_MAX_SIZE, 0);
		if (nr < 0)
			goto next_client;
		if (nr == 0) {
			disconnect_client(&privileged_clients, client);
			goto next_client;
		}
		handle_received_privileged_command(client, command, nr);
  next_client:
		client = next;
	}
}

static void broadcast_notification(unsigned int notifyId, size_t size)
{
	struct reply r;
	struct client *client;

	for (client = clients; client; client = client->next) {
		r.hdr.id = notifyId;
		send_reply(client, &r, size);
	}
}

static void event_handler(enum razer_event event,
			  const struct razer_event_data *data)
{
	switch (event) {
	case RAZER_EV_MOUSE_ADD:
		broadcast_notification(NOTIFY_ID_NEWMOUSE,
				       REPLY_SIZE(notify_newmouse));
		break;
	case RAZER_EV_MOUSE_REMOVE:
		broadcast_notification(NOTIFY_ID_DELMOUSE,
				       REPLY_SIZE(notify_delmouse));
		break;
	}
}

static int mainloop(void)
{
	struct client *client;
	int err;

	loginfo("Razer device service daemon\n");

	setup_sighandler();
	err = setup_environment();
	if (err)
		return 1;
	err = razer_register_event_handler(event_handler);
	if (err) {
		logerr("Failed to register event handler\n");
		cleanup_environment();
		return 1;
	}

	mice = razer_rescan_mice();

	while (1) {
		FD_ZERO(&wait_fdset);
		FD_SET(privsock, &wait_fdset);
		FD_SET(ctlsock, &wait_fdset);
		for (client = clients; client; client = client->next)
			FD_SET(client->fd, &wait_fdset);
		for (client = privileged_clients; client; client = client->next)
			FD_SET(client->fd, &wait_fdset);
		select(FD_SETSIZE, &wait_fdset, NULL, NULL, NULL);

		check_control_socket(privsock, &privileged_clients);
		check_privileged_connections();

		check_control_socket(ctlsock, &clients);
		check_client_connections();
	}

	return 1;
}

static void banner(FILE *fd)
{
	fprintf(fd, "Razer device service daemon\n");
	fprintf(fd, "Copyright 2009 Michael Buesch <mb@bu3sch.de>\n");
}

static void usage(FILE *fd, int argc, char **argv)
{
	banner(fd);
	fprintf(fd, "\nUsage: %s [OPTIONS]\n", argv[0]);
	fprintf(fd, "\n");
	fprintf(fd, "\n");
	fprintf(fd, "  -B|--background           Fork into the background (daemon mode)\n");
	fprintf(fd, "  -P|--pidfile PATH         Create a PID-file\n");
	fprintf(fd, "  -l|--loglevel LEVEL       Set the loglevel\n");
	fprintf(fd, "                            0=error, 1=warning, 2=info(default), 3=debug\n");
	fprintf(fd, "\n");
	fprintf(fd, "  -h|--help                 Print this help text\n");
	fprintf(fd, "  -v|--version              Print the version number\n");
}

static int parse_args(int argc, char **argv)
{
	static struct option long_options[] = {
		{ "help", no_argument, 0, 'h', },
		{ "version", no_argument, 0, 'v', },
		{ "background", no_argument, 0, 'B', },
		{ "pidfile", required_argument, 0, 'P', },
		{ "loglevel", required_argument, 0, 'l', },
		{ 0, },
	};

	int c, idx;

	while (1) {
		c = getopt_long(argc, argv, "hvBP:l:",
				long_options, &idx);
		if (c == -1)
			break;
		switch (c) {
		case 'h':
			usage(stdout, argc, argv);
			return 1;
		case 'v':
			banner(stdout);
			return 1;
		case 'B':
			cmdargs.background = 1;
			break;
		case 'P':
			cmdargs.pidfile = optarg;
			break;
		case 'l':
			if (sscanf(optarg, "%d", &cmdargs.loglevel) != 1) {
				fprintf(stderr, "Failed to parse --loglevel argument\n");
				return -1;
			}
			break;
		default:
			return -1;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	int err;
	pid_t pid;

	err = parse_args(argc, argv);
	if (err > 0)
		return 0;
	if (err)
		return err;

	if (cmdargs.background) {
		pid = fork();
		if (pid == 0) {
			/* Child process */
			return mainloop();
		}
		if (pid > 0) {
			/* Parent process */
			logdebug("Forked into background (pid=%lu)\n",
				 (unsigned long)pid);
			return 0;
		}
		logerr("Failed to fork into the background: %s\n",
		       strerror(errno));
		return 1;
	}

	return mainloop();
}
