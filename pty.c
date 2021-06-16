#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <limits.h>
#include <pwd.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

#if defined(__linux)
#include <pty.h>
#endif

#include "utils.h"
#include "term.h"

#define WRITE_LIMIT 1024

struct IOBuf {
	char data[BUFSIZ];
	int len;
};

static struct IOBuf iobuf = { 0 };

void
pty_test(void)
{
	fprintf(stderr, "BUFSZ = %d\n", BUFSIZ);
}

int
pty_init(char *shell)
{
	char *cmd = (shell) ? shell : "/bin/dash";
	char *args[] = { NULL };

	if (openpty(&pty.mfd, &pty.sfd, NULL, NULL, NULL) < 0) {
		error_fatal("openpty()", 1);
	}

	switch ((pty.pid = fork())) {
	case -1: // fork failed
		error_fatal("fork()", 1);
		break;
	case 0: // child process
		setsid();

		dup2(pty.sfd, 0);
		dup2(pty.sfd, 1);
		dup2(pty.sfd, 2);

		if (ioctl(pty.sfd, TIOCSCTTY, NULL) < 0) {
			error_fatal("ioctl()", 1);
		}
		close(pty.sfd);
		close(pty.mfd);
		{
			// execute shell
			/* setenv("TERM", "dumb", 1); */
			setenv("SHELL", "/bin/dash", 1);

			signal(SIGCHLD, SIG_DFL);
			signal(SIGHUP,  SIG_DFL);
			signal(SIGINT,  SIG_DFL);
			signal(SIGQUIT, SIG_DFL);
			signal(SIGTERM, SIG_DFL);
			signal(SIGALRM, SIG_DFL);

			execvp(cmd, args);

			_exit(1);
		}
		break;
	default: // parent process
		close(pty.sfd);
		signal(SIGCHLD, SIG_DFL);
		break;
	}

	return pty.mfd;
}

size_t
pty__test_write(const char *str, size_t len)
{
	size_t i;

	for (i = 0; i < len; i++) {
		fputc(str[i], stderr);
	}

	return i;
}

size_t
pty_read(void)
{
	int nr, nw;

	nr = read(pty.mfd, &iobuf.data[iobuf.len], ARRLEN(iobuf.data) - iobuf.len);

	switch (nr) {
	case  0:
		exit(0);
		break;
	case -1:
		error_fatal("pty_read()", 1);
		break;
	default:
		/* for (int i = 0; i < nr; ++i) { */
		/* 	printf("\n--- [%d] = %s\n", iobuf.data[i], asciistr(iobuf.data[i])); */
		/* } */
		/* nw = pty__test_write(iobuf.data, iobuf.len + nr); */
		nw = tty_write(iobuf.data, iobuf.len + nr);
		iobuf.len += nr - nw;
		if (iobuf.len) {
			memmove(&iobuf.data[0], &iobuf.data[nw], iobuf.len);
		}
		break;
	}

	return nr;
}

size_t
pty_write(const char *str, size_t len)
{
	fd_set fds_r, fds_w;
	size_t i = 0, rem = WRITE_LIMIT;
	ssize_t n; // write() return value

	while (i < len) {
		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_SET(pty.mfd, &fds_r);
		FD_SET(pty.mfd, &fds_w);

		if (pselect(pty.mfd + 1, &fds_r, &fds_w, NULL, NULL, NULL) < 0) {
			error_fatal("pselect()", 1);
		}
		if (FD_ISSET(pty.mfd, &fds_w)) {
			n = write(pty.mfd, &str[i], MIN(len-i, rem));
			if (n < 0) {
				error_fatal("write()", 1);
			}
			if (i + n < len) {
				if (len - i < rem) {
					rem = pty_read();
				}
			}
			i += n;
		}
		if (i < len && FD_ISSET(pty.mfd, &fds_r)) {
			rem = pty_read();
		}
	}

	return i;
}

void
pty_resize(int w, int h, int cols, int rows)
{
	struct winsize ptywin = {
		.ws_xpixel = w,
		.ws_ypixel = h,
		.ws_col = cols,
		.ws_row = cols,
	};

	if (ioctl(pty.mfd, TIOCSWINSZ, &ptywin) < 0) {
		fprintf(stderr, "ERROR: pty_resize() - ioctl() failure\n");
	}
}

