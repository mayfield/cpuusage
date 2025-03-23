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


static void sleep_ms(unsigned long ms) {
    struct timespec duration;
    duration.tv_nsec = (ms % 1000) * 1000000;
    duration.tv_sec = ms / 1000;
    nanosleep(&duration, NULL);
}


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


static double cpu_busy_pct(struct stats *cur, struct stats *prev) {
    uint64_t cur_busy = get_busy(cur);
    uint64_t cur_idle = get_idle(cur);
    uint64_t prev_busy = get_busy(prev);
    uint64_t prev_idle = get_idle(prev);
    uint64_t d_busy = cur_busy - prev_busy;
    uint64_t d_idle = cur_idle - prev_idle;
    return (double) d_busy / (d_busy + d_idle);
}


static double cpu_hz() {
    int fd = open("/sys/devices/system/cpu/possible", O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open [/sys/devices/system/cpu/possible]: %s", strerror(errno));
        exit(1);
    }
    char buf[4096] = {0};
    if (read(fd, buf, sizeof(buf) - 1) == -1) {
        fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
        exit(1);
    }
    int start, end;
    if (sscanf(buf, "%d-%d", &start, &end) == -1) {
        fprintf(stderr, "Failed to parse [/sys/devices/system/cpu/possible]: %s", strerror(errno));
        exit(1);
    }
    double t = 0;
    int count = 0;
    sleep_ms(200);
    for (int i = start; i <= end; i++) {
        char fname[4096];
        snprintf(fname, sizeof(fname), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        int fd = open(fname, O_RDONLY);
        sleep_ms(10);
        if (read(fd, buf, sizeof(buf) - 1) == -1) {
            fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
            continue;
        }
        long cpuspeed = strtol(buf, NULL, 10);
        t += cpuspeed;
        count++;
    }
    return t / count;
}


int main(int argc, char *argv[]) {
    if (argc < 2 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "Usage: %s COOKIE_FILE [--mhz]\n", argv[0]);
        return 1;
    }
    int fd = open("/proc/stat", O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Failed to open [/proc/stat]: %s", strerror(errno));
        return 1;
    }
    struct stats cur = {0};
    load_from_fd(fd, &cur);
    int cookie_fd = open(argv[1], O_RDWR | O_CREAT, 0600);
    if (cookie_fd == -1) {
        fprintf(stderr, "Failed to open cookie file [%s]: %s", argv[1], strerror(errno));
        return 1;
    }
    struct stats prev = {0};
    load_from_fd(cookie_fd, &prev);

    if (argc > 2 && strcmp(argv[2], "--mhz") == 0) {
        printf("%.2f Ghz, ", cpu_hz() / 1000000.0);
    }

    printf("%.1f﹪\n", cpu_busy_pct(&cur, &prev) * 100.0);

    store_to_fd(cookie_fd, &cur);
    return 0;
}
