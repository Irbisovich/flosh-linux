// Flosh Linux 1.0 management program

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/wait.h>
#include <termios.h>
#include <sys/mount.h>
#include <dirent.h>

#define MAX_LINE 512

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

int info() {
    printf("\n");

    FILE *f = fopen("/etc/art.txt", "r");
    if (f) {
        char line[512];
        while (fgets(line, sizeof(line), f)) {
            printf(" %s", line);
        }
        fclose(f);

        printf("\n");
    } else {
        printf("Couldn\'t print out logo.\n");
    }

    printf("Flosh Linux 1.0 (\033[1;31mНЕ РАСПРОСТРАНЯТЬ\033[0m) | by irbisx7\n");

    return 0;
}

void print_usage(bool show_info) {
    if (show_info) {
        fprintf(stderr, "\nflosh is a Flosh Linux distro manager.\n\nUsage:\n");
    } else {
        fprintf(stderr, "\nUsage:\n");
    }
    fprintf(stderr, "    reboot - reboots PC. (example: flosh reboot)\n");
    fprintf(stderr, "    shutdown - shutdowns PC. (example: flosh shutdown)\n");
    fprintf(stderr, "    halt - halts distro. (example: flosh halt)\n");
    fprintf(stderr, "    install - installs package. (example: flosh install packagename.tar)\n");
    fprintf(stderr, "    remove - removes package. (example: flosh remove packagename)\n");
    fprintf(stderr, "    refresh - installs packages into ramfs. (example: flosh refresh)\n");
    fprintf(stderr, "    list - prints out list of installed packages. (example: flosh list)\n");
    fprintf(stderr, "    info - prints out information about the distro. (example: flosh info)\n\n");
}

char* pkgfiles(const char *pkgname) {
    FILE *f = fopen("/drive/INFO.cfg", "r");
    if (!f) return NULL;

    char line[MAX_LINE];
    int in_pkg = 0;

    char *files = malloc(4096);
    if (!files) { fclose(f); return NULL; }
    files[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '\0') continue;

        if (line[0] == '[') {
            if (in_pkg) break;
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                if (strcmp(line + 1, pkgname) == 0) {
                    in_pkg = 1;
                }
            }
        } else if (in_pkg && line[0] != '[') {
            if (strlen(files) > 0) strcat(files, " ");
            strcat(files, line);
        }
    }
    fclose(f);

    if (strlen(files) == 0) {
        free(files);
        return NULL;
    }
    return files;
}

int append_file(const char *src, const char *dst) {
    FILE *f_src = fopen(src, "r");
    if (!f_src) { perror("fopen src"); return -1; }
    FILE *f_dst = fopen(dst, "a");
    if (!f_dst) { perror("fopen dst"); fclose(f_src); return -1; }

    char buffer[1024];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f_src)) > 0) {
        fwrite(buffer, 1, n, f_dst);
    }
    fclose(f_src);
    fclose(f_dst);
    return 0;
}

int command(const char *cmd, char *const argv[]) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        execvp(cmd, argv);
        
        perror("execvp");
        exit(1);
    } else {
        int status;

        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else {
            return -1;
        }
    }
}

int merge(const char *add) {
    if (add == NULL || access(add, R_OK) != 0) {
        fprintf(stderr, "add archive not found or not readable\n");
        return -1;
    }

    char tmp_base[] = "/tmp/merge_base_XXXXXX";
    char tmp_add[] = "/tmp/merge_add_XXXXXX";
    if (mkdtemp(tmp_base) == NULL) {
        perror("mkdtemp tmp_base");
        return -1;
    }
    if (mkdtemp(tmp_add) == NULL) {
        perror("mkdtemp tmp_add");
        rmdir(tmp_base);
        return -1;
    }

    int ret = 0;
    char cmd[512];

    if (access("/drive/rootfs.tar", R_OK) == 0) {
        if (command("/bin/busybox", (char *[]){"tar", "-xf", "/drive/rootfs.tar", "-C", tmp_base, NULL}) != 0) {
            fprintf(stderr, "Couldn\'t extract base archive, continuing with empty\n");
        }
    } else {
        printf("Base archive not found, starting fresh.\n");
    }

    if (command("/bin/busybox", (char *[]){"tar", "-xf", add, "-C", tmp_add, NULL}) != 0) {
        fprintf(stderr, "Failed to extract add archive\n");
        ret = -1;
        goto cleanup;
    }

    char src_dot[256], dst_slash[256];
    snprintf(src_dot, sizeof(src_dot), "%s/.", tmp_add);
    snprintf(dst_slash, sizeof(dst_slash), "%s/", tmp_base);

    if (command("/bin/busybox", (char *[]){"cp", "-a", src_dot, dst_slash, NULL}) != 0) {
        fprintf(stderr, "Failed to copy files\n");
        ret = -1;
        goto cleanup;
    }

    unlink("/drive/rootfs.tar");
    if (command("/bin/busybox", (char *[]){"tar", "-cf", "/drive/rootfs.tar", "-C", tmp_base, ".", NULL}) != 0) {
        fprintf(stderr, "Failed to create output archive\n");
        ret = -1;
        goto cleanup;
    }

cleanup:
    snprintf(cmd, sizeof(cmd), "rm -rf %s %s", tmp_base, tmp_add);
    system(cmd);
    return ret;
}

char* pkgnames(const char *cfg) {
    FILE *f = fopen(cfg, "r");
    if (!f) {
        return NULL;
    }
    char line[256];
    char *names = malloc(1);
    names[0] = '\0';
    int total = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end) {
                *end = '\0';
                char *name = line + 1;
                int new_len = strlen(names) + strlen(name) + 2; // +1 for space, +1 for null
                names = realloc(names, new_len);
                if (total > 0) strcat(names, " ");
                strcat(names, name);
                total++;
            }
        }
    }
    fclose(f);
    return names;
}

int rmpkgsec(const char *pkgname) {
    FILE *f = fopen("/drive/INFO.cfg", "r");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *content = malloc(size + 1);
    if (!content) { fclose(f); return -1; }
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    char *start = strstr(content, "[");
    char *found_start = NULL;
    while (start) {
        char *end = strchr(start + 1, ']');
        if (!end) break;
        *end = '\0';
        if (strcmp(start + 1, pkgname) == 0) {
            found_start = start;
            *end = ']';
            break;
        }
        *end = ']';
        start = strstr(end + 1, "[");
    }

    if (!found_start) {
        free(content);
        return -1;
    }

    char *next_section = strstr(found_start + 1, "\n[");
    if (next_section) {
        size_t to_remove = (next_section - found_start) + 1; // +1 для '\n'
        memmove(found_start, next_section + 1, size - (next_section - found_start) + 1);
        size -= to_remove;
    } else {
        if (found_start > content && found_start[-1] == '\n') {
            found_start--;
        }
        size = found_start - content;
    }

    FILE *out = fopen("/drive/INFO.cfg", "w");
    if (!out) { free(content); return -1; }
    fwrite(content, 1, size, out);
    fclose(out);

    if (size == 0) unlink("/drive/INFO.cfg");

    free(content);
    return 0;
}

int tarrmpkg(const char *pkgname) {
    char *files = pkgfiles(pkgname);
    if (!files) {
        fprintf(stderr, "Package '%s' not found in database.\n", pkgname);
        return -1;
    }

    char tmp_dir[] = "/tmp/rootfs_remove_XXXXXX";
    if (mkdtemp(tmp_dir) == NULL) {
        perror("mkdtemp");
        free(files);
        return -1;
    }

    if (access("/drive/rootfs.tar", R_OK) == 0) {
        char *args[] = {"tar", "-xf", "/drive/rootfs.tar", "-C", tmp_dir, NULL};
        if (command("/bin/busybox", args) != 0) {
            fprintf(stderr, "Failed to extract rootfs.tar\n");
            rmdir(tmp_dir);
            free(files);
            return -1;
        }
    } else {
        fprintf(stderr, "rootfs.tar not found, nothing to remove from archive.\n");
        rmdir(tmp_dir);
        free(files);
        return -1;
    }

    char *token = strtok(files, " ");
    while (token) {
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", tmp_dir, token);
        if (access(fullpath, F_OK) == 0) {
            printf("Removing from rootfs: %s\n", fullpath);
            unlink(fullpath);
        }
        token = strtok(NULL, " ");
    }
    free(files);

    unlink("/drive/rootfs.tar");
    char *args_pack[] = {"tar", "-cf", "/drive/rootfs.tar", "-C", tmp_dir, ".", NULL};
    if (command("/bin/busybox", args_pack) != 0) {
        fprintf(stderr, "Failed to repack rootfs.tar\n");
        rmdir(tmp_dir);
        return -1;
    }

    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", tmp_dir);
    system(cmd);

    return 0;
}

char getch(void) {
    struct termios oldattr, newattr;
    char ch;

    tcgetattr(STDIN_FILENO, &oldattr);
    newattr = oldattr;

    newattr.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newattr);

    ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);

    return ch;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(true);
        return 1;
    }

    if (geteuid() != 0) {
        fprintf(stderr, "flosh: must be run as root\n");
        return 1;
    }

    if (strcmp(argv[1], "reboot") == 0) {
        reboot(RB_AUTOBOOT);
    } else if (strcmp(argv[1], "shutdown") == 0) {
        reboot(RB_POWER_OFF);
    } else if (strcmp(argv[1], "halt") == 0) {
        reboot(RB_HALT_SYSTEM);
    } else if (strcmp(argv[1], "refresh") == 0) {
        if (access("/drive/rootfs.tar", F_OK) != 0) {
            fprintf(stderr, "Couldn\'t refresh packages: rootfs.tar doesn\'t exist\n");
            return 1;
        }

        char *args[] = {"tar", "-xf", "/drive/rootfs.tar", "-C", "/", NULL};
        if (command("/bin/busybox", args) != 0) {
            fprintf(stderr, "eror");
            return 1;
        }

        return 0;
    } else if (strcmp(argv[1], "install") == 0) {
        char *names = pkgnames("/drive/INFO.cfg");
        if (names) {
            char *token = strtok(names, " ");
            while (token) {
                if (strstr(argv[2], token) != NULL) {
                    printf("Package \"%s\" is already installed.\n", token);

                    free(names);
                    return 1;
                }
                token = strtok(NULL, " ");
            }
            free(names);
        }

        printf("Do you really want to install this package? Y/N: ");
        fflush(stdout);

        char c = getch();
        printf("%c\n\n", c);

        if (c == 'y' || c == 'Y') {
            if (merge(argv[2]) != 0) {
                printf("Failed to merge archives.\n");
                return -1;
            }

            if (access("/drive/rootfs.tar", F_OK) != 0) {
                fprintf(stderr, "Couldn\'t refresh packages: rootfs.tar doesn\'t exist\n");
                return 1;
            }

            char *args[] = {"tar", "-xf", "/drive/rootfs.tar", "-C", "/", NULL};
            if (command("/bin/busybox", args) != 0) {
                fprintf(stderr, "eror");
                return 1;
            }

            if (append_file("/INFO.cfg", "/drive/INFO.cfg") != 0) {
                fprintf(stderr, "Failed to merge INFO.cfg\n");
            }

            printf("Installed %s successfully\n\n", pkgnames("/INFO.cfg"));
            unlink("/INFO.cfg");

            return 0;
        }
    } else if (strcmp(argv[1], "remove") == 0) {
        char *files = pkgfiles(argv[2]);

        if (files) {
            printf("Do you really want to remove this package? Y/N: ");
            fflush(stdout);

            char c = getch();
            printf("%c\n\n", c);

            if (c == 'y' || c == 'Y') {
                char *token = strtok(files, " ");
                while (token) {
                    printf("Removing %s...\n", token);
                    unlink(token);
                    token = strtok(NULL, " ");
                }
                free(files);
                printf("Successfully removed %s.", argv[2]);
            }
        } else {
            printf("Package \"%s\" isn\'t installed yet.\n", argv[2]);
            free(files);

            return 1;
        }

        tarrmpkg(argv[2]);
        rmpkgsec(argv[2]);

        return 0;
    } else if (strcmp(argv[1], "list") == 0) {
        char *names = pkgnames("/drive/INFO.cfg");
        if (names) {
            char *token = strtok(names, " ");
            while (token) {
                printf("%s\n", token);
                token = strtok(NULL, " ");
            }
            free(names);
        }

        return 0;
    } else if (strcmp(argv[1], "info") == 0) {
        info();

        return 0;
    } else if (strcmp(argv[1], "noimsurelynota-etc-profile-ipromiseplsmountflashuserohsorryidontwanttomountitmyself") == 0) {
        if (mountdrive() == 0) {
            mkdir("/drive", 0755);

            if (mount("/mnt/drive", "/drive", NULL, MS_BIND, NULL) != 0) {
                if (symlink("/mnt/drive", "/drive") != 0) {
                    printf("\e[1;31m\e[5m[FATAL]\e[0m Couldn\'t mount drive files. You still can mount Flash drive yourself.");
                }
            }
        } else {
            printf("\e[1;31m\e[5m[FATAL]\e[0m Couldn\'t mount drive files. You still can mount Flash drive yourself.");
        }

        return 0;
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(false);
        return 1;
    }

    return 1;
}
