/*
 * rdr2mod.c — RDR2 修改器安装器（Windows x64 / SteamOS Linux x64）
 * 单文件；修改器负载以自定义容器追加在可执行文件尾部。
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <tlhelp32.h>
#  include <direct.h>
#  include <io.h>
#  include <sys/stat.h>
#  define MKDIR1(p) _mkdir(p)
#  define RMDIR1(p) _rmdir(p)
#  define SEP "\\"
#else
#  include <unistd.h>
#  include <dirent.h>
#  include <limits.h>
#  include <sys/stat.h>
#  include <sys/types.h>
#  define MKDIR1(p) mkdir((p), 0755)
#  define RMDIR1(p) rmdir(p)
#  define SEP "/"
#endif

#define MAXP 4096
#define APPID "1174180"
#define GAMEDIRNAME "Red Dead Redemption 2"
#define LAUNCHVAL "WINEDLLOVERRIDES=\"dinput8,ScriptHookRDR2=n,b\" %command%"
#define LAUNCHMARK "WINEDLLOVERRIDES"

static FILE *g_log;
static int g_yes;
static int g_no_vdf;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (g_log) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
}

static char *xdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

static char *join(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char *)malloc(la + lb + 2);
    if (!p) return NULL;
    memcpy(p, a, la);
    p[la] = SEP[0];
    memcpy(p + la + 1, b, lb + 1);
    return p;
}

static int f_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static int is_dir(const char *p)
{
    struct stat st;
    if (stat(p, &st) != 0) return 0;
#ifdef _WIN32
    return (st.st_mode & _S_IFDIR) != 0;
#else
    return S_ISDIR(st.st_mode);
#endif
}

static long long fsize(const char *p)
{
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    return (long long)st.st_size;
}

static long long fmtime(const char *p)
{
    struct stat st;
    if (stat(p, &st) != 0) return -1;
    return (long long)st.st_mtime;
}

static int mkdir_p(const char *path)
{
    char tmp[MAXP];
    size_t n = strlen(path), i;
    if (n == 0 || n >= MAXP) return -1;
    memcpy(tmp, path, n + 1);
    for (i = 1; i < n; i++) {
        char c = tmp[i];
        if (c == '/' || c == '\\') {
#ifdef _WIN32
            if (i == 2 && tmp[1] == ':') continue;
#endif
            tmp[i] = 0;
            if (MKDIR1(tmp) != 0 && errno != EEXIST) return -1;
            tmp[i] = c;
        }
    }
    if (MKDIR1(tmp) != 0 && errno != EEXIST) return -1;
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *fi, *fo;
    char buf[65536];
    size_t n;
    fi = fopen(src, "rb");
    if (!fi) return -1;
    fo = fopen(dst, "wb");
    if (!fo) { fclose(fi); return -1; }
    while ((n = fread(buf, 1, sizeof buf, fi)) > 0) {
        if (fwrite(buf, 1, n, fo) != n) { fclose(fi); fclose(fo); return -1; }
    }
    fclose(fi);
    fclose(fo);
    return 0;
}

typedef void (*dir_cb)(const char *name, int isdir, void *ud);

static int for_each_entry(const char *dir, dir_cb cb, void *ud)
{
#ifdef _WIN32
    char pat[MAXP];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(pat, sizeof pat, "%s\\*", dir);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        if (!strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, "..")) continue;
        cb(fd.cFileName, (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0, ud);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        char full[MAXP];
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
        if (stat(full, &st) != 0) continue;
        cb(e->d_name, S_ISDIR(st.st_mode) ? 1 : 0, ud);
    }
    closedir(d);
    return 0;
#endif
}

struct collect {
    char *nm[1024];
    int d[1024];
    int n;
};

static void collect_cb(const char *name, int isd, void *ud)
{
    struct collect *c = (struct collect *)ud;
    if (c->n < 1024) {
        c->nm[c->n] = xdup(name);
        c->d[c->n] = isd;
        c->n++;
    }
}

static void collect_free(struct collect *c)
{
    int i;
    for (i = 0; i < c->n; i++) free(c->nm[i]);
    c->n = 0;
}

static int rm_rf(const char *path)
{
    struct collect c;
    int i;
    if (!f_exists(path)) return 0;
    if (!is_dir(path)) return remove(path) == 0 ? 0 : -1;
    memset(&c, 0, sizeof c);
    for_each_entry(path, collect_cb, &c);
    for (i = 0; i < c.n; i++) {
        char *full = join(path, c.nm[i]);
        if (!full) continue;
        if (c.d[i]) rm_rf(full);
        else remove(full);
        free(full);
    }
    collect_free(&c);
    RMDIR1(path);
    return 0;
}

static int copy_tree(const char *src, const char *dst)
{
    struct collect c;
    int i, err = 0;
    if (!is_dir(src)) return -1;
    if (mkdir_p(dst) != 0) return -1;
    memset(&c, 0, sizeof c);
    for_each_entry(src, collect_cb, &c);
    for (i = 0; i < c.n; i++) {
        char *s = join(src, c.nm[i]);
        char *d = join(dst, c.nm[i]);
        if (s && d) {
            if (c.d[i]) { if (copy_tree(s, d) != 0) err = -1; }
            else if (copy_file(s, d) != 0) err = -1;
        }
        free(s);
        free(d);
    }
    collect_free(&c);
    return err;
}

/* ---------------- 环境 ---------------- */

static int steam_running(void)
{
#ifdef _WIN32
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe;
    int found = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    pe.dwSize = sizeof pe;
    if (Process32First(h, &pe)) {
        do {
            if (!_stricmp(pe.szExeFile, "steam.exe")) { found = 1; break; }
        } while (Process32Next(h, &pe));
    }
    CloseHandle(h);
    return found;
#else
    DIR *d = opendir("/proc");
    struct dirent *e;
    int found = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        char p[MAXP], buf[128];
        FILE *f;
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        snprintf(p, sizeof p, "/proc/%s/comm", e->d_name);
        f = fopen(p, "r");
        if (!f) continue;
        if (fgets(buf, sizeof buf, f)) {
            size_t n = strlen(buf);
            while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
            if (!strcmp(buf, "steam") || !strcmp(buf, "steamwebhelper")) found = 1;
        }
        fclose(f);
        if (found) break;
    }
    closedir(d);
    return found;
#endif
}

static char *home_dir(void)
{
    const char *env = getenv("RDR2MOD_HOME");
    if (env && *env) return xdup(env);
#ifdef _WIN32
    {
        char buf[MAXP];
        DWORD n = GetEnvironmentVariableA("USERPROFILE", buf, sizeof buf);
        if (n > 0 && n < sizeof buf) return xdup(buf);
    }
    return xdup("C:\\");
#else
    env = getenv("HOME");
    if (env && *env) return xdup(env);
    return xdup("/home/deck");
#endif
}

static char *self_path(void)
{
    char buf[MAXP];
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, buf, sizeof buf);
    if (n == 0 || n >= sizeof buf) return NULL;
    return xdup(buf);
#else
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL;
    buf[n] = 0;
    return xdup(buf);
#endif
}

/* ---------------- 负载容器 ---------------- */

typedef struct {
    unsigned char *data;
    size_t len;
    unsigned int count;
    size_t pos;
} pkg_t;

static unsigned int rd32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned int rd16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}
static unsigned long long rd64(const unsigned char *p)
{
    unsigned long long v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static int pkg_open(pkg_t *k)
{
    char *sp = self_path();
    FILE *f;
    unsigned char tr[24];
    unsigned long long plen, poff;
    long total;
    memset(k, 0, sizeof *k);
    if (!sp) return -1;
    f = fopen(sp, "rb");
    free(sp);
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    total = ftell(f);
    if (total < 32) { fclose(f); return -1; }
    if (fseek(f, total - 24, SEEK_SET) != 0) { fclose(f); return -1; }
    if (fread(tr, 1, 24, f) != 24) { fclose(f); return -1; }
    if (memcmp(tr + 16, "RDR2PKG1", 8) != 0) { fclose(f); return -1; }
    plen = rd64(tr);
    poff = rd64(tr + 8);
    if (plen == 0 || poff + plen > (unsigned long long)total) { fclose(f); return -1; }
    if (fseek(f, (long)poff, SEEK_SET) != 0) { fclose(f); return -1; }
    k->data = (unsigned char *)malloc((size_t)plen);
    if (!k->data) { fclose(f); return -1; }
    if (fread(k->data, 1, (size_t)plen, f) != (size_t)plen) {
        free(k->data);
        k->data = NULL;
        fclose(f);
        return -1;
    }
    fclose(f);
    k->len = (size_t)plen;
    if (k->len < 4) return -1;
    k->count = rd32(k->data);
    k->pos = 4;
    return 0;
}

static int pkg_next(pkg_t *k, char *name, size_t namesz, const unsigned char **body, size_t *blen)
{
    unsigned int nl;
    unsigned long long sz;
    if (k->pos + 2 > k->len) return 0;
    nl = rd16(k->data + k->pos);
    k->pos += 2;
    if (k->pos + nl + 8 > k->len) return 0;
    if ((size_t)nl + 1 > namesz) return 0;
    memcpy(name, k->data + k->pos, nl);
    name[nl] = 0;
    k->pos += nl;
    sz = rd64(k->data + k->pos);
    k->pos += 8;
    if (k->pos + (size_t)sz > k->len) return 0;
    *body = k->data + k->pos;
    *blen = (size_t)sz;
    k->pos += (size_t)sz;
    return 1;
}

/* 判断磁盘上的文件是否就是内置负载里的那一个（避免把自己的文件当"原版"备份） */
static int payload_same(const char *relname, const char *filepath)
{
    pkg_t k;
    char name[1024];
    const unsigned char *body;
    size_t bl;
    int same = 0;
    long long sz = fsize(filepath);
    if (sz < 0) return 0;
    if (pkg_open(&k) != 0) return 0;
    while (pkg_next(&k, name, sizeof name, &body, &bl)) {
        if (!strcmp(name, relname)) {
            if ((long long)bl == sz) {
                FILE *f = fopen(filepath, "rb");
                if (f) {
                    unsigned char *tmp = (unsigned char *)malloc(bl ? bl : 1);
                    if (tmp && fread(tmp, 1, bl, f) == bl && memcmp(tmp, body, bl) == 0) same = 1;
                    free(tmp);
                    fclose(f);
                }
            }
            break;
        }
    }
    free(k.data);
    return same;
}

/* ---------------- Steam 库 ---------------- */

static char **g_roots;
static int g_nroots;

static void add_root(const char *r)
{
    int i;
    char *d;
    if (!r || !*r) return;
#ifndef _WIN32
    for (i = 0; i < g_nroots; i++) if (!strcmp(g_roots[i], r)) return;
    d = xdup(r);
#else
    d = xdup(r);
    if (!d) return;
    {
        char *p;
        for (p = d; *p; p++) if (*p == '/') *p = '\\';
        while (strlen(d) > 3 && d[strlen(d) - 1] == '\\') d[strlen(d) - 1] = 0;
    }
    for (i = 0; i < g_nroots; i++) if (!strcmp(g_roots[i], d)) { free(d); return; }
#endif
    if (!d) return;
    g_roots = (char **)realloc(g_roots, sizeof(char *) * (g_nroots + 1));
    if (!g_roots) { free(d); return; }
    g_roots[g_nroots++] = d;
}

static void parse_libraryfolders(const char *steamroot)
{
    char p[MAXP];
    char line[8192];
    FILE *f;
    snprintf(p, sizeof p, "%s%ssteamapps%slibraryfolders.vdf", steamroot, SEP, SEP);
    f = fopen(p, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *q = strstr(line, "\"path\"");
        char *a, *b;
        if (!q) {
            q = strstr(line, "\"Path\"");
            if (!q) continue;
        }
        a = strchr(q + 6, '"');
        if (!a) continue;
        a++;
        b = strchr(a, '"');
        if (!b) continue;
        *b = 0;
        {
            /* 反转义 \\ -> \ */
            char *r = a, *w = a;
            while (*r) {
                if (r[0] == '\\' && r[1] == '\\') { *w++ = '\\'; r += 2; }
                else *w++ = *r++;
            }
            *w = 0;
        }
        add_root(a);
    }
    fclose(f);
}

static void discover_steam_roots(void)
{
    char *h = home_dir();
    char buf[MAXP];
    int i, base;
#ifndef _WIN32
    snprintf(buf, sizeof buf, "%s/.steam/steam", h);
    add_root(buf);
    snprintf(buf, sizeof buf, "%s/.local/share/Steam", h);
    add_root(buf);
    snprintf(buf, sizeof buf, "%s/.var/app/com.valvesoftware.Steam/data/Steam", h);
    add_root(buf);
    {
        const char *env = getenv("RDR2MOD_STEAM");
        if (env && *env) add_root(env);
    }
#else
    for (i = 'A'; i <= 'Z'; i++) {
        static const char *suf[] = { ":\\Steam", ":\\Program Files (x86)\\Steam", ":\\Program Files\\Steam",
                                     ":\\SteamLibrary", ":\\Games\\Steam" };
        int j;
        for (j = 0; j < 5; j++) {
            snprintf(buf, sizeof buf, "%c%s", i, suf[j]);
            if (f_exists(buf)) add_root(buf);
        }
    }
    {
        const char *env = getenv("RDR2MOD_STEAM");
        if (env && *env) add_root(env);
    }
#endif
    free(h);
    base = g_nroots;
    for (i = 0; i < base; i++) parse_libraryfolders(g_roots[i]);
    base = g_nroots;
    for (i = 0; i < base; i++) parse_libraryfolders(g_roots[i]);
}

static char *find_game_dir(void)
{
    int i;
    for (i = 0; i < g_nroots; i++) {
        char *c = join(g_roots[i], "steamapps");
        char *d = c ? join(c, "common") : NULL;
        char *g = d ? join(d, GAMEDIRNAME) : NULL;
        char *exe = g ? join(g, "RDR2.exe") : NULL;
        int ok = (exe && f_exists(exe));
        free(exe);
        free(c);
        free(d);
        if (ok) return g;
        free(g);
    }
    return NULL;
}

static char *find_localconfig(void)
{
    int i;
    char *best = NULL;
    long long bestt = -1;
    for (i = 0; i < g_nroots; i++) {
        char *ud = join(g_roots[i], "userdata");
        struct collect c;
        int j;
        if (!ud) continue;
        memset(&c, 0, sizeof c);
        for_each_entry(ud, collect_cb, &c);
        for (j = 0; j < c.n; j++) {
            if (!c.d[j]) continue;
            {
                char *u = join(ud, c.nm[j]);
                char *cf = u ? join(u, "config") : NULL;
                char *vdf = cf ? join(cf, "localconfig.vdf") : NULL;
                if (vdf && f_exists(vdf)) {
                    long long t = fmtime(vdf);
                    if (t > bestt) { bestt = t; free(best); best = xdup(vdf); }
                }
                free(u);
                free(cf);
                free(vdf);
            }
        }
        collect_free(&c);
        free(ud);
    }
    return best;
}

static char *find_proton_saves(void)
{
    int i;
    for (i = 0; i < g_nroots; i++) {
        char *p[MAXP];
        int k;
        char *cur = xdup(g_roots[i]);
        static const char *cmp[] = { "steamapps", "compatdata", APPID, "pfx", "drive_c", "users", "steamuser",
                                     "Documents", "Rockstar Games", "RDR2" };
        int ok = 1;
        for (k = 0; k < 10; k++) {
            char *nx = cur ? join(cur, cmp[k]) : NULL;
            free(cur);
            cur = nx;
            if (!cur) { ok = 0; break; }
        }
        (void)p;
        if (ok && is_dir(cur)) { return cur; }
        free(cur);
    }
    return NULL;
}

static char *find_windows_saves(void)
{
#ifdef _WIN32
    char *h = home_dir();
    char *d = join(h, "Documents");
    char *r = d ? join(d, "Rockstar Games") : NULL;
    char *g = r ? join(r, "RDR2") : NULL;
    char *ret = (g && is_dir(g)) ? xdup(g) : NULL;
    free(h);
    free(d);
    free(r);
    free(g);
    return ret;
#else
    return NULL;
#endif
}

/* ---------------- VDF ---------------- */

static const char *sws(const char *p, const char *e)
{
    while (p < e) {
        char c = *p;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { p++; continue; }
        if (c == '/' && p + 1 < e && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
        break;
    }
    return p;
}

static const char *match_close(const char *p, const char *e)
{
    int depth = 1, inq = 0;
    while (p < e) {
        if (inq) {
            if (*p == '\\') { p += 2; continue; }
            if (*p == '"') inq = 0;
        } else {
            if (*p == '"') inq = 1;
            else if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (depth == 0) return p; }
        }
        p++;
    }
    return NULL;
}

static int kv_block(const char *b, const char *e, const char *key, const char **bs, const char **be)
{
    const char *p = b;
    while (p < e) {
        char tok[512];
        int ti = 0, esc = 0;
        p = sws(p, e);
        if (p >= e || *p == '}') break;
        if (*p != '"') { p++; continue; }
        p++;
        while (p < e) {
            if (esc) { if (ti < 511) tok[ti++] = *p; esc = 0; p++; continue; }
            if (*p == '\\') { esc = 1; p++; continue; }
            if (*p == '"') break;
            if (ti < 511) tok[ti++] = *p;
            p++;
        }
        tok[ti] = 0;
        if (p >= e) break;
        p++;
        p = sws(p, e);
        if (p < e && *p == '{') {
            const char *inner = p + 1;
            const char *cl = match_close(inner, e);
            if (!cl) break;
            if (!strcmp(tok, key)) { *bs = inner; *be = cl; return 1; }
            p = cl + 1;
        } else if (p < e && *p == '"') {
            p++;
            while (p < e) {
                if (*p == '\\') { p += 2; continue; }
                if (*p == '"') { p++; break; }
                p++;
            }
        }
    }
    return 0;
}

static int kv_scalar(const char *b, const char *e, const char *key, const char **vs, const char **ve)
{
    const char *p = b;
    while (p < e) {
        char tok[512];
        int ti = 0, esc = 0;
        p = sws(p, e);
        if (p >= e || *p == '}') break;
        if (*p != '"') { p++; continue; }
        p++;
        while (p < e) {
            if (esc) { if (ti < 511) tok[ti++] = *p; esc = 0; p++; continue; }
            if (*p == '\\') { esc = 1; p++; continue; }
            if (*p == '"') break;
            if (ti < 511) tok[ti++] = *p;
            p++;
        }
        tok[ti] = 0;
        if (p >= e) break;
        p++;
        p = sws(p, e);
        if (p < e && *p == '{') {
            const char *cl = match_close(p + 1, e);
            if (!cl) break;
            p = cl + 1;
            continue;
        }
        if (p < e && *p == '"') {
            const char *v1 = p + 1, *q = v1;
            while (q < e) {
                if (*q == '\\') { q += 2; continue; }
                if (*q == '"') break;
                q++;
            }
            if (q >= e) break;
            if (!strcmp(tok, key)) { *vs = v1; *ve = q; return 1; }
            p = q + 1;
            continue;
        }
    }
    return 0;
}

static char *splice(const char *buf, size_t len, size_t at, const char *ins, size_t ilen, size_t *outlen)
{
    char *n = (char *)malloc(len + ilen + 1);
    if (!n) return NULL;
    memcpy(n, buf, at);
    memcpy(n + at, ins, ilen);
    memcpy(n + at + ilen, buf + at, len - at);
    n[len + ilen] = 0;
    *outlen = len + ilen;
    return n;
}

static char *vdf_escape(const char *s)
{
    size_t n = strlen(s), i, j = 0;
    char *o = (char *)malloc(n * 2 + 1);
    if (!o) return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '"' || s[i] == '\\') o[j++] = '\\';
        o[j++] = s[i];
    }
    o[j] = 0;
    return o;
}

static char *read_all(const char *p, size_t *len)
{
    FILE *f = fopen(p, "rb");
    char *buf;
    long sz;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return NULL; }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    buf[sz] = 0;
    fclose(f);
    *len = (size_t)sz;
    return buf;
}

static int write_all(const char *p, const char *buf, size_t len)
{
    FILE *f = fopen(p, "wb");
    if (!f) return -1;
    if (fwrite(buf, 1, len, f) != len) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

/* 把 1174180 的 LaunchOptions 设成 val */
static char *vdf_set_launch(char *buf, size_t *lenp, const char *val, const char *eol, char *err, size_t en)
{
    const char *b = buf, *e = buf + *lenp;
    const char *a1, *a2, *b1, *b2, *c1, *c2, *d1, *d2, *apps, *appse, *ab, *ae;
    char *esc = vdf_escape(val);
    if (!esc) return NULL;
    if (!kv_block(b, e, "UserLocalConfigStore", &a1, &a2)) { snprintf(err, en, "找不到 UserLocalConfigStore"); goto done; }
    if (!kv_block(a1, a2, "Software", &b1, &b2)) { snprintf(err, en, "找不到 Software"); goto done; }
    if (!kv_block(b1, b2, "Valve", &c1, &c2)) { snprintf(err, en, "找不到 Valve"); goto done; }
    if (!kv_block(c1, c2, "Steam", &d1, &d2)) { snprintf(err, en, "找不到 Steam"); goto done; }
    if (!kv_block(d1, d2, "apps", &apps, &appse)) { snprintf(err, en, "找不到 apps 节点"); goto done; }
    if (!kv_block(apps, appse, APPID, &ab, &ae)) {
        char ins[2048];
        int n = snprintf(ins, sizeof ins,
                         "%s\t\t\t\t\t\"%s\"%s\t\t\t\t\t{%s\t\t\t\t\t\t\"LaunchOptions\"\t\t\"%s\"%s\t\t\t\t\t}",
                         eol, APPID, eol, eol, esc, eol);
        return splice(b, *lenp, (size_t)(appse - b), ins, (size_t)n, lenp);
    }
    {
        const char *vs, *ve;
        if (kv_scalar(ab, ae, "LaunchOptions", &vs, &ve)) {
            size_t at = (size_t)(vs - b), z = (size_t)(ve - b);
            size_t nl = *lenp - (z - at) + strlen(esc);
            char *n = (char *)malloc(nl + 1);
            if (!n) goto done;
            memcpy(n, buf, at);
            memcpy(n + at, esc, strlen(esc));
            memcpy(n + at + strlen(esc), buf + z, *lenp - z);
            n[nl] = 0;
            *lenp = nl;
            return n;
        }
        {
            char ins[2048];
            int n = snprintf(ins, sizeof ins, "%s\t\t\t\t\t\t\"LaunchOptions\"\t\t\"%s\"", eol, esc);
            return splice(b, *lenp, (size_t)(ab - b), ins, (size_t)n, lenp);
        }
    }
done:
    free(esc);
    return NULL;
}

static int patch_vdf(const char *vdfpath, const char *val)
{
    size_t len = 0;
    char *buf = read_all(vdfpath, &len);
    char err[256] = "";
    char *out;
    const char *eol = "\n";
    if (!buf) return -1;
    if (strstr(buf, "\r\n")) eol = "\r\n";
    out = vdf_set_launch(buf, &len, val, eol, err, sizeof err);
    if (!out) {
        say("  [x] 改启动项失败：%s\n", err);
        free(buf);
        return -1;
    }
    {
        int rc = write_all(vdfpath, out, len);
        free(out);
        free(buf);
        return rc;
    }
}

static char *read_launch_val(const char *vdfpath)
{
    size_t len = 0;
    char *buf = read_all(vdfpath, &len);
    char *ret = NULL;
    const char *a1, *a2, *b1, *b2, *c1, *c2, *d1, *d2, *apps, *appse, *ab, *ae, *vs, *ve;
    if (!buf) return NULL;
    if (kv_block(buf, buf + len, "UserLocalConfigStore", &a1, &a2) &&
        kv_block(a1, a2, "Software", &b1, &b2) &&
        kv_block(b1, b2, "Valve", &c1, &c2) &&
        kv_block(c1, c2, "Steam", &d1, &d2) &&
        kv_block(d1, d2, "apps", &apps, &appse) &&
        kv_block(apps, appse, APPID, &ab, &ae) &&
        kv_scalar(ab, ae, "LaunchOptions", &vs, &ve)) {
        size_t n = (size_t)(ve - vs), i = 0, j = 0;
        ret = (char *)malloc(n + 1);
        if (ret) {
            for (i = 0; i < n; i++) {
                if (vs[i] == '\\' && i + 1 < n) {
                    i++;
                    ret[j++] = vs[i];
                } else {
                    ret[j++] = vs[i];
                }
            }
            ret[j] = 0;
        }
    }
    free(buf);
    return ret;
}

/* ---------------- 备份 ---------------- */

static const char *MODFILES[] = { "dinput8.dll", "ScriptHookRDR2.dll", "NativeTrainer.asi", "Rampage.asi", NULL };
static const char *MODDIRS[] = { "RampageFiles", NULL };

/* 递归复制：只在真正要写文件时才建目标目录，避免留下空目录 */
static int copy_tree_lazy(const char *src, const char *dst, int filter_payload, const char *relprefix, int *nfiles)
{
    struct collect c;
    int i, err = 0;
    char rel[MAXP];
    if (!is_dir(src)) return -1;
    memset(&c, 0, sizeof c);
    for_each_entry(src, collect_cb, &c);
    for (i = 0; i < c.n; i++) {
        char *s = join(src, c.nm[i]);
        char *d = join(dst, c.nm[i]);
        if (!s || !d) { free(s); free(d); continue; }
        snprintf(rel, sizeof rel, "%s/%s", relprefix, c.nm[i]);
        if (c.d[i]) {
            if (copy_tree_lazy(s, d, filter_payload, rel, nfiles) != 0) err = -1;
        } else if (filter_payload && payload_same(rel, s)) {
            /* 是我们自己装的文件，不备份 */
        } else {
            if (mkdir_p(dst) != 0) err = -1;
            else if (copy_file(s, d) != 0) err = -1;
            else if (nfiles) (*nfiles)++;
        }
        free(s);
        free(d);
    }
    collect_free(&c);
    return err;
}

static char *make_backup(const char *gamedir, const char *vdfpath)
{
    char *h = home_dir();
    char *base = join(h, "RDR2-mod-backup");
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    char ts[64];
    char *dir, *gsub;
    FILE *mf;
    int i;
    if (!base) { free(h); return NULL; }
    strftime(ts, sizeof ts, "%Y%m%d-%H%M%S", tm);
    dir = join(base, ts);
    if (!dir || mkdir_p(dir) != 0) {
        say("  [!] 备份目录创建失败：%s\n", dir ? dir : "(null)");
        free(base);
        free(dir);
        free(h);
        return NULL;
    }
    gsub = join(dir, "game");
    mkdir_p(gsub);
    for (i = 0; MODFILES[i]; i++) {
        char *s = join(gamedir, MODFILES[i]);
        char *d = join(gsub, MODFILES[i]);
        if (s && d && f_exists(s) && !payload_same(MODFILES[i], s)) copy_file(s, d);
        free(s);
        free(d);
    }
    for (i = 0; MODDIRS[i]; i++) {
        char *s = join(gamedir, MODDIRS[i]);
        char *d = join(gsub, MODDIRS[i]);
        if (s && d && is_dir(s)) copy_tree_lazy(s, d, 1, MODDIRS[i], NULL);
        free(s);
        free(d);
    }
    if (vdfpath && f_exists(vdfpath)) {
        char *d = join(dir, "localconfig.vdf");
        if (d) { copy_file(vdfpath, d); free(d); }
    }
    {
        char *saves = find_proton_saves();
        if (!saves) saves = find_windows_saves();
        if (saves) {
            char *d = join(dir, "saves");
            say("  · 备份存档：%s\n", saves);
            if (d && copy_tree(saves, d) == 0) say("    -> %s\n", d);
            free(d);
            free(saves);
        }
    }
    {
        char *mp = join(dir, "manifest.txt");
        mf = mp ? fopen(mp, "w") : NULL;
        if (mf) {
            fprintf(mf, "time=%s\ngamedir=%s\nvdf=%s\n", ts, gamedir, vdfpath ? vdfpath : "(none)");
            fclose(mf);
        }
        free(mp);
    }
    free(gsub);
    free(base);
    free(h);
    return dir;
}

/* ---------------- 安装 / 卸载 ---------------- */

static int extract_all(const char *gamedir, int with_nt, int *nfiles)
{
    pkg_t k;
    char name[1024];
    int ok = 0, bad = 0;
    *nfiles = 0;
    if (pkg_open(&k) != 0) {
        say("  [x] 读不到内置负载（文件被截断？）\n");
        return -1;
    }
    for (;;) {
        const unsigned char *body;
        size_t bl;
        char *dst;
        FILE *f;
        if (!pkg_next(&k, name, sizeof name, &body, &bl)) break;
        if (!with_nt && strstr(name, "NativeTrainer")) continue;
        dst = join(gamedir, name);
        if (!dst) continue;
        {
            char *s1 = strrchr(dst, '/');
            char *s2 = strrchr(dst, '\\');
            char *slash = s1;
            if (s2 && (!slash || s2 > slash)) slash = s2;
            if (slash) {
                *slash = 0;
                mkdir_p(dst);
                *slash = SEP[0];
            }
        }
        f = fopen(dst, "wb");
        if (!f) {
            say("  [x] 写入失败：%s\n", dst);
            bad++;
            free(dst);
            continue;
        }
        if (fwrite(body, 1, bl, f) != bl) bad++;
        else ok++;
        fclose(f);
        free(dst);
    }
    free(k.data);
    *nfiles = ok;
    say("  · 释放文件 %d 个%s\n", ok, bad ? "（有写入失败）" : "");
    return bad ? -1 : 0;
}

static int verify_all(const char *gamedir, int with_nt)
{
    pkg_t k;
    char name[1024];
    int miss = 0;
    if (pkg_open(&k) != 0) return -1;
    for (;;) {
        const unsigned char *body;
        size_t bl;
        char *dst;
        if (!pkg_next(&k, name, sizeof name, &body, &bl)) break;
        if (!with_nt && strstr(name, "NativeTrainer")) continue;
        dst = join(gamedir, name);
        if (!dst) continue;
        if (!f_exists(dst) || (unsigned long long)fsize(dst) != (unsigned long long)bl) {
            say("  [x] 校验不符：%s\n", name);
            miss++;
        }
        free(dst);
    }
    free(k.data);
    return miss;
}

static int do_install(const char *rootopt, int with_nt)
{
    char *gamedir = rootopt ? xdup(rootopt) : find_game_dir();
    char *exe, *vdf = NULL, *backup = NULL;
    int rc = 1, nfiles = 0;
    if (!gamedir) {
        say("[x] 没找到 RDR2 安装目录。\n");
        say("    可以手动指定：--install --root \"/path/to/Red Dead Redemption 2\"\n");
        return 1;
    }
    exe = join(gamedir, "RDR2.exe");
    if (!exe || !f_exists(exe)) {
        say("[x] 这个目录里没有 RDR2.exe：%s\n", gamedir);
        free(exe);
        free(gamedir);
        return 1;
    }
    free(exe);
    say("[*] 游戏目录：%s\n", gamedir);

    /* 写入测试 */
    {
        char *t = join(gamedir, ".rdr2mod_write_test");
        FILE *f = t ? fopen(t, "wb") : NULL;
        if (!f) {
            say("[x] 目录不可写，可能权限不足。\n");
            free(t);
            free(gamedir);
            return 1;
        }
        fclose(f);
        remove(t);
        free(t);
    }

    if (steam_running()) say("[!] Steam 正在运行：改启动项前建议先退出 Steam（否则可能被覆盖）。\n");

    vdf = find_localconfig();
    if (g_no_vdf) {
        say("[*] 已按 --no-vdf 跳过启动项写入\n");
        vdf = NULL;
    } else if (!vdf) say("[!] 没找到 localconfig.vdf，稍后需要你手动加启动项。\n");
    else say("[*] 启动项文件：%s\n", vdf);

    backup = make_backup(gamedir, vdf);
    if (backup) say("[*] 备份目录：%s\n", backup);

    say("[*] 正在释放修改器文件 ...\n");
    if (extract_all(gamedir, with_nt, &nfiles) != 0) { rc = 2; goto out; }

    if (vdf) {
        if (patch_vdf(vdf, LAUNCHVAL) == 0) say("[*] 已写入启动项：%s\n", LAUNCHVAL);
        else { say("[!] 启动项没写成功，需要手动设置（见文末）。\n"); rc = 2; }
    } else {
        rc = 2;
    }

    {
        int miss = verify_all(gamedir, with_nt);
        if (miss) { say("[x] 有 %d 个文件校验失败\n", miss); rc = 2; }
        else say("[*] 文件校验通过（%d 个）\n", nfiles);
    }
out:
    if (rc == 1) say("\n[✓] 装好了。\n");
    else if (rc == 2) {
        say("\n[!] 文件装好了，但启动项/校验有问题，看上面提示。\n");
        say("    手动兜底：Steam 库 -> 右键 Red Dead Redemption 2 -> 属性 -> 启动选项，粘贴这一行：\n");
        say("    %s\n", LAUNCHVAL);
    }
    say("\n游戏里按 F5 开菜单；手柄是 RB+A（Xbox 布局）/ R1+×（PS 布局）。\n");
    say("只玩故事模式 —— 带 ScriptHook 进 RDR Online 会被踢。\n");
    {
        char *h = home_dir();
        char *lp = join(h, "RDR2-mod-install.log");
        say("日志：%s\n", lp ? lp : "(无)");
        free(h);
        free(lp);
    }
    free(vdf);
    free(backup);
    free(gamedir);
    return rc == 1 ? 0 : 1;
}

static char *newest_backup(void)
{
    char *h = home_dir();
    char *base = join(h, "RDR2-mod-backup");
    struct collect c;
    char *best = NULL;
    long long bestt = -1;
    int i;
    free(h);
    if (!base) return NULL;
    memset(&c, 0, sizeof c);
    for_each_entry(base, collect_cb, &c);
    for (i = 0; i < c.n; i++) {
        char *d;
        if (!c.d[i]) continue;
        d = join(base, c.nm[i]);
        if (d) {
            long long t = fmtime(d);
            if (t > bestt) { bestt = t; free(best); best = d; }
            else free(d);
        }
    }
    collect_free(&c);
    free(base);
    return best;
}

static int do_uninstall(const char *rootopt)
{
    char *gamedir = rootopt ? xdup(rootopt) : find_game_dir();
    char *vdf;
    char *bk;
    int i;
    if (!gamedir) { say("[x] 没找到游戏目录，用 --root 指定。\n"); return 1; }
    say("[*] 游戏目录：%s\n", gamedir);
    bk = newest_backup();
    for (i = 0; MODFILES[i]; i++) {
        char *p = join(gamedir, MODFILES[i]);
        if (p && f_exists(p)) {
            remove(p);
            say("  - 删除 %s\n", MODFILES[i]);
        }
        free(p);
    }
    for (i = 0; MODDIRS[i]; i++) {
        char *p = join(gamedir, MODDIRS[i]);
        if (p && f_exists(p)) {
            rm_rf(p);
            say("  - 删除 %s/\n", MODDIRS[i]);
        }
        free(p);
    }
    if (bk) {
        char *gsub = join(bk, "game");
        struct collect c;
        int j;
        memset(&c, 0, sizeof c);
        for_each_entry(gsub, collect_cb, &c);
        for (j = 0; j < c.n; j++) {
            char *s = join(gsub, c.nm[j]);
            char *d = join(gamedir, c.nm[j]);
            if (s && d) {
                if (c.d[j]) copy_tree_lazy(s, d, 0, c.nm[j], NULL);
                else copy_file(s, d);
                say("  ↑ 还原 %s\n", c.nm[j]);
            }
            free(s);
            free(d);
        }
        collect_free(&c);
        say("[*] 已从备份还原：%s\n", bk);
        free(gsub);
        free(bk);
    }
    vdf = find_localconfig();
    if (vdf) {
        char *cur = read_launch_val(vdf);
        if (cur && strstr(cur, LAUNCHMARK)) {
            char *orig = NULL;
            char *bakv = bk ? join(bk, "localconfig.vdf") : NULL;
            if (bakv && f_exists(bakv)) orig = read_launch_val(bakv);
            if (orig && strstr(orig, LAUNCHMARK)) { free(orig); orig = NULL; }
            if (patch_vdf(vdf, orig ? orig : "") == 0)
                say("[*] 启动项已还原为：%s\n", (orig && *orig) ? orig : "(空)");
            free(orig);
            free(bakv);
        }
        free(cur);
        free(vdf);
    }
    say("[✓] 卸载完成。\n");
    free(gamedir);
    return 0;
}

static void do_status(void)
{
    int i;
    char *gamedir;
    char *vdf;
    discover_steam_roots();
    say("Steam 库根目录（%d 个）：\n", g_nroots);
    for (i = 0; i < g_nroots; i++) say("  %s\n", g_roots[i]);
    gamedir = find_game_dir();
    say("游戏目录：%s\n", gamedir ? gamedir : "未找到");
    if (gamedir) {
        for (i = 0; MODFILES[i]; i++) {
            char *p = join(gamedir, MODFILES[i]);
            say("  %-22s %s", MODFILES[i], (p && f_exists(p)) ? "已装 " : "缺失 ");
            if (p && f_exists(p)) say("(%lld 字节)", fsize(p));
            say("\n");
            free(p);
        }
        for (i = 0; MODDIRS[i]; i++) {
            char *p = join(gamedir, MODDIRS[i]);
            say("  %-22s %s\n", MODDIRS[i], (p && is_dir(p)) ? "已装" : "缺失");
            free(p);
        }
    }
    vdf = find_localconfig();
    say("启动项文件：%s\n", vdf ? vdf : "未找到");
    if (vdf) {
        char *cur = read_launch_val(vdf);
        say("当前启动项：%s\n", (cur && *cur) ? cur : "(空)");
        free(cur);
        free(vdf);
    }
    say("Steam 进程：%s\n", steam_running() ? "在运行" : "未运行");
    free(gamedir);
}

static void do_diagnose(void)
{
    char *sp = self_path();
    pkg_t k;
    say("可执行文件：%s\n", sp ? sp : "(未知)");
    free(sp);
    if (pkg_open(&k) == 0) {
        char name[1024];
        const unsigned char *body;
        size_t bl;
        int n = 0, total = 0;
        while (pkg_next(&k, name, sizeof name, &body, &bl)) { n++; total += (int)bl; }
        say("内置负载：%d 个文件，%d 字节\n", n, total);
        free(k.data);
    } else {
        say("内置负载：读取失败\n");
    }
    do_status();
}

static int do_toggle(const char *rootopt)
{
    char *gamedir = rootopt ? xdup(rootopt) : find_game_dir();
    char *a, *b;
    if (!gamedir) { say("[x] 没找到游戏目录。\n"); return 1; }
    a = join(gamedir, "dinput8.dll");
    b = join(gamedir, "dinput8.dll.off");
    if (a && f_exists(a)) {
        remove(b);
        if (rename(a, b) == 0) say("[*] 已临时禁用修改器（dinput8.dll -> dinput8.dll.off）\n");
        else say("[x] 改名失败\n");
    } else if (b && f_exists(b)) {
        remove(a);
        if (rename(b, a) == 0) say("[*] 已重新启用修改器\n");
        else say("[x] 改名失败\n");
    } else {
        say("[!] 没找到 dinput8.dll，可能还没安装。\n");
    }
    free(a);
    free(b);
    free(gamedir);
    return 0;
}

static void pause_if_needed(void)
{
    char b[16];
    if (g_yes) return;
#ifdef _WIN32
    if (!_isatty(_fileno(stdin))) return;
#else
    if (!isatty(fileno(stdin))) return;
#endif
    say("\n按回车退出 ...");
    fflush(stdout);
    if (!fgets(b, sizeof b, stdin)) return;
}

/* ---------------- 入口 ---------------- */

static void usage(void)
{
    say("RDR2 修改器安装器\n");
    say("  rdr2mod                     交互菜单\n");
    say("  rdr2mod --install           自动查找游戏并安装\n");
    say("  rdr2mod --install --root DIR  指定游戏目录\n");
    say("  rdr2mod --uninstall         卸载并还原\n");
    say("  rdr2mod --status            查看状态\n");
    say("  rdr2mod --diagnose          诊断信息\n");
    say("  rdr2mod --toggle            临时禁用/启用\n");
    say("  可选：--no-vdf 不改启动项  --nativetrainer 额外装官方 NativeTrainer  --yes 不问\n");
}

int main(int argc, char **argv)
{
    int i, cmd = 0, with_nt = 0, no_vdf = 0;
    char *root = NULL;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--install")) cmd = 1;
        else if (!strcmp(argv[i], "--uninstall")) cmd = 2;
        else if (!strcmp(argv[i], "--status")) cmd = 3;
        else if (!strcmp(argv[i], "--diagnose")) cmd = 4;
        else if (!strcmp(argv[i], "--toggle")) cmd = 5;
        else if (!strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
        else if (!strcmp(argv[i], "--home") && i + 1 < argc) {
#ifdef _WIN32
            _putenv_s("RDR2MOD_HOME", argv[++i]);
#else
            setenv("RDR2MOD_HOME", argv[++i], 1);
#endif
        }
        else if (!strcmp(argv[i], "--steam-root") && i + 1 < argc) {
#ifdef _WIN32
            _putenv_s("RDR2MOD_STEAM", argv[++i]);
#else
            setenv("RDR2MOD_STEAM", argv[++i], 1);
#endif
        }
        else if (!strcmp(argv[i], "--nativetrainer")) with_nt = 1;
        else if (!strcmp(argv[i], "--no-vdf")) no_vdf = 1;
        else if (!strcmp(argv[i], "--yes") || !strcmp(argv[i], "-y")) g_yes = 1;
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else if (!strcmp(argv[i], "--version")) { say("rdr2mod 1.0\n"); return 0; }
    }
    (void)no_vdf;
    g_no_vdf = no_vdf;
    {
        char *h = home_dir();
        char *lp = join(h, "RDR2-mod-install.log");
        if (lp) {
            g_log = fopen(lp, "a");
            if (g_log) {
                time_t t = time(NULL);
                struct tm *tm = localtime(&t);
                fprintf(g_log, "\n=== %04d-%02d-%02d %02d:%02d:%02d ===\n", tm->tm_year + 1900, tm->tm_mon + 1,
                        tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
            }
            free(lp);
        }
        free(h);
    }
    discover_steam_roots();

    if (cmd == 0) {
        for (;;) {
            char line[64];
            say("\n=====================================\n");
            say("  RDR2 修改器安装器 v1.0\n");
            say("=====================================\n");
            say("  1. 安装 / 修复（自动找游戏）\n");
            say("  2. 指定游戏目录安装\n");
            say("  3. 查看状态\n");
            say("  4. 临时禁用 / 重新启用\n");
            say("  5. 卸载并还原\n");
            say("  6. 诊断\n");
            say("  0. 退出\n");
            say("选择：");
            if (!fgets(line, sizeof line, stdin)) break;
            if (line[0] == '1') { do_install(NULL, 0); pause_if_needed(); continue; }
            else if (line[0] == '2') {
                char p[MAXP];
                say("游戏目录：");
                if (!fgets(p, sizeof p, stdin)) break;
                {
                    size_t n = strlen(p);
                    while (n && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == '"')) p[--n] = 0;
                }
                if (*p) { do_install(p, 0); pause_if_needed(); }
            } else if (line[0] == '3') { do_status(); pause_if_needed(); }
            else if (line[0] == '4') { do_toggle(NULL); pause_if_needed(); }
            else if (line[0] == '5') { do_uninstall(NULL); pause_if_needed(); }
            else if (line[0] == '6') { do_diagnose(); pause_if_needed(); }
            else if (line[0] == '0') break;
        }
        return 0;
    }
    {
        int rc = 0;
        if (cmd == 1) rc = do_install(root, with_nt);
        else if (cmd == 2) rc = do_uninstall(root);
        else if (cmd == 3) do_status();
        else if (cmd == 4) do_diagnose();
        else if (cmd == 5) rc = do_toggle(root);
        pause_if_needed();
        return rc;
    }
}
