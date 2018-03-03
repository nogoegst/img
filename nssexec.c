#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <inttypes.h>
#include <pwd.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>


/* Include some fundamental C functions. */
#include "c.h"

/* Get all of the CLONE_NEW* flags. */
#include "namespace.h"

#define GID 0
#define UID 1

#define _PATH_PROC_SETGROUPS	"/proc/self/setgroups"

/* synchronize parent and child by pipe */
#define PIPE_SYNC_BYTE	0x06

/* 'private' is kernel default */
#define UNSHARE_PROPAGATION_DEFAULT	(MS_REC | MS_PRIVATE)

#define getid(type) ((unsigned) ((type) == GID ? getgid() : getuid()))
#define idfile(type) ((type) == GID ? "gid_map" : "uid_map")
#define idtool(type) ((type) == GID ? "/usr/bin/newgidmap" : "/usr/bin/newuidmap")
#define subpath(type) ((type) == GID ? "/etc/subgid" : "/etc/subuid")
#define mappath(type) ((type) == GID ? "/proc/self/gid_map" : "/proc/self/uid_map")

char *append(char **destination, const char *format, ...) {
	char *extra, *result;
	va_list args;

	va_start(args, format);
	if (vasprintf(&extra, format, args) < 0)
		err(EXIT_FAILURE, _("asprintf"));
	va_end(args);

	if (*destination == NULL) {
		*destination = extra;
		return extra;
	}

	if (asprintf(&result, "%s%s", *destination, extra) < 0)
		err(EXIT_FAILURE, _("asprintf"));

	free(*destination);
	free(extra);
	*destination = result;
	return result;
}

void error(int status, int errnum, char *format, ...) {
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	if (errnum != 0)
		fprintf(stderr, ": %s\n", strerror(errnum));
	else
		fputc('\n', stderr);
	if (status != 0)
		exit(status);
}

char *string(const char *format, ...) {
	char *result;
	va_list args;

	va_start(args, format);
	if (vasprintf(&result, format, args) < 0)
		error(1, errno, "asprintf");
	va_end(args);
	return result;
}

/*enum {
  SETGROUPS_NONE = -1,
  SETGROUPS_DENY = 0,
  SETGROUPS_ALLOW = 1,
  };

  static const char *setgroups_strings[] =
  {
  [SETGROUPS_DENY] = "deny",
  [SETGROUPS_ALLOW] = "allow"
  };

  static void setgroups_control(int action)
  {
  const char *file = _PATH_PROC_SETGROUPS;
  const char *cmd;
  int fd;

  if (action < 0 || (size_t) action >= ARRAY_SIZE(setgroups_strings))
  return;
  cmd = setgroups_strings[action];

  fd = open(file, O_WRONLY);
  if (fd < 0) {
  if (errno == ENOENT)
  return;
  err(EXIT_FAILURE, _("cannot open %s"), file);
  }

  if (write_all(fd, cmd, strlen(cmd)))
  err(EXIT_FAILURE, _("write failed %s"), file);
  close(fd);
  }*/

static char *range_item(char *range, unsigned *start, unsigned *length) {
	ssize_t skip;

	while (range && *range && strchr(",;", *range))
		range++;
	if (range == NULL || *range == '\0')
		return NULL;
	if (sscanf(range, "%u:%u%zn", start, length, &skip) < 2)
		error(1, 0, "Invalid ID range '%s'", range);
	return range + skip;
}


static int try_mapping_tool(const char *app, int pid, char *range, char *id)
{
	int child;

	/*
	 * If @app is NULL, execve will segfault. Just check it here and bail (if
	 * we're in this path, the caller is already getting desparate and there
	 * isn't a backup to this failing). This usually would be a configuration
	 * or programming issue.
	 */
	if (!app)
		err(EXIT_FAILURE, _("mapping tool not present"));

	child = fork();
	if (child < 0)
	err(EXIT_FAILURE, _("failed to fork"));

	if (!child) {
#define MAX_ARGV 9
		char *argv[MAX_ARGV];
		char *envp[] = { NULL };
		char pid_fmt[16];
		int argc = 0;
		unsigned start, length;

		snprintf(pid_fmt, 16, "%d", pid);

		argv[argc++] = (char *)app;
		argv[argc++] = pid_fmt;
		argv[argc++] = "0";
		argv[argc++] = (char *)id;
		argv[argc++] = "1";
		argv[argc++] = "1";
	/*
	 * Convert the map string into a list of argument that
	 * newuidmap/newgidmap can understand.
	 */

		while ((range = range_item(range, &start, &length))) {
			char startstr[16];
			char lengthstr[16];
			sprintf(startstr, "%u", start);
			sprintf(lengthstr, "%u", length);
			argv[6] = startstr;
			argv[7] = lengthstr;
			argv[8]= (char*)0;
		}

		int i;
		for(i=0;i<MAX_ARGV;i++)
		{
			printf("%s ",argv[i]);
		}
		printf("\n");

		execve(app, argv, envp);
		fflush(stdout);
		fflush(stderr);
		err(EXIT_FAILURE, _("failed to execv"));
	} else {
		int status;

		while (1) {
			if (waitpid(child, &status, 0) < 0) {
				if (errno == EINTR)
					continue;
				err(EXIT_FAILURE, _("failed to waitpid"));
			}
			if (WIFEXITED(status) || WIFSIGNALED(status))
				return WEXITSTATUS(status);
		}
	}

	return -1;
}

static char *read_ranges(int type) {
	char *line = NULL, *entry, *range, *user;
	size_t end, size;
	//struct passwd *passwd;
	uid_t uid;
	unsigned int length, start;
	FILE *file;

	range = string("%u:1", getid(type));
	if (!(file = fopen(subpath(type), "r")))
		return range;

	uid = getuid();
	user = getenv("USER");
	user = user ? user : getlogin();

	while (getline(&line, &size, file) >= 0) {
		if (strtol(line, &entry, 10) != uid || entry == line) {
			if (strncmp(line, user, strlen(user)))
				continue;
			entry = line + strlen(user);
		}
		if (sscanf(entry, ":%u:%u%zn", &start, &length, &end) < 2)
			continue;
		if (strchr(":\n", entry[end + 1]))
			append(&range, ",%u:%u", start, length);
	}

	free(line);
	fclose(file);

	return range;
}

static void set_propagation(unsigned long flags)
{
	if (flags == 0)
		return;

	if (mount("none", "/", NULL, flags, NULL) != 0)
		err(EXIT_FAILURE, _("cannot change root filesystem propagation"));
}

void nsexec(void)
{
	pid_t pid = 0;
	unsigned long propagation = UNSHARE_PROPAGATION_DEFAULT;

	uid_t real_euid = geteuid();
	gid_t real_egid = getegid();

	char euid_fmt[16];
	char egid_fmt[16];
	snprintf(euid_fmt, 16, "%u",real_euid);
	snprintf(egid_fmt, 16, "%u",real_egid);

	char *uid_map;
	uid_map = read_ranges(UID);
	char *gid_map;
	gid_map = read_ranges(GID);

	if (unshare(CLONE_NEWNS | CLONE_NEWUSER) == -1){
		err(EXIT_FAILURE, _("unshare failed"));
	}

	set_propagation(propagation);

	pid = getpid();

	if (try_mapping_tool(idtool(UID), pid, uid_map, euid_fmt))
		err(EXIT_FAILURE, _("failed to use newuidmap"));

	if (try_mapping_tool(idtool(GID), pid, gid_map, egid_fmt))
		err(EXIT_FAILURE, _("failed to use newgidmap"));
}
