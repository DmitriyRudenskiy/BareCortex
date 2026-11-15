/* sstrace.c: log a child's openat/openat2/ioctl/readlink syscalls via
 * seccomp user-notification. No ptrace (driver panics under debugger).
 * usage: sstrace /path/to/probe [args...]
 * child env is passed through; log -> stderr of parent */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <stdint.h>
#include <stddef.h>

/* manual seccomp-notify ABI (old host headers) */
struct seccomp_data {
    int nr;
    uint32_t arch;
    uint64_t instruction_pointer;
    uint64_t args[6];
};
struct seccomp_notif {
    uint64_t id;
    uint32_t pid;
    uint32_t flags;
    int32_t nr;
    uint64_t args[6];
};
struct seccomp_notif_resp {
    uint64_t id;
    int64_t val;
    int32_t error;
    uint32_t flags;
};
#define SECCOMP_SET_MODE_FILTER 1
#define SECCOMP_FILTER_FLAG_NEW_LISTENER 2
#define SECCOMP_RET_ALLOW 0x7fff0000u
#define SECCOMP_RET_USER_NOTIF 0x7fc00000u
#define SECCOMP_USER_NOTIF_FLAG_CONTINUE 1u
#define SECCOMP_IOCTL_NOTIF_RECV _IOWR('!', 0x00, struct seccomp_notif)
#define SECCOMP_IOCTL_NOTIF_SEND _IOWR('!', 0x01, struct seccomp_notif_resp)

static const int WATCH[] = {
    SYS_openat, SYS_openat2, SYS_ioctl, SYS_readlink, SYS_readlinkat,
    SYS_newfstatat, SYS_statx, SYS_access, SYS_faccessat, SYS_read,
};
#define NWATCH (sizeof(WATCH) / sizeof(WATCH[0]))

static struct seccomp_notif get_notif(int nfd)
{
    struct seccomp_notif req;
    for (;;) {
        memset(&req, 0, sizeof req);
        int r = ioctl(nfd, SECCOMP_IOCTL_NOTIF_RECV, &req);
        if (r < 0) {
            if (errno == EINTR) continue;
            fprintf(stderr, "[sstrace] recv error: %s\n", strerror(errno));
            exit(3);
        }
        if (r == 0) exit(0); /* no more notifications */
        return req;
    }
}

static void send_continue(int nfd, uint64_t id)
{
    struct seccomp_notif_resp resp;
    memset(&resp, 0, sizeof resp);
    resp.id = id;
    resp.flags = SECCOMP_USER_NOTIF_FLAG_CONTINUE;
    if (ioctl(nfd, SECCOMP_IOCTL_NOTIF_SEND, &resp) < 0 &&
        errno != ENOENT) {
        /* child may be gone */
    }
}

static void read_target_str(int pid, uint64_t addr, char *buf, int len)
{
    char tmp[512];
    struct iovec local = { tmp, sizeof tmp };
    struct iovec remote = { (void *)addr, (size_t)len < 512 ? (size_t)len : 512 };
    ssize_t n = process_vm_readv(pid, &local, 1, &remote, 1, 0);
    if (n > 0) {
        tmp[n < 511 ? n : 511] = 0;
        strncpy(buf, tmp, len - 1);
        buf[len - 1] = 0;
    } else {
        buf[0] = '?';
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sstrace cmd args...\n"); return 2; }
    if (fcntl(0, F_GETFD) < 0) { int z = open("/dev/null", O_RDWR); if (z < 0) { perror("open0"); return 1; } }
    /* BPF: ld nr; for each watched: jeq -> RET NOTIFY; else RET ALLOW */
    int n = 1 + 2 * (int)NWATCH + 1;
    struct sock_filter f[1 + 2 * 16 + 1];
    int i = 0;
    f[i++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr));
    for (int w = 0; w < (int)NWATCH; w++) {
        f[i++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, WATCH[w], 0, 1);
        f[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_USER_NOTIF);
    }
    f[i++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    (void)n;

    struct sock_fprog prog = { (unsigned short)i, f };
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) { perror("prctl"); return 1; }
    int nfd = (int)syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER,
                           SECCOMP_FILTER_FLAG_NEW_LISTENER, &prog);
    if (nfd < 0) { perror("seccomp"); return 1; }
    if (nfd < 3) { int n = dup(nfd); if (n < 0) { perror("dup"); return 1; } close(nfd); nfd = n; }
    fprintf(stderr, "[sstrace] listener fd=%d\n", nfd);

    pid_t child = fork();
    if (child < 0) { perror("fork"); return 1; }
    if (child == 0) {
        execvp(argv[1], &argv[1]);
        perror("execvp");
        _exit(127);
    }

    struct seccomp_notif req;
    for (;;) {
        req = get_notif(nfd);
        int nr = (int)req.nr;
        char path[256] = "";
        if (nr == SYS_openat || nr == SYS_openat2)
            read_target_str((int)req.pid, req.args[1], path, sizeof path);
        if (nr == SYS_readlink || nr == SYS_readlinkat)
            read_target_str((int)req.pid, req.args[nr == SYS_readlink ? 0 : 1], path, sizeof path);
        if (nr == SYS_newfstatat || nr == SYS_statx)
            read_target_str((int)req.pid, req.args[1], path, sizeof path);
        if (nr == SYS_access || nr == SYS_faccessat)
            read_target_str((int)req.pid, req.args[nr == SYS_access ? 0 : 1], path, sizeof path);

        fprintf(stderr, "[pid %d] nr=%d a0=%llx a1=%llx a2=%llx %s\n",
                (int)req.pid, nr, (unsigned long long)req.args[0],
                (unsigned long long)req.args[1], (unsigned long long)req.args[2], path);
        fflush(stderr);
        send_continue(nfd, req.id);

        int st;
        if (waitpid(child, &st, WNOHANG) == child) {
            /* drain a few more, then stop */
            fprintf(stderr, "== child exited: %s\n",
                    WIFSIGNALED(st) ? "signal" : WIFEXITED(st) ? "exit" : "?");
            break;
        }
    }
    return 0;
}
