// Flosh Linux 1.0 init script
// bashа нет, используется busybox ash, т.к. дистро минимальный

#include <stdio.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/sysmacros.h>

int clear() {
    pid_t pid = fork();
    if (pid == -1) { perror("fork clear"); return 1; }
    if (pid == 0) {
        execl("/bin/busybox", "busybox", "clear", NULL);
        perror("execl clear");
        return 1;
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
    return 0;
}

int pkgrfsh() {
    pid_t pid = fork();
    if (pid == -1) { perror("fork clear"); return 1; }
    if (pid == 0) {
        execl("/bin/flosh", "flosh", "refresh", NULL);
        perror("execl refresh");
        return 1;
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
    return 0;
}

int rufont() {
    pid_t pid = fork();
    if (pid == -1) { perror("fork font"); return 1; }
    if (pid == 0) {
        execl("/bin/busybox", "busybox", "setfont", "-C", "/dev/tty0", "/usr/fonts/LatArCyrHeb-16.psfu.gz", NULL);
        perror("execl font");
        return 1;
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
    return 0;
}

int mountdrive() {
    DIR *dir;
    struct dirent *entry;
    char devpath[256];
    char mountpoint[256] = "/mnt";

    mkdir(mountpoint, 0755);

    dir = opendir("/dev");
    if (!dir) {
        perror("opendir /dev");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        int is_block = 0;
        int has_digit = 0;

        if (strncmp(name, "sd", 2) == 0 ||
            strncmp(name, "hd", 2) == 0 ||
            strncmp(name, "vd", 2) == 0 ||
            strncmp(name, "nvme", 4) == 0) {
            is_block = 1;
        }
        if (!is_block) continue;

        for (char *p = (char*)name; *p; p++) {
            if (*p >= '0' && *p <= '9') { has_digit = 1; break; }
        }
        if (!has_digit) continue;

        snprintf(devpath, sizeof(devpath), "/dev/%s", name);

        if (mount(devpath, mountpoint, "ext4", 0, NULL) == 0) {
            if (access("/mnt/drive", F_OK) == 0) {
                closedir(dir);
                return 0;
            } else {
                umount(mountpoint);
            }
        }

        if (mount(devpath, mountpoint, "vfat", 0, NULL) == 0) {
            if (access("/mnt/drive", F_OK) == 0) {
                closedir(dir);
                return 0;
            } else {
                umount(mountpoint);
            }
        }

    }

    closedir(dir);
    return -1;
}

void errbeep() {
    for (int i = 0; i < 3; i++) {
        printf("\a");
        fflush(stdout);
        sleep(2);
    }
}

int main() {
    mount("proc", "/proc", "proc", 0, NULL);
    mount("sysfs", "/sys", "sysfs", 0, NULL);
    mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);

    sleep(3);

    mkdir("/mnt", 0755);
    mkdir("/drive", 0755);
    if (mountdrive() == 0) {
        mkdir("/drive", 0755);

        if (mount("/mnt/drive", "/drive", NULL, MS_BIND, NULL) == 0) {
            printf("Bind-mounted /mnt/drive to /drive\n");
        } else {
            perror("bind mount");
            errbeep();

            if (symlink("/mnt/drive", "/drive") != 0) {
                perror("symlink fallback");
                errbeep();
            } else {
                printf("Symlink fallback /drive -> /mnt/drive\n");
            }
        }
    } else {
        printf("\e[1;31m\e[5m[WARNING]\e[0m Couldn\'t mount drive files. Post-init script might mount Flash drive automatically.");
        errbeep();
    }

    mknod("/dev/console", S_IFCHR | 0600, makedev(5, 1));

    int fd = open("/dev/console", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        close(fd);
    }

    clear();
    rufont();
    pkgrfsh();

    printf("\nWelcome to Flosh Linux! (\033[1;31mНЕ РАСПРОСТРАНЯТЬ\033[0m)\n\n");

    execl("/bin/busybox", "busybox", "ash", "-l", NULL);
    perror("execl failed");

    while (1) sleep(1);
    return 0;
}
