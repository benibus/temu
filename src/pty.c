/*------------------------------------------------------------------------------*
 * This file is part of temu
 * Copyright (C) 2021-2022 Benjamin Harkins
 *
 * temu is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 *------------------------------------------------------------------------------*/

#include "utils.h"
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

#define FATAL(...) do {      \
    const int e__ = errno;   \
    err_printf(__VA_ARGS__); \
    fprintf(stderr, ": (%d) %s\n", e__, strerror(e__)); \
    exit(e__ ? e__ : 1);     \
} while (0)

static void setup_parent_signals(void);
static void reset_signal(int signo);
static void on_signal(int signo, siginfo_t *info, void *context);

int
pty_init(const char *shell, int *mfd_, int *sfd_)
{
    int pid = 0;
    int mfd = 0;
    int sfd = 0;

    errno = 0;

    if (openpty(&mfd, &sfd, NULL, NULL, NULL) < 0) {
        FATAL("openpty");
    }

    setup_parent_signals();

    switch ((pid = fork())) {
    case -1: // fork failed
        FATAL("fork");
    case 0: { // child process
        setsid();

        dup2(sfd, 0);
        dup2(sfd, 1);
        dup2(sfd, 2);

        if (ioctl(sfd, TIOCSCTTY, NULL) < 0) {
            FATAL("ioctl TIOCSCTTY");
        }

        close(sfd);
        close(mfd);

        const struct passwd *pwd = getpwuid(getuid());
        if (!pwd) {
            FATAL("getpwuid");
        }

        if (strempty(shell)) {
            shell = getenv("SHELL");
            if (strempty(shell)) {
                shell = pwd->pw_shell;
                if (strempty(shell)) {
                    shell = "/bin/sh";
                }
            }
        }

        // execute shell
        setenv("SHELL",   shell, 1);
        setenv("USER",    pwd->pw_name, 1);
        setenv("LOGNAME", pwd->pw_name, 1);
        setenv("HOME",    pwd->pw_dir, 1);

        static const int signals[] = { SIGCHLD, SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM };
        for (uint i = 0; i < LEN(signals); i++) {
            reset_signal(signals[i]);
        }

        execlp(shell, shell, (char *)NULL);

        _exit(1);
        break;
    }
    default: // parent process
        close(sfd);
        break;
    }

    *mfd_ = mfd;
    *sfd_ = sfd;

    return pid;
}

void
pty_hangup(int cpid)
{
    if (cpid) {
        kill(cpid, SIGHUP);
    }
}

size_t
pty_read(int mfd, uchar *buf, size_t len)
{
    ASSERT(mfd > 0);

    errno = 0;

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(mfd, &rset);

    const int res = pselect(mfd + 1, &rset, NULL, NULL, NULL, NULL);
    if (res < 0 && errno != EINTR) {
        FATAL("pselect");
    } else if (res <= 0) {
        return 0;
    }

    const ssize_t nread = read(mfd, buf, len);
    if (nread < 0 && errno != EINTR) {
        FATAL("read");
    }

    return MAX(0, nread);
}

size_t
pty_write(int mfd, const uchar *buf, size_t len)
{
    ASSERT(mfd > 0);

    size_t idx = 0;
    fd_set wset;

    errno = 0;

    while (idx < len) {
        FD_ZERO(&wset);
        FD_SET(mfd, &wset);

        if (pselect(mfd + 1, NULL, &wset, NULL, NULL, NULL) < 0) {
            if (errno != EINTR) {
                FATAL("pselect");
            }
            continue;
        }

        if (FD_ISSET(mfd, &wset)) {
            const ssize_t nwrite = write(mfd, buf + idx, len - idx);
            if (nwrite >= 0) {
                idx += nwrite;
            } else if (errno != EINTR) {
                FATAL("write");
            }
        }
    }

    return idx;
}

void
pty_resize(int mfd, int cols, int rows, int colsize, int rowsize)
{
    ASSERT(mfd > 0);

    errno = 0;

    struct winsize spec = {
        .ws_col = cols,
        .ws_row = rows,
        .ws_xpixel = cols * colsize,
        .ws_ypixel = rows * rowsize
    };

    if (ioctl(mfd, TIOCSWINSZ, &spec) < 0) {
        FATAL("ioctl TIOCSWINSZ");
    }
}

void
on_signal(int signo, siginfo_t *info, void *context)
{
    UNUSED(context);

    int status;

    if (signo == SIGCHLD) {
        // Acknowledge the child's exit to avoid creating a zombie process
        waitpid(info->si_pid, &status, WNOHANG);
    }
}

void
reset_signal(int signo)
{
    errno = 0;

    if (sigaction(signo, &(struct sigaction){ .sa_handler = SIG_DFL }, NULL) < 0) {
        FATAL("sigaction %d", signo);
    }

}

void
setup_parent_signals(void)
{
    struct sigaction sa = {
        .sa_sigaction = on_signal,
        .sa_flags = SA_SIGINFO|SA_RESTART|SA_NOCLDSTOP
    };

    if (sigaction(SIGCHLD, &sa, NULL) < 0) {
        FATAL("sigaction SIGCHLD");
    }

    reset_signal(SIGINT);
    reset_signal(SIGQUIT);
}

#undef FATAL

