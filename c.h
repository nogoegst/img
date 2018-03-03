/*
 * Fundamental C definitions.
 */

#ifndef UTIL_LINUX_C_H
#define UTIL_LINUX_C_H

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_ERR_H
# include <err.h>
#endif

#ifndef HAVE_ERR_H
static inline void
errmsg(char doexit, int excode, char adderr, const char *fmt, ...)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);
	if (fmt != NULL) {
		va_list argp;
		va_start(argp, fmt);
		vfprintf(stderr, fmt, argp);
		va_end(argp);
		if (adderr)
			fprintf(stderr, ": ");
	}
	if (adderr)
		fprintf(stderr, "%m");
	fprintf(stderr, "\n");
	if (doexit)
		exit(excode);
}

#ifndef HAVE_ERR
# define err(E, FMT...) errmsg(1, E, 1, FMT)
#endif

#ifndef HAVE_ERRX
# define errx(E, FMT...) errmsg(1, E, 0, FMT)
#endif
#endif /* !HAVE_ERR_H */

#ifdef ENABLE_NLS
# include <libintl.h>
/*
 * For NLS support in the public shared libraries we have to specify text
 * domain name to be independent on the main program. For this purpose define
 * UL_TEXTDOMAIN_EXPLICIT before you include nls.h to your shared library code.
 */
# ifdef UL_TEXTDOMAIN_EXPLICIT
#  define _(Text) dgettext (UL_TEXTDOMAIN_EXPLICIT, Text)
# else
#  define _(Text) gettext (Text)
# endif
# ifdef gettext_noop
#  define N_(String) gettext_noop (String)
# else
#  define N_(String) (String)
# endif
# define P_(Singular, Plural, n) ngettext (Singular, Plural, n)
#else
# undef bindtextdomain
# define bindtextdomain(Domain, Directory) /* empty */
# undef textdomain
# define textdomain(Domain) /* empty */
# define _(Text) (Text)
# define N_(Text) (Text)
# define P_(Singular, Plural, n) ((n) == 1 ? (Singular) : (Plural))
#endif /* ENABLE_NLS */

#ifndef XALLOC_EXIT_CODE
# define XALLOC_EXIT_CODE EXIT_FAILURE
#endif

#define UL_BUILD_BUG_ON_ZERO(e) __extension__ (sizeof(struct { int:-!!(e); }))

#ifndef ARRAY_SIZE
# define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]) + __must_be_array(arr))
#endif

#ifndef PATH_MAX
# define PATH_MAX 4096
#endif

#ifdef __GNUC__

/* &a[0] degrades to a pointer: a different type from an array */
# define __must_be_array(a) \
	UL_BUILD_BUG_ON_ZERO(__builtin_types_compatible_p(__typeof__(a), __typeof__(&a[0])))

#else /* !__GNUC__ */
# define __must_be_array(a)	0
# define __attribute__(_arg_)
#endif /* !__GNUC__ */

/*
 * The usleep function was marked obsolete in POSIX.1-2001 and was removed
 * in POSIX.1-2008.  It was replaced with nanosleep() that provides more
 * advantages (like no interaction with signals and other timer functions).
 */

static inline int xusleep(useconds_t usec)
{
	struct timespec waittime = {
		.tv_sec   =  usec / 1000000L,
		.tv_nsec  = (usec % 1000000L) * 1000
	};
	return nanosleep(&waittime, NULL);
}

static inline int __attribute__ ((__format__(printf, 2, 3)))
xasprintf(char **strp, const char *fmt, ...)
{
	int ret;
	va_list args;
	va_start(args, fmt);
	ret = vasprintf(&(*strp), fmt, args);
	va_end(args);
	if (ret < 0)
		err(XALLOC_EXIT_CODE, "cannot allocate string");
	return ret;
}

static inline int write_all(int fd, const void *buf, size_t count)
{
	while (count) {
		ssize_t tmp;

		errno = 0;
		tmp = write(fd, buf, count);
		if (tmp > 0) {
			count -= tmp;
			if (count)
				buf = (void *) ((char *) buf + tmp);
		} else if (errno != EINTR && errno != EAGAIN)
			return -1;
		if (errno == EAGAIN)	/* Try later, *sigh* */
			xusleep(250000);
	}
	return 0;
}

static inline ssize_t read_all(int fd, char *buf, size_t count)
{
	ssize_t ret;
	ssize_t c = 0;
	int tries = 0;

	memset(buf, 0, count);
	while (count > 0) {
		ret = read(fd, buf, count);
		if (ret <= 0) {
			if (ret < 0 && (errno == EAGAIN || errno == EINTR) && (tries++ < 5)) {
				xusleep(250000);
				continue;
			}
			return c ? c : -1;
		}
		if (ret > 0)
			tries = 0;
		count -= ret;
		buf += ret;
		c += ret;
	}
	return c;
}

#endif /* UTIL_LINUX_C_H */
