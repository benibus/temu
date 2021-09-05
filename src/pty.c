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
#include "pty.h"

#define WRITE_LIMIT 1024

static void sys_sigchld(int);

int
pty_init(TTY *tty, const char *shell)
{
	PTY *pty = &tty->pty;
	const char *cmd = (shell) ? shell : "/bin/dash";
	char *args[] = { NULL };

	if (openpty(&pty->mfd, &pty->sfd, NULL, NULL, NULL) < 0) {
		error_fatal("openpty()", 1);
	}

	switch ((pty->pid = fork())) {
	case -1: // fork failed
		error_fatal("fork()", 1);
		break;
	case 0: // child process
		setsid();

		dup2(pty->sfd, 0);
		dup2(pty->sfd, 1);
		dup2(pty->sfd, 2);

		if (ioctl(pty->sfd, TIOCSCTTY, NULL) < 0) {
			error_fatal("ioctl()", 1);
		}
		close(pty->sfd);
		close(pty->mfd);
		{
			// execute shell
			setenv("SHELL", cmd, 1);

			// TODO(ben): Move to non-deprecated sigaction API
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
		close(pty->sfd);
		signal(SIGCHLD, sys_sigchld);
		break;
	}

	return pty->mfd;
}

size_t
pty_read(TTY *tty, uint8 type)
{
	PTY *pty = &tty->pty;
	int nr, nw;

	nr = read(pty->mfd, pty->buf + pty->size, LEN(pty->buf) - pty->size);

	switch (nr) {
		case  0:
			exit(0);
			break;
		case -1:
			error_fatal("pty_read()", 1);
			break;
		default:
			pty->size += nr;
			nw = tty_write_raw(tty, pty->buf, pty->size, type);
			pty->size -= nw;
			if (pty->size) {
				ASSERT(pty->size + nw <= LEN(pty->buf));
				memmove(pty->buf, pty->buf + nw, pty->size);
			}
			break;
	}

	return nr;
}

size_t
pty_write(TTY *tty, const char *str, size_t len, uint8 type)
{
	const PTY *pty = &tty->pty;
	fd_set fds_r, fds_w;
	size_t i = 0, rem = WRITE_LIMIT;
	ssize_t n; // write() return value

	while (i < len) {
		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_SET(pty->mfd, &fds_r);
		FD_SET(pty->mfd, &fds_w);

		if (pselect(pty->mfd + 1, &fds_r, &fds_w, NULL, NULL, NULL) < 0) {
			error_fatal("pselect()", 1);
		}
		if (FD_ISSET(pty->mfd, &fds_w)) {
			n = write(pty->mfd, &str[i], MIN(len-i, rem));
			if (n < 0) {
				error_fatal("write()", 1);
			}
			if (i + n < len) {
				if (len - i < rem) {
					rem = pty_read(tty, type);
				}
			}
			i += n;
		}
		if (i < len && FD_ISSET(pty->mfd, &fds_r)) {
			rem = pty_read(tty, type);
		}
	}

	return i;
}

void
pty_resize(const TTY *tty, int cols, int rows)
{
	struct winsize region = {
		.ws_col = cols,
		.ws_row = rows,
		.ws_xpixel = cols * tty->colpx,
		.ws_ypixel = rows * tty->rowpx
	};

	if (ioctl(tty->pty.mfd, TIOCSWINSZ, &region) < 0) {
		fprintf(stderr, "ERROR: pty_resize() - ioctl() failure\n");
	}
}

void
sys_sigchld(int arg)
{
	(void)arg;
	// TODO(ben): Actually implement this. Right now it just prevents returning an error code
	// as a result of normal shell termination (via "$ exit")
	_exit(0);
}

