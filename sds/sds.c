/* sds.c - sds-* subcommand wrapper that automatically pipes output through
 *         ${PAGER}, less or more.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char * const subcommands[] = { "dump" };
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
    int pager_in = STDIN_FILENO;
    pid_t pid = one_open(&pager_in);
    if (pid == 0) { // child
        char * const argv[] = {"less", "-R", NULL};
        if (execvp(argv[0], argv)) {
            perror("exec()ing pager");
            abort();
        }
    }
    return pager_in;
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

    int cmd_out = STDOUT_FILENO;
    pid_t pid = one_open(&cmd_out);
    if (pid == 0) { // child
        //execvp("sds-dump", ["sds dump", argv[1..-1], NULL]);
        char * const argv_[] = {"sds dump", "-G", "/scratch3/bishop/sample_sds/tavg1_2d_flx_Nx.20120402_0830.nc4", NULL};
        if (execvp("sds/sds-dump", argv_)) {
            perror("exec()ing sds-dump");
            abort();
        }
    }

    // parent
    char buf[1024];
    int n;

    int pager_in = open_pager();

    errno = 0;
    while ((n = (int)read(cmd_out, buf, sizeof(buf))) > 0) {
        if (write(pager_in, buf, (size_t)n) < n) {
            perror("writing output from command");
            abort();
        }
    }
    close(cmd_out);
    close(pager_in);
    if (errno != 0) {
        perror("after read");
        abort();
    }

    waitpid(-1, NULL, 0);
    waitpid(-1, NULL, 0);

    return 0;
}
