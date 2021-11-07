#include "utils.h"
#include "terminal.h"
#include "pty.h"

#include <errno.h>
#include <fcntl.h>
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

#define fatal(s) (perror(s), exit((errno) ? errno : EXIT_FAILURE))

static void sys_sigchld(int);

int
pty_init(const char *shell, int *mfd_, int *sfd_)
{
	int pid = 0;
	int mfd = 0;
	int sfd = 0;

	if (openpty(&mfd, &sfd, NULL, NULL, NULL) < 0) {
		fatal("openpty()");
	}

	switch ((pid = fork())) {
	case -1: // fork failed
		fatal("fork()");
	case 0: { // child process
		setsid();

		dup2(sfd, 0);
		dup2(sfd, 1);
		dup2(sfd, 2);

		if (ioctl(sfd, TIOCSCTTY, NULL) < 0) {
			fatal("ioctl");
		}

		close(sfd);
		close(mfd);

		const struct passwd *pwd = getpwuid(getuid());
		if (!pwd) {
			fatal("getpwuid()");
		}

		if (!shell || !*shell) {
			shell = getenv("SHELL");
			if (!shell || !*shell) {
				shell = pwd->pw_shell;
				if (!shell || !*shell) {
					shell = "/bin/sh";
				}
			}
		}

		// execute shell
		setenv("SHELL",   shell, 1);
		setenv("USER",    pwd->pw_name, 1);
		setenv("LOGNAME", pwd->pw_name, 1);
		setenv("HOME",    pwd->pw_dir, 1);

		// TODO(ben): Move to non-deprecated sigaction API
		signal(SIGCHLD, SIG_DFL);
		signal(SIGHUP,  SIG_DFL);
		signal(SIGINT,  SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
		signal(SIGTERM, SIG_DFL);
		signal(SIGALRM, SIG_DFL);

		execlp(shell, shell, (char *)NULL);

		_exit(1);
		break;
	}
	default: // parent process
		close(sfd);
		signal(SIGCHLD, sys_sigchld);
		break;
	}

#if 0
	{
		struct termios tattr;

		if (tcgetattr(mfd, &tattr) < 0) {
			perror("tcgetattr()");
			exit(EXIT_FAILURE);
		}

		tattr.c_oflag |= OFILL;
		if (tcsetattr(mfd, TCSADRAIN, &tattr) < 0) {
			perror("tcsetattr()");
			exit(EXIT_FAILURE);
		}
	}
#endif

	*mfd_ = mfd;
	*sfd_ = sfd;

	return pid;
}

size_t
pty_read(int mfd, uchar *buf, size_t len, uint32 msec)
{
	ASSERT(mfd > 0);

	fd_set rset;
	FD_ZERO(&rset);
	FD_SET(mfd, &rset);

	switch (select(mfd + 1, &rset, NULL, NULL, &(struct timeval){ 0, msec * 1E3 })) {
	case -1: fatal("select()");
	case  0: return 0;
	}

	ssize_t nread = read(mfd, buf, len);
	switch (nread) {
	case -1: fatal("read()");
	case  0: exit(0);
	}

	return nread;

	/* if (!status) { */
	/* 	return 0; */
	/* } else if (status < 0) { */
	/* 	fatal("select()"); */
	/* } */

	/* ssize_t nread = read(mfd, buf, len); */

	/* switch (nread) { */
	/* case  0: */
	/* 	exit(0); */
	/* case -1: */
	/* 	fatal("read()"); */
	/* default: */
	/* 	break; */
	/* } */

	/* return nread; */
}

size_t
pty_write(int mfd, const uchar *buf, size_t len)
{
	ASSERT(mfd > 0);

	size_t idx = 0;
	fd_set wset;

	while (idx < len) {
		FD_ZERO(&wset);
		FD_SET(mfd, &wset);

		if (pselect(mfd + 1, NULL, &wset, NULL, NULL, NULL) < 0) {
			fatal("pselect()");
		}

		if (FD_ISSET(mfd, &wset)) {
			ssize_t result = write(mfd, buf + idx, len - idx);
			if (result < 0) {
				fatal("write()");
			}

			idx += result;
		}
	}

	return idx;
}

void
pty_resize(int mfd, int cols, int rows, int colsize, int rowsize)
{
	ASSERT(mfd > 0);

	struct winsize spec = {
		.ws_col = cols,
		.ws_row = rows,
		.ws_xpixel = cols * colsize,
		.ws_ypixel = rows * rowsize
	};

	if (ioctl(mfd, TIOCSWINSZ, &spec) < 0) {
		perror("ioctl()");
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

