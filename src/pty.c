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

#define FATAL(...) do {    \
    const int e__ = errno; \
    printerr("SYS ERROR (%d) %s: ", e__, strerror(e__)); \
    printerr(__VA_ARGS__); \
    printerr("\n");        \
    exit(1);               \
} while (0)

static void sys_sigchld(int);
static void signal_reset(int signo);
static void signal_handle(int signo, siginfo_t *info, void *context);

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

    switch ((pid = fork())) {
    case -1: // fork failed
        FATAL("fork()");
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
            FATAL("getpwuid()");
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

        static const int signals[] = { SIGCHLD, SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGALRM };
        for (uint i = 0; i < LEN(signals); i++) {
            signal_reset(signals[i]);
        }

        execlp(shell, shell, (char *)NULL);

        _exit(1);
        break;
    }
    default: // parent process
        close(sfd);

        struct sigaction sa = {
            .sa_sigaction = signal_handle,
            .sa_flags = SA_SIGINFO|SA_RESTART|SA_NOCLDSTOP
        };
        if (sigaction(SIGCHLD, &sa, NULL) < 0) {
            FATAL("Failed to install SIGCHLD handler");
        }
        signal_reset(SIGINT);
        signal_reset(SIGQUIT);
        break;
    }

    *mfd_ = mfd;
    *sfd_ = sfd;

    return pid;
}

void
pty_hangup(int pid)
{
    if (pid) {
        kill(pid, SIGHUP);
    }
}

size_t
pty_read(int mfd, uchar *buf, size_t len, uint32 msec)
{
    ASSERT(mfd > 0);

    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(mfd, &rset);

    switch (select(mfd + 1, &rset, NULL, NULL, &(struct timeval){ 0, msec * 1E3 })) {
    case -1: FATAL("select");
    case  0: return 0;
    }

    ssize_t nread = read(mfd, buf, len);
    switch (nread) {
    case -1: FATAL("read");
    case  0: exit(0);
    }

    return nread;
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
            FATAL("pselect");
        }

        if (FD_ISSET(mfd, &wset)) {
            ssize_t result = write(mfd, buf + idx, len - idx);
            if (result < 0) {
                FATAL("write");
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
        FATAL("ioctl TIOCSWINSZ");
    }
}

void
signal_reset(int signo)
{
    if (sigaction(signo, &(struct sigaction){ .sa_handler = SIG_DFL }, NULL) < 0) {
        FATAL("Failed to install signal(%d) handler", signo);
    }

}

void
signal_handle(int signo, siginfo_t *info, void *context)
{
    UNUSED(context);

    switch (signo) {
    case SIGCHLD: {
        int status;
        waitpid(info->si_pid, &status, WNOHANG);

        /* FIXME(ben):
         * The forked process exits with 1 after manually sending SIGHUP (i.e. after window close)
         * so we can't reliably pass-on the child's return value.
         * There's probably something I'm missing here...
         */
        dbgprint("Handling SIGCHLD (pid = %hd), status = %d", info->si_pid, status);
        if (WIFEXITED(status) && WEXITSTATUS(status)) {
            dbgprint("Child exited with code %d", WEXITSTATUS(status));
        }

        _exit(0);
    }
    default:
        break;
    }
}

#undef FATAL

