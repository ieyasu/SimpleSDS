/* sds.c - sds-* subcommand wrapper that automatically pipes output through
 *         ${PAGER}, less or more.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char * const subcommands[] = { "diff", "dump" };
#define N_SUBCOMMANDS (sizeof(subcommands) / sizeof(subcommands[0]))

void checked_dup2(int oldfd, int newfd)
{
    if (dup2(oldfd, newfd) != newfd) {
        perror("dup2(stdio)");
        abort();
    }
}

/* Open a pipe for one-way communication between parent and child processes,
 * fork, then rejigger FDs so the requested stdin/out is replaced with the
 * pipe.  Returns the pid_t value from the fork() call.
 *
 * whichfd: an in/out parameter.  On the way in, it must contain STDIN_FILENO
 *          or STDOUT_FILENO to request that the child's stdin or stdout
 *          stream be replaced with the pipe.  On the way out, it contains
 *          number of the pipe's fd to read or write to the child process.
 */
pid_t one_open(int *whichfd)
{
    int fds[2]; // [0] is read end; [1] is write
    if (pipe(fds)) {
        perror("pipe()ing for child stdio");
        abort();
    }

    pid_t pid = fork();
    switch (pid) {
    case -1: // error!
        perror("fork() in sds");
        abort();
        break;
    case 0: // in child
        if (*whichfd == STDIN_FILENO)
            checked_dup2(fds[0], *whichfd);
        else
            checked_dup2(fds[1], *whichfd);
        close(fds[0]);
        close(fds[1]);
        break;
    default: // in parent
        if (*whichfd == STDIN_FILENO) {
            close(fds[0]); // close read end
            *whichfd = fds[1];
        } else {
            close(fds[1]); // close write end
            *whichfd = fds[0];
        }
        break;
    }

    return pid;
}

int open_pager(void)
{
    char *argv[] = {NULL, NULL, NULL};
    int pager_in = STDIN_FILENO;
    pid_t pid = one_open(&pager_in);
    if (pid == 0) { // child
        argv[0] = getenv("PAGER");
        if (argv[0] && strlen(argv[0]) > 0) {
            if (!strcmp(argv[0], "less")) {
                argv[1] = "-R";
            }
            execvp(argv[0], argv);
        } else {
            // first try less
            argv[0] = "less"; argv[1] = "-R";
            execvp(argv[0], argv);

            // now try more
            argv[0] = "more"; argv[1] = NULL;
            execvp(argv[0], argv);
            // fallthrough to error code below
        }
        fprintf(stderr, "failed to exec pager '%s': %s\n",
                argv[0], strerror(errno));
        abort();
    }
    return pager_in;
}

#define MAX_ARGS 100

void exec_subcommand(int orig_argc, char **orig_argv, int istty)
{
    char *argv[MAX_ARGS], bin[512], subcommand[512];

    snprintf(bin, sizeof(bin), "sds-%s", orig_argv[1]);
    snprintf(subcommand, sizeof(subcommand), "sds %s", orig_argv[1]);

    int i = 0;
    argv[i++] = subcommand;
    if (istty)
        argv[i++] = "-G";
    int j = 2;
    while (i < MAX_ARGS && j < orig_argc) {
        argv[i++] = orig_argv[j++];
    }
    argv[i] = NULL;

    execvp(bin, argv);

    fprintf(stderr, "exec()ing '%s': %s\n", bin, strerror(errno));
    abort();
}

int larger_than_terminal(char *buf, int buflen)
{
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    w.ws_row -= 2;

    int lines = 0, col = 0;
    for (int i = 0; i < buflen; i++) {
        switch (buf[i]) {
        case '\n':
            lines++;
            col = 0;
            break;
        case '\033': // ANSI esc sequence
            while (i < buflen && buf[i] != 'm')
                i++;
            break;
        default:
            if (++col >= w.ws_col) {
                lines++;
                col = 0;
            }
            break;
        }
        if (lines >= w.ws_row)
            return -1; // too long!
    }
    return 0;
}

void usage(void)
{
    fputs("Usage: sds COMMAND ARG...\n", stderr);
    exit(-1);
}

int cmdcmp(const void *a, const void *b)
{
    return strcmp((const char *)a, *(const char **)b);
}

int main(int argc, char **argv)
{
    // parse command line
    if (argc < 2) {
        usage();
    }

    // make sure we have a known subcommand
    if (!bsearch(argv[1], subcommands, N_SUBCOMMANDS, sizeof(char *), cmdcmp)) {
        fprintf(stderr, "Invalid command '%s'\n\n", argv[1]);
        usage();
    }

    int istty = isatty(STDOUT_FILENO);

    int cmd_out = STDOUT_FILENO;
    pid_t pid = one_open(&cmd_out);
    if (pid == 0) { // child
        exec_subcommand(argc, argv, istty);
    }

    // parent
    errno = 0;

    char buf[4096];
    int n = read(cmd_out, buf, sizeof(buf));
    if (larger_than_terminal(buf, n)) {
        int pager_in = open_pager();

        do {
            if (write(pager_in, buf, (size_t)n) < n) {
                perror("writing output from command");
                abort();
            }
        } while ((n = (int)read(cmd_out, buf, sizeof(buf))) > 0);

        close(pager_in);
        waitpid(-1, NULL, 0);
    } else if (n > 0) {
        write(STDOUT_FILENO, buf, n);
    }
    close(cmd_out);

    if (errno != 0) {
        perror("after read");
        abort();
    }

    waitpid(-1, NULL, 0);

    return 0;
}
