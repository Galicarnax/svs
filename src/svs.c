#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

typedef unsigned long long uint64;

// ANSI color codes
#define GREEN "\033[0;32m"
#define RED "\033[0;31m"
#define YELLOW "\033[0;33m"
#define GRAY "\033[0;90m"
#define WHITE "\033[0;37m"
#define BLUE "\033[0;34m"
#define CYAN "\033[0;36m"
#define MAGENTA "\033[0;35m"
#define ORANGE "\033[0;38;5;208m"
#define BOLD "\033[1m"
#define ITALIC "\033[3m"
#define NC "\033[0m"

// from runit's sources
#define tai_unix(t, u) ((void)((t)->x = 4611686018427387914ULL + (uint64)(u)))

// From runit's tai.h
struct tai
{
    uint64 x;
};

struct taia
{
    struct tai sec;
    unsigned long nano;
    unsigned long atto;
};

void taia_now(struct taia *t)
{
    struct timeval now;
    gettimeofday(&now, (struct timezone *)0);
    tai_unix(&t->sec, now.tv_sec);
    t->nano = 1000 * now.tv_usec + 500;
    t->atto = 0;
}

void tai_unpack(const char *s, struct tai *t)
{
    uint64 x;

    x = (unsigned char)s[0];
    x <<= 8;
    x += (unsigned char)s[1];
    x <<= 8;
    x += (unsigned char)s[2];
    x <<= 8;
    x += (unsigned char)s[3];
    x <<= 8;
    x += (unsigned char)s[4];
    x <<= 8;
    x += (unsigned char)s[5];
    x <<= 8;
    x += (unsigned char)s[6];
    x <<= 8;
    x += (unsigned char)s[7];
    t->x = x;
}

struct service_status
{
    int state;
    int want;
    int pid;
    int paused;
    int normallyup;
    struct tai tstatus;
};

int compare_entries(const void *a, const void *b)
{
    return strcmp(((struct dirent *)a)->d_name, ((struct dirent *)b)->d_name);
}

// return 0 if service is running,
// 1 if service is down and wants up
// -1 if error (broken symlink, non-existing run, ...)
int service_status_short(const char *service_dir)
{
    char status_path[256];
    snprintf(status_path, sizeof(status_path), "%s/supervise/status", service_dir);
    struct stat s;
    if (stat(status_path, &s) == -1)
    {
        return -1;
    }
    if (s.st_size != 20)
    {
        return -1;
    }
    FILE *fp = fopen(status_path, "rb");
    if (!fp)
    {
        return -1;
    }

    char svstatus[20];
    size_t nread = fread(svstatus, 1, 20, fp);
    fclose(fp);
    if (nread != 20)
    {
        return -1;
    }

    int state = svstatus[19];
    int want = svstatus[17];

    if (state == 0 && want == 'u')
    {
        return 1;
    }
    else if (state == 1 || state == 2)
    {
        return 0;
    }
    return -1;
}

struct service_status get_service_status(const char *service_dir)
{
    char status_path[256];
    snprintf(status_path, sizeof(status_path), "%s/supervise/status", service_dir);
    struct stat s;
    if (stat(status_path, &s) == -1)
    {
        struct service_status status;
        status.state = -1; // Error
        return status;
    }
    if (s.st_size != 20)
    {
        struct service_status status;
        status.state = -2; // Error
        return status;
    }
    struct service_status status;
    char path[256];
    snprintf(path, sizeof(path), "%s/supervise/status", service_dir);
    FILE *fp = fopen(path, "rb");
    if (!fp)
    {
        status.state = -1; // Error
        return status;
    }

    char svstatus[20];
    size_t nread = fread(svstatus, 1, 20, fp);
    fclose(fp);
    if (nread != 20)
    {
        status.state = -1; // Error
        return status;
    }

    int pid = (unsigned char)svstatus[15];
    pid <<= 8;
    pid += (unsigned char)svstatus[14];
    pid <<= 8;
    pid += (unsigned char)svstatus[13];
    pid <<= 8;
    pid += (unsigned char)svstatus[12];
    status.pid = pid;

    tai_unpack(svstatus, &status.tstatus);

    status.state = svstatus[19];
    status.want = svstatus[17];
    status.paused = svstatus[16];

    // if `down` file exists, set normallyup to 0
    snprintf(path, sizeof(path), "%s/down", service_dir);
    if (stat(path, &s) == -1)
    {
        if (errno == ENOENT)
        {
            status.normallyup = 1; // Service is normally up
        }
        else
        {
            fprintf(stderr, "warn: unable to stat %s/down: %s\n", service_dir, strerror(errno));
            status.state = -1; // Error
        }
    }
    else
    {
        status.normallyup = 0; // Service is normally down
    }

    return status;
}

// get status indicator, given service_status struct
char *get_status_indicator(struct service_status S)
{
    if (S.state == 1)
    {
        if (S.paused)
            return ORANGE "⏸" NC;
        else if (S.want == 'd')
            return ORANGE "▼" NC;
        else
            return GREEN "✔" NC;
    }
    else if (S.state == 0)
    {
        if (S.want == 'u')
            return RED "✘" NC;
        else
            return ORANGE "■" NC;
    }
    else if (S.state == 2)
    {
        if (S.paused)
            return MAGENTA "⏸" NC;
        else if (S.want == 'd')
            return MAGENTA "▼" NC;
        else
            return MAGENTA "▽" NC;
    }
    else
    {
        return RED "?" NC;
    }
}

// Time delta formatting
void format_time(unsigned long delta, char *out, size_t out_size)
{
    if (delta < 60)
    {
        snprintf(out, out_size, "%lus", delta);
    }
    else if (delta < 3600)
    {
        snprintf(out, out_size, "%lum", delta / 60);
    }
    else if (delta < 86400)
    {
        snprintf(out, out_size, "%luh", delta / 3600);
    }
    else if (delta < 604800)
    {
        snprintf(out, out_size, "%lud", delta / 86400);
    }
    else if (delta < 2419200)
    {
        snprintf(out, out_size, "%luw", delta / 604800);
    }
    else if (delta < 29030400)
    {
        snprintf(out, out_size, "%luM", delta / 2419200);
    }
    else
    {
        snprintf(out, out_size, "%luy", delta / 29030400);
    }
}

// Get username from PID using /proc/<pid>/stat
int get_username_from_pid(int pid, char *user, size_t user_size)
{

    char proc_path[64];
    snprintf(proc_path, sizeof(proc_path), "/proc/%d/status", pid);
    FILE *fp = fopen(proc_path, "r");
    if (!fp)
    {
        strncpy(user, "?", user_size);
        return -1;
    }

    uid_t uid = (uid_t)-1;
    char line[256];
    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "Uid:", 4) == 0)
        {
            int ruid, euid, suid, fsuid;
            if (sscanf(line, "Uid:\t%d\t%d\t%d\t%d", &ruid, &euid, &suid, &fsuid) == 4)
            {
                uid = ruid; // Real UID
            }
            break;
        }
    }
    fclose(fp);

    if (uid == (uid_t)-1)
    {
        strncpy(user, "@", user_size);
        return -1;
    }

    struct passwd *pw = getpwuid(uid);
    if (!pw)
    {
        strncpy(user, "!", user_size);
        return -1;
    }
    strncpy(user, pw->pw_name, user_size);
    user[user_size - 1] = '\0';
    return 0;
}

int check_services(DIR *dir, char *svdir)
{
    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        if (entry->d_type == DT_DIR || entry->d_type == DT_LNK)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            char service_dir[256];
            snprintf(service_dir, sizeof(service_dir), "%s/%s", svdir, entry->d_name);
            int status = service_status_short(service_dir);
            if (status > 0)
            {
                return 1;
            }
        }
    }
    return 0;
}

int print_status(DIR *dir, char *svdir)
{
    // Find longest service name
    int max_length = 7; // "SERVICE"
    struct dirent *entry;
    int entry_count = 0;
    while ((entry = readdir(dir)))
    {
        if (entry->d_type == DT_DIR || entry->d_type == DT_LNK)
        {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            entry_count++;
            int len = strlen(entry->d_name);
            if (len > max_length)
                max_length = len;
        }
    }
    rewinddir(dir);
    max_length++;

    // Allocate array for entries
    struct dirent *entries = malloc(entry_count * sizeof(struct dirent));
    if (!entries)
    {
        fprintf(stderr, "Error: Memory allocation failed\n");
        closedir(dir);
        return 1;
    }
    int valid_entries = 0;

    // Collect entries
    while ((entry = readdir(dir)))
    {
        if (entry->d_type == DT_DIR || entry->d_type == DT_LNK)
        {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            {
                memcpy(&entries[valid_entries], entry, sizeof(struct dirent));
                int len = strlen(entries[valid_entries].d_name);
                if (len > max_length)
                    max_length = len;
                valid_entries++;
            }
        }
    }

    // sort alphabetically
    qsort(entries, valid_entries, sizeof(struct dirent), compare_entries);

    // Print header
    printf(BOLD "   %-*s A  %-6s %-10s %-6s LOG" NC "\n", max_length, "SERVICE", "PID", "USER", "TIME");

    char service_dir[256];
    struct taia tnow;
    taia_now(&tnow);

    for (int i = 0; i < valid_entries; i++)
    {
        entry = &entries[i];
        if (entry->d_type != DT_DIR && entry->d_type != DT_LNK)
            continue;

        snprintf(service_dir, sizeof(service_dir), "%s/%s", svdir, entry->d_name);

        struct service_status S = get_service_status(service_dir);

        if (S.state < 0)
        {
            printf(RED " ?" NC ITALIC " %-*s " GRAY "-  %-6s %-10s %-6s %-3s\n" NC, max_length, entry->d_name, "---",
                   "---", "--", "-");
            continue;
        }

        unsigned long delta = (tnow.sec.x < S.tstatus.x) ? 0 : (tnow.sec.x - S.tstatus.x);

        const char *state_fmt = get_status_indicator(S);

        const char *autostart = S.normallyup ? YELLOW "+" NC : GRAY "-" NC;

        // PID and User
        char pid_str[6] = "---";
        char user[16] = "---";
        if (S.pid && (S.state == 1 || S.state == 2))
        {
            snprintf(pid_str, sizeof(pid_str), "%d", S.pid);
            if (get_username_from_pid(S.pid, user, sizeof(user)) != 0)
            {
                strcpy(user, "***");
            }
            if (strlen(user) > 9 && strlen(user) != 10)
            {
                user[9] = '~';
                user[10] = '\0';
            }
        }

        // Time formatting
        char time_str[16];
        format_time(delta, time_str, sizeof(time_str));
        const char *time_fmt;
        if (delta < 60)
            time_fmt = YELLOW;
        else if (delta < 300)
            time_fmt = CYAN;
        else if (delta < 3600)
            time_fmt = WHITE;
        else
            time_fmt = GRAY;

        // check status of log service
        char log_dir[261];
        snprintf(log_dir, sizeof(log_dir), "%s/log", service_dir);
        int has_log = (access(log_dir, F_OK) == 0);
        char log_fmt[22];
        if (has_log)
        {
            S = get_service_status(log_dir);

            strncpy(log_fmt, get_status_indicator(S), sizeof(log_fmt));
            log_fmt[sizeof(log_fmt) - 1] = '\0';

            // if log service normally down, append ` -`
            if (S.normallyup == 0)
                strcat(log_fmt, " -");
        }
        else
        {
            strcpy(log_fmt, GRAY "-" NC);
        }

        printf(" %s %-*s %s " MAGENTA " %-6s " BLUE "%-10s " NC "%s%-6s %-3s\n", state_fmt, max_length, entry->d_name,
               autostart, pid_str, user, time_fmt, time_str, log_fmt);
    }

    free(entries);
    return 0;
}

int main(int argc, char *argv[])
{
    int opt;
    int quiet_mode = 0;
    char *svdir = NULL;
    while ((opt = getopt(argc, argv, "qd:")) != -1)
    {
        switch (opt)
        {
        case 'q':
            quiet_mode = 1;
            break;
        case 'd':
            svdir = optarg;
            break;
        default:
            fprintf(stderr, "Unknown option: -%c\n", optopt);
            return 1;
        }
    }

    if (!svdir)
    {
        svdir = getenv("SVDIR");
        if (!svdir)
        {
            svdir = "/var/service";
        }
    }
    if (svdir[strlen(svdir) - 1] == '/')
    {
        svdir[strlen(svdir) - 1] = '\0';
    }

    DIR *dir = opendir(svdir);
    if (!dir)
    {
        fprintf(stderr, "Error: Cannot open %s: %s\n", svdir, strerror(errno));
        return 1;
    }

    if (quiet_mode)
    {
        if (check_services(dir, svdir) == 1)
        {
            closedir(dir);
            return 1;
        }
    }
    else
    {
        if (print_status(dir, svdir) == 1)
        {
            closedir(dir);
            return 1;
        }
    }

    closedir(dir);
    return 0;
}
