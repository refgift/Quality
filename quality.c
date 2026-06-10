/*
 * quality.c - Software Thermometer (C conversion of quality)
 *
 * Quality = Temperature (F)
 * Entropy = Disorder (higher = colder/lower quality)
 *
 * Rules applied:
 * - no contexts: all state explicit via parameters and return values; no ctx structs
 * - no imbalances: balanced if/else chains, symmetric alloc/free, complete handling
 * - no indirections: value returns for Metrics, embedded char arrays for dup lines
 *   (fixed 2D), int offsets not char**, direct array indexing, flat procedural
 *
 * Single file, no deps beyond libc + POSIX for dir walk.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <math.h>
#include <errno.h>
#include <strings.h>

#define MAX_PATH 512
#define MAX_DUP_CAND 16384
#define DUP_LINE_MAX 255
#define MAX_FILE_SIZE (64 * 1024 * 1024)

typedef struct {
    char filename[MAX_PATH];
    double quality_temp_f;
    int loc;
    int total_lines;
    double comment_ratio;
    int complexity_score;
    int duplicate_lines;
    double entropy_raw;
    char interpretation[80];
} Metrics;

static const char* COMPLEXITY_KWS[] = {
    "if", "else if", "elif", "for", "while", "do", "switch", "case",
    "&&", "||", "try", "catch", "except", "throw", "return"
};
static const size_t NUM_KWS = sizeof(COMPLEXITY_KWS) / sizeof(COMPLEXITY_KWS[0]);

static const char* SUPPORTED_EXTS[] = {
    ".c", ".cpp", ".h", ".hpp", ".py", ".js", ".java", ".ts", ".go", ".rs", NULL
};

static bool match_ic(const char* s, const char* p) {
    while (*p) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*p)) return false;
        ++s;
        ++p;
    }
    return true;
}

static int count_substr_ic(const char* text, const char* sub) {
    if (!*sub) return 0;
    int count = 0;
    size_t sl = strlen(sub);
    const char* t = text;
    while (*t) {
        if (match_ic(t, sub)) {
            ++count;
            t += sl;
        } else {
            ++t;
        }
    }
    return count;
}

static bool range_contains(const char* start, const char* end, const char* needle) {
    if (!*needle) return true;
    size_t nl = strlen(needle);
    if (start + nl > end) return false;
    for (const char* s = start; s + nl <= end; ++s) {
        if (memcmp(s, needle, nl) == 0) return true;
    }
    return false;
}

static int compare_str(const void* a, const void* b) {
    return strcmp((const char*)a, (const char*)b);
}

static double display_round(double v) {
    /* format with %.1f and parse back: guarantees reprint matches, and picks
       whatever the C runtime chooses for this computed double (aligns output to py in practice) */
    char buf[64];
    snprintf(buf, sizeof(buf), "%.1f", v);
    return atof(buf);
}

static const char* get_interpretation(double temp) {
    if (temp >= 90) return " Excellent  clean, maintainable, low entropy";
    else if (temp >= 80) return " Good  solid code with minor room for improvement";
    else if (temp >= 70) return " Acceptable  functional but watch complexity/duplication";
    else if (temp >= 60) return " Lukewarm  typical AI code; needs review";
    else if (temp >= 50) return " Cool  noticeable entropy; refactoring recommended";
    else if (temp >= 40) return " Very cool  high disorder; significant technical debt";
    else if (temp >= 32) return " Cold  on the rocks";
    else return " Frozen; functional but difficult";
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0 || sz > MAX_FILE_SIZE) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    char* buf = (char*)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static char* read_stdin(void) {
    size_t cap = 4096;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if (!buf) return NULL;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (len + 1 >= cap) {
            cap = cap * 2;
            if (cap > MAX_FILE_SIZE) cap = MAX_FILE_SIZE;
            char* nb = (char*)realloc(buf, cap);
            if (!nb) {
                free(buf);
                return NULL;
            }
            buf = nb;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

static Metrics compute_metrics(const char* content, const char* fname) {
    Metrics m;
    memset(&m, 0, sizeof(m));
    if (fname && *fname) {
        strncpy(m.filename, fname, MAX_PATH - 1);
        m.filename[MAX_PATH - 1] = '\0';
    } else {
        strcpy(m.filename, "stdin");
    }

    /* one pass: total_lines, loc, comments, collect dup candidates (embedded array) */
    int total_lines = 0;
    int loc = 0;
    int comment_lines = 0;
    bool in_multiline = false;

    char (*dlines)[DUP_LINE_MAX + 1] = (char (*)[DUP_LINE_MAX + 1]) calloc(MAX_DUP_CAND, DUP_LINE_MAX + 1);
    int dline_count = 0;

    const char* p = content;
    while (*p) {
        const char* ls = p;
        while (*p && *p != '\n' && *p != '\r') ++p;
        const char* le = p;
        if (*p == '\r') ++p;
        if (*p == '\n') ++p;

        ++total_lines;

        /* stripped range ss..se */
        const char* ss = ls;
        while (ss < le && isspace((unsigned char)*ss)) ++ss;
        const char* se = le;
        while (se > ss && isspace((unsigned char)*(se - 1))) --se;
        int slen = (int)(se - ss);

        if (slen > 0) ++loc;

        /* collect for dups if qualifies (direct embedded, no per-line pointers).
           Must collect for ALL lines (even those that look like comments), to match py. */
        if (slen > 10 && dline_count < MAX_DUP_CAND && dlines) {
            int cl = slen > DUP_LINE_MAX ? DUP_LINE_MAX : slen;
            memcpy(dlines[dline_count], ss, (size_t)cl);
            dlines[dline_count][cl] = '\0';
            ++dline_count;
        }

        /* comment detection (mirrors original logic exactly) */
        if (in_multiline) {
            ++comment_lines;
            if (range_contains(ss, se, "*/")) {
                in_multiline = false;
            }
            continue;
        }
        bool is_comment_start = false;
        if (slen >= 1 && *ss == '#') {
            is_comment_start = true;
        } else if (slen >= 2 && ss[0] == '/' && ss[1] == '/') {
            is_comment_start = true;
        } else if (slen >= 2 && ss[0] == '/' && ss[1] == '*') {
            is_comment_start = true;
        }
        if (is_comment_start) {
            ++comment_lines;
            if (slen >= 2 && ss[0] == '/' && ss[1] == '*' && !range_contains(ss, se, "*/")) {
                in_multiline = true;
            }
            continue;
        }
        if (range_contains(ss, se, "/*")) {
            ++comment_lines;
            if (!range_contains(ss, se, "*/")) {
                in_multiline = true;
            }
        }
    }

    m.total_lines = total_lines;
    m.loc = loc;
    m.comment_ratio = (loc > 0 ? (comment_lines * 100.0 / loc) : 0.0); /* full precision for entropy/penalty */

    /* complexity (no lower buffer, direct ic counts = no false extra work) */
    m.complexity_score = 0;
    for (size_t i = 0; i < NUM_KWS; ++i) {
        m.complexity_score += count_substr_ic(content, COMPLEXITY_KWS[i]);
    }

    /* dups via direct sort of embedded array (no indirection) */
    m.duplicate_lines = 0;
    if (dlines && dline_count > 1) {
        qsort(dlines, (size_t)dline_count, sizeof(dlines[0]), compare_str);
        int i = 0;
        while (i < dline_count) {
            int j = i + 1;
            while (j < dline_count && strcmp(dlines[j], dlines[i]) == 0) ++j;
            int rep = j - i;
            if (rep > 1) m.duplicate_lines += (rep - 1);
            i = j;
        }
    }
    if (dlines) free(dlines);

    /* entropy + temp (exact weights from original) */
    double entropy = (m.complexity_score * 1.8) +
                     (m.duplicate_lines * 4.0) +
                     (m.loc / 80.0) +
                     ((50.0 - m.comment_ratio) > 0.0 ? (50.0 - m.comment_ratio) * 0.3 : 0.0);

    double qtemp = 92.0 - (entropy * 0.55);
    if (qtemp > 100.0) qtemp = 100.0;
    if (qtemp < 0.0) qtemp = 0.0;
    if (m.comment_ratio < 8.0) qtemp -= 8.0;
    if (qtemp < 0.0) qtemp = 0.0;

    /* use display_round so that printed values and derived avg match the digit choices of python version */
    m.comment_ratio = display_round(m.comment_ratio);
    m.quality_temp_f = display_round(qtemp);
    m.entropy_raw = display_round(entropy);

    strncpy(m.interpretation, get_interpretation(m.quality_temp_f), sizeof(m.interpretation) - 1);
    m.interpretation[sizeof(m.interpretation) - 1] = '\0';

    return m;
}

static void print_report(const Metrics* m) {
    putchar('\n');
    for (int i = 0; i < 60; ++i) putchar('=');
    putchar('\n');
    printf("  SOFTWARE THERMOMETER  |  %s\n", m->filename);
    for (int i = 0; i < 60; ++i) putchar('=');
    putchar('\n');
    printf("  Quality Temperature: %.1fF\n", m->quality_temp_f);
    printf("  Interpretation:      %s\n", m->interpretation);
    for (int i = 0; i < 60; ++i) putchar('-');
    putchar('\n');
    printf("  Lines of Code (LOC): %d\n", m->loc);
    printf("  Total Lines:         %d\n", m->total_lines);
    printf("  Comment Ratio:       %.1f%%\n", m->comment_ratio);
    printf("  Complexity Score:    %d\n", m->complexity_score);
    printf("  Duplicated Lines:    %d\n", m->duplicate_lines);
    printf("  Entropy (raw):       %.1f\n", m->entropy_raw);
    for (int i = 0; i < 60; ++i) putchar('=');
    putchar('\n');
    printf("  Tip: Run this regularly on AI-generated code to track if\n");
    printf("       temperature is rising (improving) or falling over time.\n\n");
}

static bool has_supported_ext(const char* path) {
    const char* dot = strrchr(path, '.');
    if (!dot) return false;
    for (size_t i = 0; SUPPORTED_EXTS[i]; ++i) {
        if (strcasecmp(dot, SUPPORTED_EXTS[i]) == 0) return true;
    }
    return false;
}

static void process_path(const char* path, double* sum, int* nfiles, bool* any);

static void process_directory(const char* dirpath, double* sum, int* nfiles, bool* any) {
    DIR* d = opendir(dirpath);
    if (!d) {
        printf("Error reading %s\n", dirpath);
        return;
    }
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char sub[MAX_PATH];
        int written = snprintf(sub, sizeof(sub), "%s/%s", dirpath, ent->d_name);
        if (written < 0 || (size_t)written >= sizeof(sub)) continue;
        process_path(sub, sum, nfiles, any);
    }
    closedir(d);
}

static void process_path(const char* path, double* sum, int* nfiles, bool* any) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        process_directory(path, sum, nfiles, any);
    } else if (S_ISREG(st.st_mode)) {
        if (has_supported_ext(path)) {
            char* content = read_file(path);
            if (content) {
                Metrics m = compute_metrics(content, path);
                free(content);
                print_report(&m);
                if (sum) *sum += m.quality_temp_f;
                if (nfiles) ++(*nfiles);
                if (any) *any = true;
            } else {
                printf("Error reading %s\n", path);
            }
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: quality [file|directory|-] \n");
        return 1;
    }

    const char* target = argv[1];

    if (strcmp(target, "-") == 0) {
        char* content = read_stdin();
        if (!content) {
            fprintf(stderr, "Error reading stdin\n");
            return 1;
        }
        Metrics m = compute_metrics(content, "stdin");
        free(content);
        print_report(&m);
        return 0;
    }

    struct stat st;
    if (stat(target, &st) != 0) {
        printf("File not found: %s\n", target);
        return 1;
    }

    if (S_ISDIR(st.st_mode)) {
        printf("Analyzing directory: %s\n\n", target);
        double sum = 0.0;
        int nfiles = 0;
        bool any = false;
        process_directory(target, &sum, &nfiles, &any);
        if (any && nfiles > 0) {
            double avg = sum / nfiles;
            printf("\n DIRECTORY AVERAGE QUALITY TEMPERATURE: %.1fF\n", avg);
        }
    } else {
        char* content = read_file(target);
        if (!content) {
            printf("Error reading %s\n", target);
            return 1;
        }
        Metrics m = compute_metrics(content, target);
        free(content);
        print_report(&m);
    }

    return 0;
}
