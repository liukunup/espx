/**
 * @file yaml.c
 * @brief Minimal YAML subset parser producing cJSON (see yaml.h)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <cJSON.h>

#include "yaml.h"

/* -------------------------------------------------------------------------- */
/* line table                                                                  */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char *text;   /* start of the line content (after indentation) */
    size_t len;         /* content length, comment and trailing space removed */
    int indent;         /* indentation in columns */
    bool is_item;       /* line starts with "- " or is exactly "-" */
    int line_no;        /* 1-based source line, for error messages */
} yline_t;

typedef struct {
    yline_t *lines;
    size_t count;
    size_t cap;
    int err_line;
    const char *err_msg;
} yctx_t;

static void set_err(yctx_t *c, int line, const char *msg)
{
    if (c->err_msg == NULL) {         /* keep the first error */
        c->err_line = line;
        c->err_msg = msg;
    }
}

static bool push_line(yctx_t *c, const yline_t *l)
{
    if (c->count == c->cap) {
        size_t ncap = c->cap ? c->cap * 2 : 32;
        yline_t *n = realloc(c->lines, ncap * sizeof(*n));
        if (n == NULL) return false;
        c->lines = n;
        c->cap = ncap;
    }
    c->lines[c->count++] = *l;
    return true;
}

/**
 * @brief Strip a trailing comment, honouring quotes
 */
static size_t strip_comment(const char *s, size_t len)
{
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < len; i++) {
        char ch = s[i];
        if (ch == '\\' && in_d && i + 1 < len) { i++; continue; }
        if (ch == '\'' && !in_d) in_s = !in_s;
        else if (ch == '"' && !in_s) in_d = !in_d;
        else if (ch == '#' && !in_s && !in_d) {
            /* a comment must be preceded by whitespace or start the line */
            if (i == 0 || isspace((unsigned char)s[i - 1])) return i;
        }
    }
    return len;
}

static bool tokenize(yctx_t *c, const char *text)
{
    int line_no = 0;
    const char *p = text;

    while (*p) {
        line_no++;
        const char *eol = strpbrk(p, "\r\n");
        size_t raw_len = eol ? (size_t)(eol - p) : strlen(p);

        /* indentation */
        int indent = 0;
        const char *s = p;
        size_t rem = raw_len;
        while (rem > 0 && (*s == ' ' || *s == '\t')) {
            indent += (*s == '\t') ? 2 : 1;   /* tabs count as 2 columns */
            s++; rem--;
        }

        size_t len = strip_comment(s, rem);
        while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;

        if (len > 0) {
            yline_t l = { .text = s, .len = len, .indent = indent,
                          .is_item = false, .line_no = line_no };

            if (len >= 1 && s[0] == '-' && (len == 1 || s[1] == ' ' || s[1] == '\t')) {
                l.is_item = true;
                size_t skip = 1;
                while (skip < len && (s[skip] == ' ' || s[skip] == '\t')) skip++;
                l.text = s + skip;
                l.len = len - skip;
                /* indentation of the item content is used for continuation lines */
                l.indent = indent;
            }

            if (!push_line(c, &l)) {
                set_err(c, line_no, "out of memory");
                return false;
            }
        }

        p = eol ? eol + 1 : p + raw_len;
        if (eol && *eol == '\r' && eol[1] == '\n') p = eol + 2;
        else if (eol && *eol == '\n') p = eol + 1;
        else if (eol) p = eol + 1;
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/* scalars                                                                     */
/* -------------------------------------------------------------------------- */

static char *unquote(const char *s, size_t len)
{
    if (len >= 2 && ((s[0] == '"' && s[len - 1] == '"') ||
                     (s[0] == '\'' && s[len - 1] == '\''))) {
        char quote = s[0];
        s++; len -= 2;
        char *out = malloc(len + 1);
        if (out == NULL) return NULL;
        size_t o = 0;
        for (size_t i = 0; i < len; i++) {
            if (quote == '"' && s[i] == '\\' && i + 1 < len) {
                i++;
                switch (s[i]) {
                case 'n': out[o++] = '\n'; break;
                case 't': out[o++] = '\t'; break;
                case 'r': out[o++] = '\r'; break;
                case '"': out[o++] = '"'; break;
                case '\\': out[o++] = '\\'; break;
                default: out[o++] = s[i]; break;
                }
            } else {
                out[o++] = s[i];
            }
        }
        out[o] = '\0';
        return out;
    }

    char *out = malloc(len + 1);
    if (out == NULL) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static bool ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static cJSON *flow_parse(const char **pp, const char *end, int *err);

/**
 * @brief Parse a flow mapping key, always as a string
 *
 * @return malloc'd key text (caller frees), or NULL on error
 */
static char *flow_key(const char **pp, const char *end, int *err);

/**
 * @brief Convert a plain (non flow-collection) scalar token to JSON
 *
 * Numbers, booleans, null and quoted/plain strings. Flow collections are
 * handled by flow_parse() and must not reach here.
 */
static cJSON *scalar_basic(const char *s, size_t len);

/**
 * @brief Parse a flow scalar: quoted string or plain text up to a terminator
 */
static cJSON *flow_scalar(const char **pp, const char *end, bool stop_at_colon, int *err)
{
    const char *p = *pp;

    while (p < end && (*p == ' ' || *p == '\t')) p++;

    if (p < end && (*p == '"' || *p == '\'')) {
        char quote = *p;
        const char *start = p;
        p++;
        while (p < end) {
            if (quote == '"' && *p == '\\' && p + 1 < end) { p += 2; continue; }
            if (*p == quote) { p++; break; }
            p++;
        }
        if (p > end || (p <= end && *(p - 1) != quote)) { *err = 1; return NULL; }
        cJSON *v = scalar_basic(start, (size_t)(p - start));
        *pp = p;
        return v;
    }

    const char *start = p;
    while (p < end) {
        char ch = *p;
        if (ch == ',' || ch == '}' || ch == ']') break;
        if (stop_at_colon && ch == ':' &&
            (p + 1 >= end || p[1] == ' ' || p[1] == '\t')) break;
        p++;
    }

    const char *stop = p;
    while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;

    if (stop == start) { *err = 1; return NULL; }

    cJSON *v = scalar_basic(start, (size_t)(stop - start));
    *pp = p;
    return v;
}

static char *flow_key(const char **pp, const char *end, int *err)
{
    const char *p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    const char *start = p;
    char quote = 0;

    if (p < end && (*p == '"' || *p == '\'')) {
        quote = *p;
        start = p;
        p++;
        while (p < end) {
            if (quote == '"' && *p == '\\' && p + 1 < end) { p += 2; continue; }
            if (*p == quote) { p++; break; }
            p++;
        }
        if (p > end || *(p - 1) != quote) { *err = 1; return NULL; }
        char *out = unquote(start, (size_t)(p - start));
        if (out == NULL) { *err = 1; return NULL; }
        *pp = p;
        return out;
    }

    while (p < end) {
        char ch = *p;
        if (ch == ',' || ch == '}' || ch == ']') break;
        if (ch == ':' && (p + 1 >= end || p[1] == ' ' || p[1] == '\t')) break;
        p++;
    }

    const char *stop = p;
    while (stop > start && (stop[-1] == ' ' || stop[-1] == '\t')) stop--;
    if (stop == start) { *err = 1; return NULL; }

    char *out = malloc((size_t)(stop - start) + 1);
    if (out == NULL) { *err = 1; return NULL; }
    memcpy(out, start, (size_t)(stop - start));
    out[stop - start] = '\0';
    *pp = p;
    return out;
}

/**
 * @brief Parse a YAML flow collection ({...} or [...]) or a plain scalar
 */
static cJSON *flow_parse(const char **pp, const char *end, int *err)
{
    const char *p = *pp;
    while (p < end && (*p == ' ' || *p == '\t')) p++;

    if (p < end && (*p == '{' || *p == '[')) {
        bool is_map = (*p == '{');
        const char close = is_map ? '}' : ']';
        p++;

        cJSON *out = is_map ? cJSON_CreateObject() : cJSON_CreateArray();
        if (out == NULL) { *err = 1; return NULL; }

        while (true) {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
            if (p >= end) { cJSON_Delete(out); *err = 1; return NULL; }
            if (*p == close) { p++; break; }
            if (*p == ',') { p++; continue; }

            if (is_map) {
                /* A mapping key is always a string: it must NOT go through
                 * scalar conversion, or a key such as "on" would become the
                 * boolean true and the key would be lost. */
                char *kname = flow_key(&p, end, err);
                if (*err || kname == NULL) { cJSON_Delete(out); *err = 1; return NULL; }
                while (p < end && (*p == ' ' || *p == '\t')) p++;
                if (p >= end || *p != ':') {
                    free(kname); cJSON_Delete(out); *err = 1; return NULL;
                }
                p++;
                cJSON *val = flow_parse(&p, end, err);
                if (*err || val == NULL) { free(kname); cJSON_Delete(out); *err = 1; return NULL; }

                if (cJSON_GetObjectItem(out, kname)) cJSON_DeleteItemFromObject(out, kname);
                cJSON_AddItemToObject(out, kname, val);
                free(kname);
            } else {
                cJSON *val = flow_parse(&p, end, err);
                if (*err || val == NULL) { cJSON_Delete(out); *err = 1; return NULL; }
                cJSON_AddItemToArray(out, val);
            }
        }

        *pp = p;
        return out;
    }

    cJSON *v = flow_scalar(&p, end, false, err);
    *pp = p;
    return v;
}

/**
 * @brief Convert a scalar token to JSON
 *
 * A token starting with '{' or '[' MUST parse as a flow collection. Falling
 * back to a string here would silently turn a device config into a string and
 * break the bind, so it is reported as an error instead.
 */
static cJSON *scalar_basic(const char *s, size_t len)
{
    char *str = unquote(s, len);
    if (str == NULL) {
        return NULL;
    }

    cJSON *out = NULL;

    if (str[0] == '\0') {
        out = cJSON_CreateNull();
    } else if (ieq(str, "null") || strcmp(str, "~") == 0) {
        out = cJSON_CreateNull();
    } else if (ieq(str, "true") || ieq(str, "yes") || ieq(str, "on")) {
        out = cJSON_CreateBool(1);
    } else if (ieq(str, "false") || ieq(str, "no") || ieq(str, "off")) {
        out = cJSON_CreateBool(0);
    }

    if (out == NULL) {
        char *endp = NULL;
        long long iv = strtoll(str, &endp, 0);
        if (endp != str && *endp == '\0') {
            out = cJSON_CreateNumber((double)iv);
        } else {
            double dv = strtod(str, &endp);
            if (endp != str && *endp == '\0') {
                out = cJSON_CreateNumber(dv);
            }
        }
    }

    if (out == NULL) {
        out = cJSON_CreateString(str);
    }

    free(str);
    return out;
}

/**
 * @brief Convert a value token to JSON, handling flow collections
 *
 * A token starting with '{' or '[' MUST parse as a flow collection: falling
 * back to a string would silently turn a device config into a string and break
 * the bind, so a malformed collection is an error.
 */
static cJSON *scalar_to_json(yctx_t *c, const char *s, size_t len, int line_no)
{
    char *str = unquote(s, len);
    if (str == NULL) {
        set_err(c, line_no, "out of memory");
        return NULL;
    }

    if (str[0] == '{' || str[0] == '[') {
        int err = 0;
        const char *p = str;
        cJSON *flow = flow_parse(&p, str + strlen(str), &err);
        if (err || flow == NULL) {
            free(str);
            set_err(c, line_no, "malformed flow collection {..} / [..]");
            return NULL;
        }
        /* trailing junk after the collection */
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0') {
            cJSON_Delete(flow);
            free(str);
            set_err(c, line_no, "trailing characters after flow collection");
            return NULL;
        }
        free(str);
        return flow;
    }

    free(str);
    return scalar_basic(s, len);
}

/* -------------------------------------------------------------------------- */
/* block parsing                                                               */
/* -------------------------------------------------------------------------- */

static cJSON *parse_block(yctx_t *c, size_t *i, int indent);

/**
 * @brief Split "key: value" at the first colon outside quotes
 *
 * @return true when a mapping entry was found; key/val are trimmed slices
 */
static bool split_key(const yline_t *l, size_t *key_len, size_t *val_off, size_t *val_len)
{
    bool in_s = false, in_d = false;
    for (size_t i = 0; i < l->len; i++) {
        char ch = l->text[i];
        if (ch == '\\' && in_d && i + 1 < l->len) { i++; continue; }
        if (ch == '\'' && !in_d) in_s = !in_s;
        else if (ch == '"' && !in_s) in_d = !in_d;
        else if (ch == ':' && !in_s && !in_d) {
            /* "key:" or "key: value" — a colon inside a bare scalar such as
             * "mqtt://host" only counts when followed by space/EOL, and we
             * only look for the FIRST such colon. */
            if (i + 1 < l->len && l->text[i + 1] != ' ' && l->text[i + 1] != '\t') {
                continue;
            }
            *key_len = i;
            size_t v = i + 1;
            while (v < l->len && (l->text[v] == ' ' || l->text[v] == '\t')) v++;
            *val_off = v;
            *val_len = l->len - v;
            return true;
        }
    }
    return false;
}

static cJSON *parse_mapping(yctx_t *c, size_t *i, int indent)
{
    cJSON *obj = cJSON_CreateObject();
    if (obj == NULL) return NULL;

    while (*i < c->count) {
        yline_t *l = &c->lines[*i];

        if (l->indent < indent) break;
        if (l->indent > indent) {
            set_err(c, l->line_no, "unexpected indentation");
            cJSON_Delete(obj);
            return NULL;
        }
        if (l->is_item) break;              /* a sequence at this level */

        size_t key_len, val_off, val_len;
        if (!split_key(l, &key_len, &val_off, &val_len)) {
            set_err(c, l->line_no, "expected 'key: value'");
            cJSON_Delete(obj);
            return NULL;
        }

        char *key = unquote(l->text, key_len);
        if (key == NULL) {
            cJSON_Delete(obj);
            return NULL;
        }

        cJSON *val;
        if (val_len > 0) {
            val = scalar_to_json(c, l->text + val_off, val_len, l->line_no);
            (*i)++;
            if (val == NULL) {
                free(key);
                cJSON_Delete(obj);
                return NULL;
            }
        } else {
            (*i)++;
            if (*i < c->count && c->lines[*i].indent > l->indent) {
                val = parse_block(c, i, c->lines[*i].indent);
            } else {
                val = cJSON_CreateNull();
            }
        }

        if (val == NULL) {
            free(key);
            cJSON_Delete(obj);
            return NULL;
        }

        /* last key wins on duplicates */
        if (cJSON_GetObjectItem(obj, key) != NULL) {
            cJSON_DeleteItemFromObject(obj, key);
        }
        cJSON_AddItemToObject(obj, key, val);
        free(key);
    }

    return obj;
}

static cJSON *parse_sequence(yctx_t *c, size_t *i, int indent)
{
    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) return NULL;

    while (*i < c->count) {
        yline_t *l = &c->lines[*i];

        if (l->indent < indent) break;
        if (!l->is_item) break;
        if (l->indent > indent) {
            set_err(c, l->line_no, "unexpected indentation in sequence");
            cJSON_Delete(arr);
            return NULL;
        }

        cJSON *item = NULL;

        if (l->len == 0) {
            /* "-" alone: the item is the following more-indented block */
            (*i)++;
            if (*i < c->count && c->lines[*i].indent > indent) {
                item = parse_block(c, i, c->lines[*i].indent);
            } else {
                item = cJSON_CreateNull();
            }
        } else if (l->text[0] == '{' || l->text[0] == '[') {
            /* "- {id: a, type: relay}" — a flow collection, not an inline map */
            item = scalar_to_json(c, l->text, l->len, l->line_no);
            (*i)++;
            if (item == NULL) {
                cJSON_Delete(arr);
                return NULL;
            }
        } else {
            size_t key_len, val_off, val_len;
            if (split_key(l, &key_len, &val_off, &val_len)) {
                /* inline mapping start: "- id: x" then continuation lines */
                item = cJSON_CreateObject();
                char *key = unquote(l->text, key_len);
                cJSON *val;
                if (val_len > 0) {
                    val = scalar_to_json(c, l->text + val_off, val_len, l->line_no);
                } else {
                    val = cJSON_CreateNull();
                }
                if (key == NULL || val == NULL) {
                    free(key);
                    cJSON_Delete(arr);
                    if (item) cJSON_Delete(item);
                    return NULL;
                }
                cJSON_AddItemToObject(item, key, val);
                free(key);
                (*i)++;

                /* continuation keys are indented past the "-" */
                while (*i < c->count && c->lines[*i].indent > indent && !c->lines[*i].is_item) {
                    cJSON *rest = parse_mapping(c, i, c->lines[*i].indent);
                    if (rest == NULL) {
                        cJSON_Delete(arr);
                        cJSON_Delete(item);
                        return NULL;
                    }
                    cJSON *child = NULL;
                    cJSON_ArrayForEach(child, rest) {
                        if (cJSON_GetObjectItem(item, child->string) != NULL) {
                            cJSON_DeleteItemFromObject(item, child->string);
                        }
                        cJSON_AddItemToObject(item, child->string,
                                              cJSON_Duplicate(child, true));
                    }
                    cJSON_Delete(rest);
                }
            } else {
                item = scalar_to_json(c, l->text, l->len, l->line_no);
                (*i)++;
            }
        }

        if (item == NULL) {
            cJSON_Delete(arr);
            return NULL;
        }
        cJSON_AddItemToArray(arr, item);
    }

    return arr;
}

static cJSON *parse_block(yctx_t *c, size_t *i, int indent)
{
    if (*i >= c->count) return cJSON_CreateNull();
    return c->lines[*i].is_item ? parse_sequence(c, i, indent)
                                : parse_mapping(c, i, indent);
}

/* -------------------------------------------------------------------------- */
/* public                                                                      */
/* -------------------------------------------------------------------------- */

cJSON *yaml_parse_ex(const char *text, int *err_line, const char **err_msg)
{
    if (text == NULL) {
        if (err_msg) *err_msg = "null input";
        return NULL;
    }

    yctx_t c = {0};
    c.err_line = 0;
    c.err_msg = NULL;

    if (!tokenize(&c, text)) {
        free(c.lines);
        if (err_line) *err_line = c.err_line;
        if (err_msg) *err_msg = c.err_msg;
        return NULL;
    }

    if (c.count == 0) {
        free(c.lines);
        return cJSON_CreateNull();          /* empty document */
    }

    size_t i = 0;
    cJSON *root = parse_block(&c, &i, c.lines[0].indent);

    if (root != NULL && i < c.count) {
        set_err(&c, c.lines[i].line_no, "trailing content after the document root");
        cJSON_Delete(root);
        root = NULL;
    }

    if (err_line) *err_line = c.err_line;
    if (err_msg) *err_msg = c.err_msg;

    free(c.lines);
    return root;
}

cJSON *yaml_parse(const char *text)
{
    return yaml_parse_ex(text, NULL, NULL);
}
