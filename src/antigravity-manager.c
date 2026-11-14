/*
 * Antigravity Manager v1.1 - cross-platform installer/updater for Google's
 * Antigravity tools (App, IDE, CLI "agy", Python SDK).
 *
 * What it does
 *   - Detects what is installed and which version (App, IDE, CLI, SDK).
 *   - Looks up the latest release automatically and shows a status table.
 *   - Installs anything that is missing, updates anything that is outdated.
 *   - Cleans up downloads, staging folders and old backups by itself.
 *   - Rolls back automatically if a new install fails verification.
 *
 * Build
 *   Linux:   cc -std=c11 -O2 -Wall -Wextra antigravity-manager.c -o antigravity-manager
 *   Windows: gcc -std=c11 -O2 -Wall -Wextra antigravity-manager.c -o antigravity-manager.exe
 *
 * Usage
 *   antigravity-manager                 interactive menu
 *   antigravity-manager --check         print status; exit code 10 if something needs attention
 *   antigravity-manager --update ide    update/install one of: app ide cli sdk all
 *   Options: --yes (no prompts) --keep-backup --archive FILE --page-file FILE --help
 *
 * Requires: curl (bundled with Windows 10+ and nearly all Linux distros) and
 * tar (Linux). Nothing is downloaded from hosts other than Google's.
 */

#define _DEFAULT_SOURCE
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"  /* all paths are length-checked by exists()/open */
#endif
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
  #include <windows.h>
  #include <direct.h>
  #include <io.h>
  #define PATH_SEP '\\'
  #define IS_SEP(c) ((c) == '/' || (c) == '\\')
  #define MKDIR(p) _mkdir(p)
  #define POPEN _popen
  #define PCLOSE _pclose
  #define QUOTE '"'
  #define PLAT_A "windows-x64"
  #define PLAT_B "win32-x64"
  #define PKG_EXT ".exe"
#else
  #include <unistd.h>
  #include <dirent.h>
  #define PATH_SEP '/'
  #define IS_SEP(c) ((c) == '/')
  #define MKDIR(p) mkdir((p), 0755)
  #define POPEN popen
  #define PCLOSE pclose
  #define QUOTE '\''
  #define PLAT_A "linux-x64"
  #define PLAT_B "linux-x64"
  #define PKG_EXT ".tar.gz"
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define PAGE_URL "https://antigravity.google/download"
#define SITE_URL "https://antigravity.google/"
#define CLI_SH   "https://antigravity.google/cli/install.sh"
#define CLI_PS   "https://antigravity.google/cli/install.ps1"
#define SDK_PKG  "google-antigravity"

enum { C_APP, C_IDE, C_CLI, C_SDK, C_COUNT };
static const char *C_NAME[C_COUNT] = {
    "Antigravity App", "Antigravity IDE", "Antigravity CLI (agy)", "Antigravity Python SDK"
};
static const char *C_SHORT[C_COUNT] = { "app", "ide", "cli", "sdk" };

typedef struct { int found; char ver[64]; char url[2048]; } Release;
typedef struct {
    int installed;
    char ver[64];      /* installed version, "unknown" if unreadable */
    char latest[64];   /* latest version, "" if not known */
    char path[PATH_MAX];
} Status;

static Release g_rel[2];          /* 0 = App, 1 = IDE */
static int g_scanned = 0;
static int g_yes = 0, g_keep_backup = 0;
static const char *g_page_file = NULL;
static const char *g_archive = NULL;

/* ---------------------------------------------------------------- utils */

static int exists(const char *p) { struct stat st; return stat(p, &st) == 0; }
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && (st.st_mode & S_IFDIR); }

static void trim_newline(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\n' || s[n-1] == '\r' || s[n-1] == ' ')) s[--n] = '\0';
}

static void home_dir(char *out, size_t cap) {
#ifdef _WIN32
    const char *p = getenv("USERPROFILE");
#else
    const char *p = getenv("HOME");
#endif
    snprintf(out, cap, "%.1500s", p ? p : ".");
}

static void tmp_dir(char *out, size_t cap) {
#ifdef _WIN32
    const char *t = getenv("TEMP");
    snprintf(out, cap, "%.1500s\\antigravity-manager", t ? t : ".");
#else
    char h[PATH_MAX];
    home_dir(h, sizeof h);
    snprintf(out, cap, "%s/.cache/antigravity-manager", h);
#endif
}

/* Quote an argument for the platform shell. Anything that could break out of
 * the quotes makes the argument empty, so the command simply fails. */
static char *Q(const char *s) {
    static char bufs[6][PATH_MAX + 8];
    static int slot = 0;
    char *b = bufs[slot];
    slot = (slot + 1) % 6;
    if (strlen(s) >= PATH_MAX || strpbrk(s, "'\"`\r\n")) {
        b[0] = QUOTE; b[1] = QUOTE; b[2] = '\0';
    } else {
        snprintf(b, PATH_MAX + 8, "%c%s%c", QUOTE, s, QUOTE);
    }
    return b;
}

static void wrapcmd(char *out, size_t cap, const char *cmd) {
#ifdef _WIN32
    snprintf(out, cap, "\"%s\"", cmd);   /* cmd /c quote-stripping workaround */
#else
    snprintf(out, cap, "%s", cmd);
#endif
}

static int run(int verbose, const char *fmt, ...) {
    char cmd[16384], full[16400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof cmd, fmt, ap);
    va_end(ap);
    if (verbose) printf("\n> %s\n", cmd);
    fflush(stdout);
    wrapcmd(full, sizeof full, cmd);
    return system(full) == 0;
}

static int capture(const char *cmd, char *out, size_t cap) {
    char full[16400];
    FILE *fp;
    size_t n = 0, r;
    out[0] = '\0';
    wrapcmd(full, sizeof full, cmd);
    fp = POPEN(full, "r");
    if (!fp) return 0;
    while (n + 1 < cap && (r = fread(out + n, 1, cap - 1 - n, fp)) > 0) n += r;
    out[n] = '\0';
    return PCLOSE(fp) == 0;
}

static char *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;
    size_t n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    if (sz < 0 || sz > 20L * 1024 * 1024) { fclose(f); return NULL; }
    buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);
    if (len) *len = n;
    return buf;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    size_t i, n;
    snprintf(tmp, sizeof tmp, "%s", path);
    n = strlen(tmp);
    for (i = 1; i < n; ++i) {
        if (IS_SEP(tmp[i])) { char c = tmp[i]; tmp[i] = '\0'; (void)MKDIR(tmp); tmp[i] = c; }
    }
    (void)MKDIR(tmp);
    return is_dir(tmp);
}

static void remove_tree(const char *path) {
#ifdef _WIN32
    if (is_dir(path)) (void)run(0, "rmdir /s /q %s", Q(path));
    else remove(path);
#else
    struct stat st;
    DIR *d;
    struct dirent *e;
    char child[PATH_MAX];
    if (lstat(path, &st) != 0) return;
    if (!S_ISDIR(st.st_mode)) { remove(path); return; }
    d = opendir(path);
    if (d) {
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (snprintf(child, sizeof child, "%s/%s", path, e->d_name) >= (int)sizeof child) continue;
            remove_tree(child);
        }
        closedir(d);
    }
    rmdir(path);
#endif
}

static void open_url(const char *url) {
#ifdef _WIN32
    (void)run(0, "start \"\" %s", Q(url));
#elif defined(__APPLE__)
    (void)run(0, "open %s", Q(url));
#else
    (void)run(0, "xdg-open %s >/dev/null 2>&1", Q(url));
#endif
}

static int ask(const char *question, int def) {
    char line[32];
    if (g_yes) return def;
    printf("%s [%s]: ", question, def ? "Y/n" : "y/N");
    fflush(stdout);
    if (!fgets(line, sizeof line, stdin)) return 0;
    if (line[0] == '\n' || line[0] == '\r') return def;
    return line[0] == 'y' || line[0] == 'Y';
}

/* ------------------------------------------------------------- versions */

static int extract_version(const char *s, char *out, size_t cap) {
    const char *p;
    for (p = s; *p; ++p) {
        if (isdigit((unsigned char)*p) && (p == s || !isalnum((unsigned char)p[-1]) || p[-1] == 'v')) {
            const char *q = p;
            size_t n = 0;
            int dots = 0;
            while ((isdigit((unsigned char)*q) || *q == '.') && n + 1 < cap) {
                if (*q == '.') dots++;
                out[n++] = *q++;
            }
            while (n && out[n-1] == '.') n--;
            out[n] = '\0';
            if (dots) return 1;
        }
    }
    out[0] = '\0';
    return 0;
}

static int vercmp(const char *a, const char *b) {
    while (*a || *b) {
        char *ea, *eb;
        long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
        if (x != y) return x < y ? -1 : 1;
        if (ea == a && eb == b) break;
        a = ea; b = eb;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }
    return 0;
}

static int json_get_buf(const char *buf, const char *key, char *out, size_t cap) {
    char pat[96];
    const char *p;
    size_t i = 0;
    snprintf(pat, sizeof pat, "\"%s\"", key);
    p = strstr(buf, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (isspace((unsigned char)*p)) p++;
    if (*p != ':') return 0;
    p++;
    while (isspace((unsigned char)*p)) p++;
    if (*p != '"') return 0;
    p++;
    while (*p && *p != '"' && i + 1 < cap) out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
}

static int json_get(const char *path, const char *key, char *out, size_t cap) {
    char *buf = read_file(path, NULL);
    int r;
    if (!buf) return 0;
    r = json_get_buf(buf, key, out, cap);
    free(buf);
    return r;
}

static uint32_t le32(const unsigned char *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/* The Antigravity App is an Electron app: its package.json lives inside
 * resources/app.asar. Read just that one entry without unpacking anything. */
static int asar_version(const char *dir, char *out, size_t cap) {
    char path[PATH_MAX + 32];
    unsigned char h[16];
    char *js = NULL, *pkg = NULL;
    const char *q, *entry = NULL, *end, *sz, *off;
    uint32_t hsize, jlen;
    unsigned long size;
    unsigned long long offset;
    int depth = 1, ok = 0;
    FILE *f;
    snprintf(path, sizeof path, "%s/resources/app.asar", dir);
    f = fopen(path, "rb");
    if (!f) return 0;
    if (fread(h, 1, sizeof h, f) != sizeof h) goto fail;
    hsize = le32(h + 4);
    jlen = le32(h + 12);
    if (jlen == 0 || jlen > 32u * 1024 * 1024) goto fail;
    js = malloc((size_t)jlen + 1);
    if (!js || fread(js, 1, jlen, f) != jlen) goto fail;
    js[jlen] = '\0';
    q = strstr(js, "\"files\"");
    if (!q || !(q = strchr(q + 7, '{'))) goto fail;
    q++;
    while (*q && depth > 0) {          /* find "package.json" among the top-level entries */
        if (*q == '"') {
            const char *k = ++q;
            while (*q && *q != '"') { if (*q == '\\' && q[1]) q++; q++; }
            if (depth == 1 && (size_t)(q - k) == 12 && !strncmp(k, "package.json", 12)) { entry = q; break; }
            if (*q) q++;
        } else if (*q == '{') { depth++; q++; }
        else if (*q == '}') { depth--; q++; }
        else q++;
    }
    if (!entry || !(end = strchr(entry, '}'))) goto fail;
    sz = strstr(entry, "\"size\":");
    off = strstr(entry, "\"offset\":\"");
    if (!sz || !off || sz > end || off > end) goto fail;
    size = strtoul(sz + 7, NULL, 10);
    offset = strtoull(off + 10, NULL, 10);
    if (size == 0 || size > 1024 * 1024) goto fail;
    pkg = malloc(size + 1);
    if (!pkg || fseek(f, (long)(8 + hsize + offset), SEEK_SET) != 0 || fread(pkg, 1, size, f) != size) goto fail;
    pkg[size] = '\0';
    ok = json_get_buf(pkg, "version", out, cap);
fail:
    free(js);
    free(pkg);
    fclose(f);
    return ok;
}

static int looks_installed(const char *dir) {
    char p[PATH_MAX + 40];
    snprintf(p, sizeof p, "%s/resources/app/product.json", dir);
    if (exists(p)) return 1;
    snprintf(p, sizeof p, "%s/resources/app.asar", dir);
    return exists(p);
}

/* --------------------------------------------------- install detection */

static void env_override(int idx, char *out, size_t cap) {
    const char *v = getenv(idx == C_IDE ? "ANTIGRAVITY_IDE_DIR" : "ANTIGRAVITY_APP_DIR");
    snprintf(out, cap, "%.1500s", v ? v : "");
}

static int find_install(int idx, char *out, size_t cap) {
    char home[PATH_MAX], c[8][PATH_MAX];
    int n = 0, i;
    int ide = (idx == C_IDE);
    home_dir(home, sizeof home);
    env_override(idx, c[n], PATH_MAX);
    if (c[n][0]) n++;
#ifdef _WIN32
    {
        const char *la = getenv("LOCALAPPDATA");
        char base[PATH_MAX];
        snprintf(base, sizeof base, "%s", la ? la : home);
        snprintf(c[n++], PATH_MAX, "%s/Programs/%s", base, ide ? "Antigravity IDE" : "Antigravity");
        snprintf(c[n++], PATH_MAX, "%s/Programs/%s", base, ide ? "antigravity-ide" : "antigravity");
    }
#else
    if (ide) {
        snprintf(c[n++], PATH_MAX, "%s/Applications/antigravity-official/Antigravity IDE", home);
        snprintf(c[n++], PATH_MAX, "%s/.local/opt/antigravity-ide", home);
        snprintf(c[n++], PATH_MAX, "/opt/antigravity-ide");
        snprintf(c[n++], PATH_MAX, "/opt/Antigravity IDE");
    } else {
        snprintf(c[n++], PATH_MAX, "%s/Applications/antigravity-app", home);
        snprintf(c[n++], PATH_MAX, "%s/.local/opt/antigravity-app", home);
        snprintf(c[n++], PATH_MAX, "%s/Applications/antigravity-official/Antigravity", home);
        snprintf(c[n++], PATH_MAX, "/opt/Antigravity");
        snprintf(c[n++], PATH_MAX, "/opt/antigravity");
    }
#endif
    for (i = 0; i < n; ++i)
        if (looks_installed(c[i])) { snprintf(out, cap, "%s", c[i]); return 1; }
    return 0;
}

static void default_target(int idx, char *out, size_t cap) {
    char home[PATH_MAX], ov[PATH_MAX];
    env_override(idx, ov, sizeof ov);
    if (ov[0]) { snprintf(out, cap, "%s", ov); return; }
    home_dir(home, sizeof home);
#ifdef _WIN32
    {
        const char *la = getenv("LOCALAPPDATA");
        snprintf(out, cap, "%s/Programs/%s", la ? la : home, idx == C_IDE ? "Antigravity IDE" : "Antigravity");
    }
#else
    snprintf(out, cap, "%s/.local/opt/%s", home, idx == C_IDE ? "antigravity-ide" : "antigravity-app");
#endif
}

static void install_version(const char *dir, char *out, size_t cap) {
    char pj[PATH_MAX + 32];
    snprintf(pj, sizeof pj, "%s/resources/app/product.json", dir);
    if (json_get(pj, "ideVersion", out, cap)) return;
    if (asar_version(dir, out, cap)) return;
    snprintf(out, cap, "unknown");
}

static void venv_dir(char *out, size_t cap) {
    char h[PATH_MAX - 64];
    home_dir(h, sizeof h);
#ifdef _WIN32
    snprintf(out, cap, "%s\\AppData\\Local\\AntigravitySDK-venv", h);
#else
    snprintf(out, cap, "%s/.local/share/antigravity-sdk-venv", h);
#endif
}

static void venv_python(char *out, size_t cap) {
    char v[PATH_MAX - 32];
    venv_dir(v, sizeof v);
#ifdef _WIN32
    snprintf(out, cap, "%s\\Scripts\\python.exe", v);
#else
    snprintf(out, cap, "%s/bin/python", v);
#endif
}

static int cli_version(char *ver, size_t cap) {
    char out[1024], home[PATH_MAX], alt[PATH_MAX];
    home_dir(home, sizeof home);
    if (capture("agy --version 2>&1", out, sizeof out) && extract_version(out, ver, cap)) return 1;
#ifdef _WIN32
    {
        const char *la = getenv("LOCALAPPDATA");
        snprintf(alt, sizeof alt, "%s\\agy\\bin\\agy.exe", la ? la : home);
    }
#else
    snprintf(alt, sizeof alt, "%s/.local/bin/agy", home);
#endif
    if (exists(alt)) {
        char cmd[PATH_MAX + 32];
        snprintf(cmd, sizeof cmd, "%s --version 2>&1", Q(alt));
        if (capture(cmd, out, sizeof out) && extract_version(out, ver, cap)) return 1;
    }
    return 0;
}

/* ------------------------------------------------------ release lookup */

static int url_allowed(const char *url) {
    static const char *hosts[] = { "antigravity.google", "edgedl.me.gvt1.com", "dl.google.com",
                                   "redirector.gvt1.com", "storage.googleapis.com", NULL };
    const char *h, *e;
    size_t len, i;
    if (strncmp(url, "https://", 8) != 0) return 0;
    h = url + 8;
    e = h;
    while (*e && *e != '/' && *e != ':' && *e != '?') e++;
    len = (size_t)(e - h);
    for (i = 0; hosts[i]; ++i)
        if (strlen(hosts[i]) == len && !strncmp(h, hosts[i], len)) {
            if (!strcmp(hosts[i], "storage.googleapis.com"))   /* only Google's public Antigravity bucket */
                return strncmp(e, "/antigravity-public/", 20) == 0;
            return 1;
        }
    return 0;
}

static int contains_ci(const char *s, const char *needle) {
    size_t n = strlen(needle), i, j;
    for (i = 0; s[i]; ++i) {
        for (j = 0; j < n && s[i + j] && tolower((unsigned char)s[i + j]) == needle[j]; ++j) {}
        if (j == n) return 1;
    }
    return 0;
}

static void consider(const char *u, size_t len) {
    char url[2048], ver[64];
    const char *s, *file, *pp;
    size_t i, n;
    int idx;
    if (len == 0 || len >= sizeof url) return;
    memcpy(url, u, len);
    url[len] = '\0';
    if (!strstr(url, "/antigravity")) return;
    pp = strstr(url, "/" PLAT_A "/");
    if (!pp) pp = strstr(url, "/" PLAT_B "/");
    if (!pp) return;
    n = strlen(url);
    if (n < strlen(PKG_EXT) || strcmp(url + n - strlen(PKG_EXT), PKG_EXT) != 0) return;
    s = pp;                              /* version = path component just before the platform */
    while (s > url && s[-1] != '/') s--;
    for (i = 0; s + i < pp && s[i] != '-' && i < sizeof ver - 1; ++i) ver[i] = s[i];
    ver[i] = '\0';
    if (!isdigit((unsigned char)ver[0])) return;
    file = strrchr(url, '/') + 1;
    idx = contains_ci(file, "ide") ? 1 : 0;
    if (!g_rel[idx].found || vercmp(ver, g_rel[idx].ver) > 0) {
        g_rel[idx].found = 1;
        snprintf(g_rel[idx].ver, sizeof g_rel[idx].ver, "%s", ver);
        snprintf(g_rel[idx].url, sizeof g_rel[idx].url, "%s", url);
    }
}

static void scan_text(char *t) {
    static const char *prefixes[] = { "https://edgedl.me.gvt1.com/",
                                      "https://storage.googleapis.com/antigravity-public/", NULL };
    char *r = t, *w = t, *p;
    int k;
    while (*r) { if (r[0] == '\\' && r[1] == '/') r++; *w++ = *r++; }   /* unescape \/ */
    *w = '\0';
    for (k = 0; prefixes[k]; ++k) {
        p = t;
        while ((p = strstr(p, prefixes[k])) != NULL) {
            char *e = p;
            while (*e && !strchr("\"'<> \t\r\n\\)", *e)) e++;
            consider(p, (size_t)(e - p));
            p = (e > p) ? e : p + 1;
        }
    }
}

static char *fetch_text(const char *url) {
    char dir[PATH_MAX], f[PATH_MAX + 32];
    char *buf;
    tmp_dir(dir, sizeof dir);
    mkdir_p(dir);
    snprintf(f, sizeof f, "%s%cfetch.tmp", dir, PATH_SEP);
    if (!run(0, "curl -fsSL --max-time 30 -A \"Mozilla/5.0\" -o %s %s", Q(f), Q(url))) {
        remove(f);
        return NULL;
    }
    buf = read_file(f, NULL);
    remove(f);
    return buf;
}

/* Reads the official download page (and the scripts it references) and
 * extracts the newest Linux/Windows package URLs for the App and the IDE. */
static void ensure_releases(void) {
    char *page;
    char srcs[10][600];
    int ns = 0, i;
    const char *p;
    if (g_scanned) return;
    g_scanned = 1;
    memset(g_rel, 0, sizeof g_rel);
    page = g_page_file ? read_file(g_page_file, NULL) : fetch_text(PAGE_URL);
    if (!page) { puts("  (could not reach Google's download page)"); return; }

    p = page;
    while (ns < 10 && (p = strstr(p, "src=\"")) != NULL) {
        const char *e;
        char tmp[560];
        size_t len;
        p += 5;
        e = strchr(p, '"');
        if (!e) break;
        len = (size_t)(e - p);
        if (len > 0 && len < sizeof tmp && strstr(p, ".js") && strstr(p, ".js") < e) {
            memcpy(tmp, p, len);
            tmp[len] = '\0';
            if (!strncmp(tmp, SITE_URL, strlen(SITE_URL))) snprintf(srcs[ns++], sizeof srcs[0], "%s", tmp);
            else if (tmp[0] == '/' && tmp[1] != '/') snprintf(srcs[ns++], sizeof srcs[0], "https://antigravity.google%s", tmp);
        }
        p = e;
    }
    scan_text(page);
    free(page);
    for (i = 0; i < ns && !(g_rel[0].found && g_rel[1].found); ++i) {
        char *js = g_page_file ? NULL : fetch_text(srcs[i]);
        if (js) { scan_text(js); free(js); }
    }
}

/* ---------------------------------------------------------- status info */

static void gather(int idx, Status *st, int want_latest) {
    char out[2048], cmd[PATH_MAX + 64], py[PATH_MAX];
    memset(st, 0, sizeof *st);
    switch (idx) {
    case C_APP:
    case C_IDE:
        if (find_install(idx, st->path, sizeof st->path)) {
            st->installed = 1;
            install_version(st->path, st->ver, sizeof st->ver);
        }
        if (want_latest) {
            ensure_releases();
            if (g_rel[idx == C_IDE].found) snprintf(st->latest, sizeof st->latest, "%s", g_rel[idx == C_IDE].ver);
        }
        break;
    case C_CLI:
        if (cli_version(st->ver, sizeof st->ver)) st->installed = 1;
        break;
    case C_SDK:
        venv_python(py, sizeof py);
        if (exists(py)) {
            snprintf(cmd, sizeof cmd, "%s -m pip show " SDK_PKG " 2>&1", Q(py));
            if (capture(cmd, out, sizeof out)) {
                char *v = strstr(out, "Version:");
                if (v && extract_version(v + 8, st->ver, sizeof st->ver)) st->installed = 1;
            }
        }
        if (want_latest) {
#ifdef _WIN32
            const char *base = "py";
#else
            const char *base = "python3";
#endif
            if (exists(py)) snprintf(cmd, sizeof cmd, "%s -m pip index versions " SDK_PKG " 2>&1", Q(py));
            else snprintf(cmd, sizeof cmd, "%s -m pip index versions " SDK_PKG " 2>&1", base);
            if (capture(cmd, out, sizeof out)) {
                char *v = strstr(out, SDK_PKG " (");
                if (v) extract_version(v + strlen(SDK_PKG) + 2, st->latest, sizeof st->latest);
            }
        }
        break;
    }
}

/* returns 1 when action is needed; fills a human-readable state */
static int state_of(const Status *s, const char **text) {
    if (!s->installed) { *text = "NOT INSTALLED"; return 1; }
    if (s->latest[0] && strcmp(s->ver, "unknown") != 0) {
        if (vercmp(s->ver, s->latest) < 0) { *text = "UPDATE AVAILABLE"; return 1; }
        *text = "up to date";
        return 0;
    }
    if (!strcmp(s->ver, "unknown")) { *text = "installed (version unreadable)"; return 0; }
    *text = "installed (self-updating)";
    return 0;
}

static int print_status(Status *all) {
    int i, need = 0;
    puts("\nChecking installed versions and latest releases...");
    for (i = 0; i < C_COUNT; ++i) gather(i, &all[i], 1);
    puts("\n  Component                 Installed   Latest      Status");
    puts("  ------------------------  ----------  ----------  ------------------------------");
    for (i = 0; i < C_COUNT; ++i) {
        const char *t;
        need += state_of(&all[i], &t);
        printf("  %-24s  %-10s  %-10s  %s\n", C_NAME[i],
               all[i].installed ? all[i].ver : "-",
               all[i].latest[0] ? all[i].latest : "?", t);
    }
    return need;
}

/* ------------------------------------------------------------- download */

static int download(const char *url, const char *dest) {
    struct stat st;
    if (!url_allowed(url)) { fprintf(stderr, "Refusing to download from an untrusted host:\n%s\n", url); return 0; }
    if (!run(1, "curl -fL --progress-bar -o %s %s", Q(dest), Q(url))) return 0;
    return stat(dest, &st) == 0 && st.st_size > 1024 * 1024;
}

/* kind: 1 = downloaded by us, 0 = supplied at the prompt, -1 = supplied via --archive */
static int acquire(int idx, char *dest, size_t cap, int *kind) {
    char dir[PATH_MAX], in[2048];
    Release *r = &g_rel[idx == C_IDE];
    if (g_archive) {
        snprintf(dest, cap, "%s", g_archive);
        *kind = -1;
        if (!exists(dest)) { fprintf(stderr, "Archive not found: %s\n", dest); return 0; }
        return 1;
    }
    tmp_dir(dir, sizeof dir);
    mkdir_p(dir);
    ensure_releases();
    if (r->found) {
        snprintf(dest, cap, "%s%cantigravity-%s-%s%s", dir, PATH_SEP, idx == C_IDE ? "ide" : "app", r->ver, PKG_EXT);
        printf("Downloading %s %s ...\n", C_NAME[idx], r->ver);
        if (download(r->url, dest)) { *kind = 1; return 1; }
        remove(dest);
        puts("Automatic download failed.");
    } else {
        puts("Could not determine the download link automatically.");
    }
    if (g_yes) return 0;
    open_url(PAGE_URL);
    printf("Opening Google's download page. Paste a direct download URL or the path of a\n"
           "downloaded file (Enter to cancel): ");
    fflush(stdout);
    if (!fgets(in, sizeof in, stdin)) return 0;
    trim_newline(in);
    if (!*in) return 0;
    if (!strncmp(in, "http", 4)) {
        snprintf(dest, cap, "%s%cmanual-download%s", dir, PATH_SEP, PKG_EXT);
        if (!download(in, dest)) { remove(dest); return 0; }
        *kind = 1;
        return 1;
    }
    snprintf(dest, cap, "%s", in);
    *kind = 0;
    if (!exists(dest)) { fprintf(stderr, "File not found: %s\n", dest); return 0; }
    return 1;
}

static void cleanup_download(const char *path, int kind) {
    if (kind == 1) { remove(path); puts("Downloaded file removed."); }
    else if (kind == 0 && ask("Delete the file you supplied?", 1)) { remove(path); puts("File removed."); }
}

/* -------------------------------------------------- App / IDE installers */

#ifndef _WIN32
static int find_binary_dir(const char *dir, const char **names, char *found, size_t cap, int depth) {
    DIR *d;
    struct dirent *e;
    char child[PATH_MAX];
    struct stat st;
    int i;
    for (i = 0; names[i]; ++i) {
        snprintf(child, sizeof child, "%s/%s", dir, names[i]);
        if (stat(child, &st) == 0 && S_ISREG(st.st_mode)) {
            char res[PATH_MAX + 16];
            snprintf(res, sizeof res, "%s/resources", dir);
            if (is_dir(res)) { snprintf(found, cap, "%s", dir); return 1; }
        }
    }
    if (depth <= 0 || !(d = opendir(dir))) return 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof child, "%s/%s", dir, e->d_name);
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode) && find_binary_dir(child, names, found, cap, depth - 1)) {
            closedir(d);
            return 1;
        }
    }
    closedir(d);
    return 0;
}

static int install_package(int idx) {
    static const char *ide_names[] = { "antigravity-ide", NULL };
    static const char *app_names[] = { "antigravity", "Antigravity", NULL };
    const char **names = (idx == C_IDE) ? ide_names : app_names;
    char target[PATH_MAX], parent[PATH_MAX], stage[PATH_MAX + 16], backup[PATH_MAX + 32];
    char legacy[PATH_MAX + 16], archive[PATH_MAX], root[PATH_MAX], home[PATH_MAX], bin[PATH_MAX + 64];
    char oldver[64] = "none", newver[64] = "unknown";
    int kind = 0, ok = 0, had_old = 0, i;
    char *slash;

    home_dir(home, sizeof home);
    if (!find_install(idx, target, sizeof target)) default_target(idx, target, sizeof target);
    had_old = is_dir(target);
    if (had_old) install_version(target, oldver, sizeof oldver);

    snprintf(parent, sizeof parent, "%s", target);
    slash = strrchr(parent, '/');
    if (slash && slash != parent) *slash = '\0';
    if (!mkdir_p(parent) || access(parent, W_OK) != 0) {
        fprintf(stderr, "Cannot write to %s (try running with sudo, or set %s to a folder you own).\n",
                parent, idx == C_IDE ? "ANTIGRAVITY_IDE_DIR" : "ANTIGRAVITY_APP_DIR");
        return 0;
    }
    snprintf(stage, sizeof stage, "%s/.agm-stage", parent);
    snprintf(backup, sizeof backup, "%s.agm-backup", target);
    snprintf(legacy, sizeof legacy, "%s.backup", target);

    if (!acquire(idx, archive, sizeof archive, &kind)) return 0;

    remove_tree(stage);
    if (!mkdir_p(stage)) { fprintf(stderr, "Cannot create staging folder %s\n", stage); goto done; }
    if (!run(1, "tar -xzf %s -C %s", Q(archive), Q(stage))) { fputs("Archive could not be extracted.\n", stderr); goto done; }
    if (!find_binary_dir(stage, names, root, sizeof root, 3)) {
        fprintf(stderr, "No '%s' executable found inside the archive. Nothing was changed.\n", names[0]);
        goto done;
    }

    if (had_old) {
        int running = 0;
        for (i = 0; names[i]; ++i) if (run(0, "pgrep -x %s >/dev/null 2>&1", Q(names[i]))) running = 1;
        if (running && !ask("It looks like Antigravity is running. Replace it anyway? (close it first for safety)", 0)) {
            puts("Skipped: close the application and run again.");
            goto done;
        }
        remove_tree(backup);
        if (rename(target, backup) != 0) { fprintf(stderr, "Could not back up the current install: %s\n", strerror(errno)); goto done; }
    }
    if (rename(root, target) != 0) {
        fprintf(stderr, "Could not move the new version into place: %s\n", strerror(errno));
        if (had_old) rename(backup, target);
        goto done;
    }

    /* verify before throwing the backup away */
    for (i = 0; names[i]; ++i) {
        snprintf(bin, sizeof bin, "%s/%s", target, names[i]);
        if (access(bin, X_OK) == 0) ok = 1;
    }
    if (!ok) {
        fputs("Verification failed (executable missing). Rolling back.\n", stderr);
        remove_tree(target);
        if (had_old) rename(backup, target);
        goto done;
    }
    install_version(target, newver, sizeof newver);
    printf("\n%s: %s -> %s\nInstalled at: %s\n", C_NAME[idx], oldver, newver, target);

    {   /* desktop launcher, only if one does not exist yet */
        char apps[PATH_MAX + 32], desk[PATH_MAX + 64];
        FILE *fp;
        snprintf(apps, sizeof apps, "%s/.local/share/applications", home);
        snprintf(desk, sizeof desk, "%s/%s.desktop", apps, idx == C_IDE ? "antigravity-ide" : "antigravity-app");
        if (!exists(desk) && mkdir_p(apps) && (fp = fopen(desk, "w")) != NULL) {
            fprintf(fp, "[Desktop Entry]\nType=Application\nName=%s\nExec=\"%s/%s\" %%F\nTerminal=false\nCategories=Development;IDE;\n",
                    C_NAME[idx], target, names[0]);
            fclose(fp);
            puts("Created a desktop launcher.");
        }
    }

    if (had_old) {
        if (g_keep_backup) printf("Backup kept at: %s\n", backup);
        else { remove_tree(backup); puts("Old version (backup) removed."); }
    }
    if (is_dir(legacy) && ask("Found an older manual backup next to the install. Delete it?", 0)) {
        remove_tree(legacy);
        puts("Older backup removed.");
    }
    cleanup_download(archive, kind);
    remove_tree(stage);
    return 1;
done:
    remove_tree(stage);
    if (kind == 1) remove(archive);
    return 0;
}
#else
static int install_package(int idx) {
    Status before, after;
    char exe[PATH_MAX];
    int kind = 0, ok;
    gather(idx, &before, 0);
    if (!acquire(idx, exe, sizeof exe, &kind)) return 0;
    puts("Starting the official installer. Close Antigravity first and follow its prompts.");
    ok = run(1, "%s", Q(exe));
    gather(idx, &after, 0);
    printf("\n%s: %s -> %s\n", C_NAME[idx], before.installed ? before.ver : "none",
           after.installed ? after.ver : "not detected");
    if (ok) cleanup_download(exe, kind);
    else if (kind == 1) remove(exe);
    return ok;
}
#endif

/* -------------------------------------------------------- CLI and SDK */

static int update_cli(void) {
    Status before, after;
    int ok = 0;
    gather(C_CLI, &before, 0);
    if (before.installed) {
        puts("Updating with the CLI's own updater (agy update)...");
        ok = run(1, "agy update");
    }
    if (!ok) {
        puts("Running Google's official CLI installer...");
#ifdef _WIN32
        ok = run(1, "powershell -NoProfile -ExecutionPolicy Bypass -Command \"irm '%s' | iex\"", CLI_PS);
#else
        ok = run(1, "bash -c 'set -o pipefail; curl -fsSL %s | bash'", CLI_SH);
#endif
    }
    gather(C_CLI, &after, 0);
    printf("\n%s: %s -> %s\n", C_NAME[C_CLI], before.installed ? before.ver : "none",
           after.installed ? after.ver : "not detected (open a new terminal so PATH refreshes)");
    return ok;
}

static int update_sdk(void) {
    Status before, after;
    char venv[PATH_MAX], py[PATH_MAX];
    int ok = 1;
    gather(C_SDK, &before, 0);
    venv_dir(venv, sizeof venv);
    venv_python(py, sizeof py);
    if (!exists(py)) {
#ifdef _WIN32
        ok = run(1, "py -m venv %s", Q(venv));
#else
        ok = run(1, "python3 -m venv %s", Q(venv));
#endif
    }
    if (ok) ok = run(1, "%s -m pip install --upgrade --quiet " SDK_PKG, Q(py));
    gather(C_SDK, &after, 0);
    printf("\n%s: %s -> %s\n", C_NAME[C_SDK], before.installed ? before.ver : "none",
           after.installed ? after.ver : "not detected");
    return ok;
}

static int update_component(int idx) {
    Status st;
    const char *t;
    if (idx == C_CLI) return update_cli();
    if (idx == C_SDK) return update_sdk();
    gather(idx, &st, 1);
    if (st.installed && !g_archive && !state_of(&st, &t) && st.latest[0]) {
        printf("%s %s is already the latest version.\n", C_NAME[idx], st.ver);
        if (!ask("Reinstall anyway?", 0)) return 1;
    } else if (!st.installed) {
        printf("%s is not installed. Installing it now.\n", C_NAME[idx]);
    } else if (st.latest[0]) {
        printf("Updating %s: %s -> %s\n", C_NAME[idx], st.ver, st.latest);
    }
    return install_package(idx);
}

static int update_needed(Status *all) {
    int i, ok = 1, n = 0;
    for (i = 0; i < C_COUNT; ++i) {
        const char *t;
        if (state_of(&all[i], &t)) n++;
    }
    if (!n) { puts("\nEverything is installed and up to date."); return 1; }
    printf("\n%d component(s) need attention.\n", n);
    if (!ask("Install/update them all now?", 1)) return 1;
    for (i = 0; i < C_COUNT; ++i) {
        const char *t;
        if (!state_of(&all[i], &t)) continue;
        printf("\n=== %s ===\n", C_NAME[i]);
        ok &= update_component(i);
    }
    return ok;
}

/* ----------------------------------------------------------------- main */

static void usage(void) {
    puts("Antigravity Manager v1.1\n\n"
         "  (no arguments)        interactive menu\n"
         "  --check               show status; exit code 10 if something needs attention\n"
         "  --update TARGET       TARGET = app | ide | cli | sdk | all (all = only what needs it)\n"
         "  --yes                 never prompt (safe defaults)\n"
         "  --keep-backup         keep the previous version after a successful update\n"
         "  --archive FILE        install App/IDE from a local file instead of downloading\n"
         "  --page-file FILE      read the download page from a local file (offline/testing)\n"
         "\nEnvironment: ANTIGRAVITY_IDE_DIR / ANTIGRAVITY_APP_DIR override install locations.");
}

static int menu_loop(void) {
    Status all[C_COUNT];
    char line[64];
    for (;;) {
        int choice;
        print_status(all);
        puts("\n========================================");
        puts("       Google Antigravity Manager");
        puts("========================================");
        puts("  1. App      2. IDE      3. CLI      4. Python SDK");
        puts("  5. Install/update everything that needs it");
        puts("  6. Re-check versions");
        puts("  0. Exit");
        printf("\nSelect an option: ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) return 0;
        choice = atoi(line);
        if (choice == 0) { puts("Goodbye."); return 0; }
        if (choice >= 1 && choice <= 4) {
            int ok = update_component(choice - 1);
            puts(ok ? "\nDone." : "\nFailed or cancelled.");
        } else if (choice == 5) {
            puts(update_needed(all) ? "\nDone." : "\nSome steps failed.");
        } else if (choice == 6) {
            g_scanned = 0;
            continue;
        } else {
            puts("Invalid selection.");
            continue;
        }
        printf("\nPress Enter to continue...");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) return 0;
    }
}

int main(int argc, char **argv) {
    const char *update = NULL;
    int check = 0, i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) g_yes = 1;
        else if (!strcmp(argv[i], "--keep-backup")) g_keep_backup = 1;
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--update") && i + 1 < argc) update = argv[++i];
        else if (!strcmp(argv[i], "--archive") && i + 1 < argc) g_archive = argv[++i];
        else if (!strcmp(argv[i], "--page-file") && i + 1 < argc) g_page_file = argv[++i];
        else { usage(); return !strcmp(argv[i], "--help") ? 0 : 2; }
    }
    if (check) {
        Status all[C_COUNT];
        return print_status(all) ? 10 : 0;
    }
    if (update) {
        Status all[C_COUNT];
        int ok = 1;
        if (!strcmp(update, "all")) {
            g_yes = 1;
            print_status(all);
            return update_needed(all) ? 0 : 1;
        }
        for (i = 0; i < C_COUNT; ++i)
            if (!strcmp(update, C_SHORT[i])) { ok = update_component(i); return ok ? 0 : 1; }
        usage();
        return 2;
    }
    return menu_loop();
}
