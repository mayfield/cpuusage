#define _GNU_SOURCE
#include<stdint.h>
#include<stdlib.h>
#include<stdio.h>
#include<fcntl.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<time.h>


// See: https://stackoverflow.com/questions/23367857/accurate-calculation-of-cpu-usage-given-in-percentage-in-linux
// for details on how each of these should be accumulated.
// In short:
//  1. guest is a subset of user
//  2. guestnice is a subset of nice
//  3. system, irq & softirq are all considered system and should be added.
//  4. idle and iowait are considered idle time and should be added
//  5. steal is relevant only when inside a VM and the outer hyporvisor stole ticks from us.
struct stats {
    uint64_t user;
    uint64_t nice;
    uint64_t system;
    uint64_t idle;
    uint64_t iowait;
    uint64_t irq;
    uint64_t softirq;
    uint64_t steal;
    uint64_t guest;
    uint64_t guestnice;
};


static void load_from_fd(int fd, struct stats *stats) {
    char buf[4096] = {0};
    int r = read(fd, buf, sizeof(buf) - 1);
    if (r == -1) {
        fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
        exit(1);
    } else if (r == 0) {
        memset(stats, 0, sizeof(struct stats));
    } else {
        if (sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
            &stats->user, &stats->nice, &stats->system, &stats->idle, &stats->iowait,
            &stats->irq, &stats->softirq, &stats->steal, &stats->guest, &stats->guestnice) == -1) {
            fprintf(stderr, "Failed to parse cpu line [%d]: %s\n", fd, strerror(errno));
            exit(1);
        }
    }
}


static void store_to_fd(int fd, struct stats *stats) {
   char buf[4096] = {0};
    int r = snprintf(buf, sizeof(buf), "cpu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
        stats->user, stats->nice, stats->system, stats->idle, stats->iowait,
        stats->irq, stats->softirq, stats->steal, stats->guest, stats->guestnice);
    if (r < 0 || r >= sizeof(buf)) {
        fprintf(stderr, "Failed to build stats string: %s\n", strerror(errno));
        exit(1);
    }
    lseek(fd, 0, SEEK_SET);
    ftruncate(fd, 0);
    write(fd, buf, strlen(buf));
}


static uint64_t get_busy(struct stats *s) {
    return s->user + s->nice + s->system + s->irq + s->softirq;
}


static uint64_t get_idle(struct stats *s) {
    return s->idle + s->iowait + s->steal;
}


int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "Usage: %s COOKIE_FILE\n", argv[0]);
        return 1;
    }
    int fd = open("/proc/stat", O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open [/proc/stat]: %s", strerror(errno));
        return 1;
    }
    struct stats c = {0};
    load_from_fd(fd, &c);
    close(fd);
    int cookie_fd = open(argv[1], O_RDWR | O_CREAT, 0600);
    if (cookie_fd == -1) {
        fprintf(stderr, "Failed to open cookie file [%s]: %s", argv[1], strerror(errno));
        return 1;
    }
    lseek(cookie_fd, 0, SEEK_SET);
    struct stats prev = {0};
    load_from_fd(cookie_fd, &prev);
    store_to_fd(cookie_fd, &c);
    close(cookie_fd);
    uint64_t cur_busy = get_busy(&c);
    uint64_t cur_idle = get_idle(&c);
    uint64_t prev_busy = get_busy(&prev);
    uint64_t prev_idle = get_idle(&prev);
    uint64_t d_busy = cur_busy - prev_busy;
    uint64_t d_idle = cur_idle - prev_idle;
    double usage = (double) d_busy / (d_busy + d_idle);
    printf("%.1f%%\n", usage * 100.0);
    return 0;
}
