/*
 * lj_ffi_cdef.cpp — C Declaration Parser for LJ-FFI
 *
 * Parses a LuaJIT-compatible cdef string and registers types + functions
 * into the LJ-FFI type system and function registry.
 */

#include "lj_ffi.h"
#include "lj_ffi_cdef.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctype.h>
#include <string>
#include <vector>
#include <unordered_map>

/* ============================================================
 * Function Registry
 * ============================================================ */

static std::unordered_map<std::string, lj_ffi_func_t*> g_func_registry;
static bool g_func_initialized = false;

void lj_ffi_func_init(void)
{
    if (g_func_initialized) return;
    g_func_initialized = true;
    fprintf(stderr, "LJ-FFI: function registry initialized\n");
}

int lj_ffi_func_register(const char *name, lj_ffi_type_t *ret_type,
                         lj_ffi_type_t **param_types, int num_params)
{
    if (!name || !ret_type) return -1;

    lj_ffi_func_t *f = (lj_ffi_func_t*)calloc(1, sizeof(lj_ffi_func_t));
    f->name = strdup(name);
    f->ret_type = ret_type;
    f->num_params = num_params;

    if (num_params > 0 && param_types) {
        f->param_types = (lj_ffi_type_t**)malloc(num_params * sizeof(lj_ffi_type_t*));
        memcpy(f->param_types, param_types, num_params * sizeof(lj_ffi_type_t*));
    }

    g_func_registry[std::string(name)] = f;
    return 0;
}

lj_ffi_func_t *lj_ffi_func_lookup(const char *name)
{
    if (!name) return nullptr;
    auto it = g_func_registry.find(std::string(name));
    return (it != g_func_registry.end()) ? it->second : nullptr;
}

int lj_ffi_func_bind(const char *name, void *ptr)
{
    lj_ffi_func_t *f = lj_ffi_func_lookup(name);
    if (!f) {
        fprintf(stderr, "LJ-FFI: bind failed, unknown function '%s'\n", name);
        return -1;
    }
    f->func_ptr = ptr;
    return 0;
}

/* ============================================================
 * Tokenizer
 * ============================================================ */

enum token_kind {
    TOK_EOF, TOK_IDENT, TOK_NUMBER,
    TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN,
    TOK_SEMICOLON, TOK_COMMA, TOK_STAR, TOK_LBRACKET, TOK_RBRACKET,
    TOK_TYPEDEF, TOK_STRUCT, TOK_VOID, TOK_UNKNOWN
};

struct token {
    token_kind kind;
    std::string text;
};

class lexer {
public:
    lexer(const char *src) : m_src(src), m_pos(0) {}

    token next()
    {
        skip_ws();
        if (m_pos >= m_len) return {TOK_EOF, ""};

        char c = m_src[m_pos];

        /* Single-char tokens */
        switch (c) {
            case '{': m_pos++; return {TOK_LBRACE, "{"};
            case '}': m_pos++; return {TOK_RBRACE, "}"};
            case '(': m_pos++; return {TOK_LPAREN, "("};
            case ')': m_pos++; return {TOK_RPAREN, ")"};
            case ';': m_pos++; return {TOK_SEMICOLON, ";"};
            case ',': m_pos++; return {TOK_COMMA, ","};
            case '*': m_pos++; return {TOK_STAR, "*"};
            case '[': m_pos++; return {TOK_LBRACKET, "["};
            case ']': m_pos++; return {TOK_RBRACKET, "]"};
        }

        /* Number */
        if (isdigit(c)) {
            size_t start = m_pos;
            while (m_pos < m_len && isdigit(m_src[m_pos])) m_pos++;
            return {TOK_NUMBER, std::string(m_src + start, m_pos - start)};
        }

        /* Identifier / keyword */
        if (isalpha(c) || c == '_') {
            size_t start = m_pos;
            while (m_pos < m_len && (isalnum(m_src[m_pos]) || m_src[m_pos] == '_'))
                m_pos++;
            std::string word(m_src + start, m_pos - start);

            if (word == "typedef") return {TOK_TYPEDEF, word};
            if (word == "struct")  return {TOK_STRUCT, word};
            if (word == "void")    return {TOK_VOID, word};
            return {TOK_IDENT, word};
        }

        /* Skip unknown char */
        m_pos++;
        return {TOK_UNKNOWN, std::string(1, c)};
    }

    token peek()
    {
        size_t saved = m_pos;
        token t = next();
        m_pos = saved;
        return t;
    }

    bool at_end() { skip_ws(); return m_pos >= m_len; }

private:
    void skip_ws()
    {
        while (m_pos < m_len && isspace(m_src[m_pos])) m_pos++;
        /* Skip C-style comments */
        while (m_pos + 1 < m_len && m_src[m_pos] == '/' && m_src[m_pos+1] == '*') {
            m_pos += 2;
            while (m_pos + 1 < m_len && !(m_src[m_pos] == '*' && m_src[m_pos+1] == '/'))
                m_pos++;
            if (m_pos + 1 < m_len) m_pos += 2;
        }
        /* Skip C++ style comments */
        while (m_pos + 1 < m_len && m_src[m_pos] == '/' && m_src[m_pos+1] == '/') {
            m_pos += 2;
            while (m_pos < m_len && m_src[m_pos] != '\n') m_pos++;
        }
    }

    const char *m_src;
    size_t m_pos;
    size_t m_len = strlen(m_src);
};

/* ============================================================
 * Type Resolution Helper
 * ============================================================ */

/*
 * Resolve a type name (possibly with trailing *) to lj_ffi_type_t*.
 * Handles: "int", "uint32_t", "vec3_t", "int*", "void*"
 */
static lj_ffi_type_t *resolve_type(lexer &lex, bool allow_pointer = true)
{
    token t = lex.next();

    if (t.kind == TOK_VOID) {
        /* Check for void* */
        if (allow_pointer && lex.peek().kind == TOK_STAR) {
            lex.next(); /* consume * */
            return lj_ffi_type_lookup("pointer");
        }
        return lj_ffi_type_lookup("void");
    }

    if (t.kind != TOK_IDENT && t.kind != TOK_STRUCT) {
        fprintf(stderr, "LJ-FFI cdef: expected type name, got '%s'\n", t.text.c_str());
        return nullptr;
    }

    /* Handle "struct foo" syntax (unnamed struct already parsed separately) */
    if (t.kind == TOK_STRUCT) {
        t = lex.next(); /* get the struct tag name */
        if (t.kind != TOK_IDENT) {
            fprintf(stderr, "LJ-FFI cdef: expected struct tag after 'struct'\n");
            return nullptr;
        }
    }

    lj_ffi_type_t *base = lj_ffi_type_lookup(t.text.c_str());
    if (!base) {
        fprintf(stderr, "LJ-FFI cdef: unknown type '%s'\n", t.text.c_str());
        return nullptr;
    }

    /* Handle pointer suffix */
    if (allow_pointer && lex.peek().kind == TOK_STAR) {
        lex.next(); /* consume * */
        /* For now, all pointers map to generic pointer type.
         * Future: create typed pointer wrapper. */
        return lj_ffi_type_lookup("pointer");
    }

    return base;
}

/* ============================================================
 * Struct Parser
 *
 * Parses: { field_type field_name; ... }
 * Assumes opening '{' is NOT yet consumed.
 * ============================================================ */

static lj_ffi_type_t *parse_struct_body(lexer &lex, const char *tag_name)
{
    /* Expect '{' */
    token t = lex.next();
    if (t.kind != TOK_LBRACE) {
        fprintf(stderr, "LJ-FFI cdef: expected '{' in struct %s\n", tag_name);
        return nullptr;
    }

    std::vector<lj_ffi_field_t> fields;
    size_t offset = 0;
    size_t max_align = 1;

    while (lex.peek().kind != TOK_RBRACE && !lex.at_end()) {
        /* Parse field type */
        lj_ffi_type_t *ftype = resolve_type(lex);
        if (!ftype) return nullptr;

        /* Parse field name */
        token fname = lex.next();
        if (fname.kind != TOK_IDENT) {
            fprintf(stderr, "LJ-FFI cdef: expected field name in struct %s\n", tag_name);
            return nullptr;
        }

        /* Handle array suffix: field_name[N] */
        int array_len = -1;
        if (lex.peek().kind == TOK_LBRACKET) {
            lex.next(); /* consume [ */
            token num = lex.next();
            if (num.kind == TOK_NUMBER) {
                array_len = atoi(num.text.c_str());
            }
            token rb = lex.next(); /* consume ] */
            (void)rb;
        }

        /* Compute alignment and offset */
        size_t field_size = ftype->size;
        size_t field_align = ftype->alignment;

        if (array_len > 0) {
            field_size *= array_len;
        }

        /* Align offset */
        if (field_align > 0 && (offset % field_align) != 0) {
            offset += field_align - (offset % field_align);
        }

        lj_ffi_field_t field;
        field.name = strdup(fname.text.c_str());
        field.field_type = ftype;
        field.offset = offset;
        fields.push_back(field);

        offset += field_size;
        if (field_align > max_align) max_align = field_align;

        /* Expect ';' */
        t = lex.next();
        if (t.kind != TOK_SEMICOLON) {
            fprintf(stderr, "LJ-FFI cdef: expected ';' after field %s in struct %s\n",
                    fname.text.c_str(), tag_name);
            return nullptr;
        }
    }

    /* Consume '}' */
    t = lex.next();
    if (t.kind != TOK_RBRACE) {
        fprintf(stderr, "LJ-FFI cdef: expected '}' in struct %s\n", tag_name);
        return nullptr;
    }

    /* Final padding to alignment */
    if (max_align > 0 && (offset % max_align) != 0) {
        offset += max_align - (offset % max_align);
    }

    /* Build lj_ffi_type_t for this struct */
    lj_ffi_type_t *st = lj_ffi_type_alloc(LJ_FFI_STRUCT);
    st->size = offset;
    st->alignment = max_align;
    st->num_fields = (int)fields.size();
    st->fields = (lj_ffi_field_t*)malloc(fields.size() * sizeof(lj_ffi_field_t));
    memcpy(st->fields, fields.data(), fields.size() * sizeof(lj_ffi_field_t));

    return st;
}

/* ============================================================
 * Top-Level Declaration Parser
 * ============================================================ */

static int parse_typedef_struct(lexer &lex)
{
    /* Already consumed 'typedef' and 'struct' */
    /* Next could be: tag_name '{' ... '}' alias ';'
     *            or: '{' ... '}' alias ';' (anonymous struct) */

    std::string tag_name = "<anon>";
    if (lex.peek().kind == TOK_IDENT) {
        tag_name = lex.next().text;
    }

    lj_ffi_type_t *st = parse_struct_body(lex, tag_name.c_str());
    if (!st) return -1;

    /* Next should be the typedef alias name */
    token alias = lex.next();
    if (alias.kind != TOK_IDENT) {
        fprintf(stderr, "LJ-FFI cdef: expected alias name after struct body\n");
        free(st);
        return -1;
    }

    /* Register under the alias name */
    st->name = strdup(alias.text.c_str());
    lj_ffi_type_register(alias.text.c_str(), st);

    /* Also register under "struct tag_name" if named */
    if (tag_name != "<anon>") {
        std::string struct_name = "struct " + tag_name;
        /* Create a shallow alias (same pointer, different key) */
        g_type_registry_ref_hack(struct_name, st);
    }

    /* Expect ';' */
    token semi = lex.next();
    if (semi.kind != TOK_SEMICOLON) {
        fprintf(stderr, "LJ-FFI cdef: expected ';' after typedef struct %s\n",
                alias.text.c_str());
        return -1;
    }

    fprintf(stderr, "LJ-FFI cdef: registered struct '%s' (%d fields, %zu bytes)\n",
            alias.text.c_str(), st->num_fields, st->size);
    return 0;
}

static int parse_typedef_simple(lexer &lex)
{
    /* Already consumed 'typedef', next is base type then alias */
    lj_ffi_type_t *base = resolve_type(lex);
    if (!base) return -1;

    token alias = lex.next();
    if (alias.kind != TOK_IDENT) {
        fprintf(stderr, "LJ-FFI cdef: expected alias in typedef\n");
        return -1;
    }

    /* Register alias pointing to same type */
    lj_ffi_type_register(alias.text.c_str(), base);

    token semi = lex.next();
    if (semi.kind != TOK_SEMICOLON) {
        fprintf(stderr, "LJ-FFI cdef: expected ';' after typedef %s\n", alias.text.c_str());
        return -1;
    }

    return 0;
}

static int parse_function_decl(lexer &lex, lj_ffi_type_t *ret_type, const std::string &func_name)
{
    /* Already consumed return type and function name, '(' is next */
    token lp = lex.next();
    if (lp.kind != TOK_LPAREN) {
        fprintf(stderr, "LJ-FFI cdef: expected '(' after function name %s\n", func_name.c_str());
        return -1;
    }

    std::vector<lj_ffi_type_t*> params;

    /* Check for void parameter list: func(void) */
    if (lex.peek().kind == TOK_VOID) {
        lex.next(); /* consume void */
        /* Should be followed by ')' */
    } else {
        /* Parse parameter list */
        while (lex.peek().kind != TOK_RPAREN && !lex.at_end()) {
            lj_ffi_type_t *ptype = resolve_type(lex);
            if (!ptype) return -1;

            /* Parameter name is optional, skip if present */
            if (lex.peek().kind == TOK_IDENT) {
                lex.next(); /* skip param name */
            }

            params.push_back(ptype);

            if (lex.peek().kind == TOK_COMMA) {
                lex.next(); /* consume comma */
            }
        }
    }

    token rp = lex.next();
    if (rp.kind != TOK_RPAREN) {
        fprintf(stderr, "LJ-FFI cdef: expected ')' in function %s\n", func_name.c_str());
        return -1;
    }

    token semi = lex.next();
    if (semi.kind != TOK_SEMICOLON) {
        fprintf(stderr, "LJ-FFI cdef: expected ';' after function %s\n", func_name.c_str());
        return -1;
    }

    lj_ffi_type_t **param_arr = nullptr;
    if (!params.empty()) {
        param_arr = params.data();
    }

    lj_ffi_func_register(func_name.c_str(), ret_type, param_arr, (int)params.size());

    fprintf(stderr, "LJ-FFI cdef: registered function '%s' (%d params)\n",
            func_name.c_str(), (int)params.size());
    return 0;
}

/* ============================================================
 * Public Entry Point
 * ============================================================ */

int lj_ffi_cdef_parse(const char *source)
{
    if (!source) return -1;

    lexer lex(source);
    int errors = 0;

    while (!lex.at_end()) {
        token t = lex.next();

        if (t.kind == TOK_TYPEDEF) {
            /* typedef struct { ... } name;  OR  typedef type alias; */
            if (lex.peek().kind == TOK_STRUCT) {
                lex.next(); /* consume 'struct' */
                if (parse_typedef_struct(lex) != 0) errors++;
            } else {
                if (parse_typedef_simple(lex) != 0) errors++;
            }
        } else if (t.kind == TOK_IDENT || t.kind == TOK_VOID) {
            /* Could be: type func_name(...); */
            lj_ffi_type_t *ret_type;
            if (t.kind == TOK_VOID) {
                ret_type = lj_ffi_type_lookup("void");
            } else {
                ret_type = lj_ffi_type_lookup(t.text.c_str());
            }

            if (!ret_type) {
                fprintf(stderr, "LJ-FFI cdef: unknown type '%s' at top level\n", t.text.c_str());
                errors++;
                /* Try to skip to next semicolon */
                while (!lex.at_end() && lex.next().kind != TOK_SEMICOLON) {}
                continue;
            }

            /* Check for pointer return type */
            if (lex.peek().kind == TOK_STAR) {
                lex.next();
                ret_type = lj_ffi_type_lookup("pointer");
            }

            /* Next should be function name */
            token fname = lex.next();
            if (fname.kind != TOK_IDENT) {
                fprintf(stderr, "LJ-FFI cdef: expected identifier after type '%s'\n", t.text.c_str());
                errors++;
                while (!lex.at_end() && lex.next().kind != TOK_SEMICOLON) {}
                continue;
            }

            /* If next is '(' it's a function declaration */
            if (lex.peek().kind == TOK_LPAREN) {
                if (parse_function_decl(lex, ret_type, fname.text) != 0) errors++;
            } else {
                /* Could be a global variable declaration — skip for now */
                while (!lex.at_end() && lex.next().kind != TOK_SEMICOLON) {}
            }
        } else if (t.kind == TOK_SEMICOLON) {
            /* Empty statement, skip */
        } else {
            /* Unknown top-level token, skip to semicolon */
            while (!lex.at_end() && lex.next().kind != TOK_SEMICOLON) {}
        }
    }

    return (errors > 0) ? -1 : 0;
}
