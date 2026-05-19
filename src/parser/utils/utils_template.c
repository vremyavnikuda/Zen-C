// SPDX-License-Identifier: MIT
#include "plugins/plugin_manager.h"
#include "parser.h"
#include "utils/format_expr.h"
#include "utils/colors.h"
#include "utils/utils.h"
#include "constants.h"
#include "ast/primitives.h"
#include <ctype.h>
#include "analysis/const_fold.h"
#include <stdlib.h>
#include <string.h>

static int is_unmangle_primitive(const char *base);

ASTNode *copy_fields(ASTNode *fields)
{
    if (!fields)
    {
        return NULL;
    }
    ASTNode *n = ast_create(NODE_FIELD);
    n->field.name = xstrdup(fields->field.name);
    n->field.type = xstrdup(fields->field.type);
    n->next = copy_fields(fields->next);
    return n;
}

char *replace_in_string(const char *src, const char *old_w, const char *new_w)
{
    if (!src || !old_w || !new_w)
    {
        return src ? xstrdup(src) : NULL;
    }

    // Check for multiple parameters (comma separated)
    if (strchr(old_w, ','))
    {
        char *running_src = xstrdup(src);

        char *p_ptr = (char *)old_w;
        char *c_ptr = (char *)new_w;

        while (*p_ptr && *c_ptr)
        {
            char *p_end = strchr(p_ptr, ',');
            int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);

            char *c_end = strchr(c_ptr, ',');
            int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

            char *curr_p = xmalloc(p_len + 1);
            strncpy(curr_p, p_ptr, p_len);
            curr_p[p_len] = 0;

            char *curr_c = xmalloc(c_len + 1);
            strncpy(curr_c, c_ptr, c_len);
            curr_c[c_len] = 0;

            char *next_src = replace_in_string(running_src, curr_p, curr_c);
            zfree(running_src);
            running_src = next_src;

            zfree(curr_p);
            zfree(curr_c);

            if (p_end)
            {
                p_ptr = p_end + 1;
            }
            else
            {
                break;
            }
            if (c_end)
            {
                c_ptr = c_end + 1;
            }
            else
            {
                break;
            }
        }
        return running_src;
    }

    char *result;
    int i, cnt = 0;
    int newWlen = strlen(new_w);
    int oldWlen = strlen(old_w);

    // Pass 1: Count replacements
    int in_string = 0;
    for (i = 0; src[i] != '\0'; i++)
    {
        if (src[i] == '\"' && (i == 0 || src[i - 1] != '\\'))
        {
            in_string = !in_string;
        }

        if (!in_string && strstr(&src[i], old_w) == &src[i])
        {
            // Check boundaries
            int valid = 1;
            if (i > 0 && is_ident_char(src[i - 1]))
            {
                valid = 0;
            }
            if (valid && (is_ident_char(src[i + oldWlen]) || src[i + oldWlen] == '<'))
            {
                valid = 0;
            }

            if (valid)
            {
                cnt++;
                i += oldWlen - 1;
            }
        }
    }

    // Allocate result buffer
    result = (char *)xmalloc(i + cnt * (newWlen - oldWlen) + 1);

    // Pass 2: Perform replacement
    int j = 0;
    in_string = 0;

    int src_idx = 0;

    while (src[src_idx] != '\0')
    {
        if (src[src_idx] == '\"' && (src_idx == 0 || src[src_idx - 1] != '\\'))
        {
            in_string = !in_string;
        }

        int replaced = 0;
        if (!in_string && strstr(&src[src_idx], old_w) == &src[src_idx])
        {
            int valid = 1;
            if (src_idx > 0 && is_ident_char(src[src_idx - 1]))
            {
                valid = 0;
            }
            if (valid && (is_ident_char(src[src_idx + oldWlen]) || src[src_idx + oldWlen] == '<'))
            {
                valid = 0;
            }

            if (valid)
            {
                strcpy(&result[j], new_w);
                j += newWlen;
                src_idx += oldWlen;
                replaced = 1;
            }
        }

        if (!replaced)
        {
            result[j++] = src[src_idx++];
        }
    }
    result[j] = '\0';
    return result;
}

Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns);
// Helper to replace generic params in mangled names (e.g. Option_V_None ->
// Option_int_None)
char *replace_mangled_part(const char *src, const char *param, const char *concrete)
{
    if (!src || !param || !concrete)
    {
        return src ? xstrdup(src) : NULL;
    }

    size_t plen = strlen(param);
    size_t clen = strlen(concrete);
    size_t src_len = strlen(src);

    // Initial estimate for result size
    size_t res_cap = src_len + 512;
    char *result = xmalloc(res_cap);
    result[0] = 0;

    const char *curr = src;
    char *out = result;
    size_t current_len = 0;

    while (*curr)
    {
        // Ensure enough space (including the next character or replacement)
        if (current_len + (clen > 1 ? clen : 1) + 1 >= res_cap)
        {
            res_cap = res_cap * 2 + clen;
            char *new_res = xmalloc(res_cap);
            memcpy(new_res, result, current_len);
            zfree(result);
            result = new_res;
            out = result + current_len;
        }

        // Check if param matches here
        if (strncmp(curr, param, plen) == 0)
        {
            int valid = 1;
            int has_underscore_boundary = 0;

            if (curr > src)
            {
                if (*(curr - 1) == '_')
                {
                    has_underscore_boundary = 1;
                }
                else if (is_ident_char(*(curr - 1)))
                {
                    valid = 0;
                }
            }

            if (valid && curr[plen] != 0 && curr[plen] != '_' && is_ident_char(curr[plen]))
            {
                if (strncmp(curr + plen, "Ptr", 3) != 0)
                {
                    valid = 0;
                }
            }
            if (valid && curr[plen] == '_')
            {
                has_underscore_boundary = 1;
            }

            if (valid && !has_underscore_boundary)
            {
                // Also allow <, ,, (, [ as boundaries
                char prev = (curr > src) ? *(curr - 1) : 0;
                if (prev == '<' || prev == ',' || prev == '(' || prev == '[' || prev == ' ')
                {
                    // OK
                }
                else
                {
                    valid = 0;
                }
            }

            if (valid)
            {
                // Ensure double underscore boundary for the replacement
                if (curr > src && *(curr - 1) == '_' && (curr == src + 1 || *(curr - 2) != '_'))
                {
                    *out++ = '_';
                    current_len++;
                }

                memcpy(out, concrete, clen);
                out += clen;
                current_len += clen;

                if (curr[plen] == '_' && curr[plen + 1] != '_')
                {
                    *out++ = '_';
                    current_len++;
                }

                curr += plen;
                continue;
            }
        }
        *out++ = *curr++;
        current_len++;
    }
    *out = 0;
    return result;
}

char *replace_type_str(const char *src, const char *param, const char *concrete,
                       const char *old_struct, const char *new_struct)
{
    if (!src)
    {
        return NULL;
    }
    if (!param || !concrete)
    {
        return xstrdup(src);
    }

    // 1. Exact match (base case)
    if (strcmp(src, param) == 0)
    {
        return xstrdup(concrete);
    }

    // 2. Handle simple pointer cases recursively (safe as src shrinks)
    size_t slen = strlen(src);
    if (slen > 1 && src[slen - 1] == '*')
    {
        char *base = xmalloc(slen);
        strncpy(base, src, slen - 1);
        base[slen - 1] = 0;
        char *nb = replace_type_str(base, param, concrete, old_struct, new_struct);
        char *res = xmalloc(strlen(nb) + 2);
        sprintf(res, "%s*", nb);
        zfree(base);
        zfree(nb);
        return res;
    }

    // 3. Structural fallback for complex strings (e.g. "Self", "Option<T>")
    char *res = xstrdup(src);

    // Case 3a: Explicit template replacement (e.g. Vec<T> -> Vec__int32_t)
    if (old_struct && new_struct && param)
    {
        char tpl_w[MAX_TYPE_NAME_LEN];
        snprintf(tpl_w, sizeof(tpl_w), "%s<%s>", old_struct, param);
        if (strstr(res, tpl_w))
        {
            char *tmp = replace_in_string(res, tpl_w, new_struct);
            zfree(res);
            res = tmp;
        }
    }

    // Case 3b: Base struct replacement (e.g. Vec -> Vec__int32_t)
    if (old_struct && new_struct && strstr(res, old_struct))
    {
        char *tmp = replace_in_string(res, old_struct, new_struct);
        zfree(res);
        res = tmp;
    }

    // 4. Boundary-safe mangled replacement (e.g. "Option_T" or "Option__T")
    // Split multi-param strings (X, Y, Z) and replace each individually
    char *final_res = xstrdup(res);
    if (param && concrete && strchr(param, ','))
    {
        char *p_ptr = (char *)param;
        char *c_ptr = (char *)concrete;
        while (*p_ptr && *c_ptr)
        {
            char *p_end = strchr(p_ptr, ',');
            int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
            char *c_end = strchr(c_ptr, ',');
            int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

            char *p_part = xmalloc(p_len + 1);
            strncpy(p_part, p_ptr, p_len);
            p_part[p_len] = 0;

            char *c_part = xmalloc(c_len + 1);
            strncpy(c_part, c_ptr, c_len);
            c_part[c_len] = 0;

            char *clean_c = sanitize_mangled_name(c_part);
            char *tmp = replace_mangled_part(final_res, p_part, clean_c);
            zfree(final_res);
            final_res = tmp;

            zfree(p_part);
            zfree(c_part);
            zfree(clean_c);

            if (p_end)
            {
                p_ptr = p_end + 1;
            }
            else
            {
                break;
            }
            if (c_end)
            {
                c_ptr = c_end + 1;
            }
            else
            {
                break;
            }
        }
    }
    else
    {
        char *t1 = replace_in_string(final_res, param, concrete);
        zfree(final_res);
        final_res = t1;

        char *clean_c = sanitize_mangled_name(concrete);
        char *tmp = replace_mangled_part(final_res, param, clean_c);
        zfree(final_res);
        final_res = tmp;
        zfree(clean_c);
    }

    zfree(res);
    return final_res;
}

ASTNode *copy_ast_replacing(ASTNode *n, const char *p, const char *c, const char *os,
                            const char *ns);

Type *type_from_string_helper(const char *c)
{
    if (!c)
    {
        return NULL;
    }

    // Check for pointer suffix '*'
    size_t len = strlen(c);
    if (len > 0 && c[len - 1] == '*')
    {
        size_t base_len = len - 1;
        char *base = xmalloc(base_len + 1);
        strncpy(base, c, base_len);
        base[base_len] = 0;

        Type *inner = type_from_string_helper(base);
        zfree(base);

        return type_new_ptr(inner);
    }

    // Check for 'const ' prefix
    if (strncmp(c, "const ", 6) == 0)
    {
        Type *inner = type_from_string_helper(c + 6);
        if (inner)
        {
            inner->is_const = 1;
        }
        return inner;
    }

    if (strncmp(c, "struct ", 7) == 0)
    {
        Type *n = type_new(TYPE_STRUCT);
        n->name = sanitize_mangled_name(c + 7);
        n->is_explicit_struct = 1;
        return n;
    }

    if (strcmp(c, "int") == 0)
    {
        return type_new(TYPE_INT);
    }
    if (strcmp(c, "float") == 0)
    {
        return type_new(TYPE_FLOAT);
    }
    if (strcmp(c, "void") == 0)
    {
        return type_new(TYPE_VOID);
    }
    if (strcmp(c, "string") == 0)
    {
        return type_new(TYPE_STRING);
    }
    if (strcmp(c, "bool") == 0)
    {
        return type_new(TYPE_BOOL);
    }
    if (strcmp(c, "char") == 0)
    {
        return type_new(TYPE_CHAR);
    }
    if (strcmp(c, "I8") == 0 || strcmp(c, "i8") == 0)
    {
        return type_new(TYPE_I8);
    }
    if (strcmp(c, "U8") == 0 || strcmp(c, "u8") == 0)
    {
        return type_new(TYPE_U8);
    }
    if (strcmp(c, "I16") == 0 || strcmp(c, "i16") == 0)
    {
        return type_new(TYPE_I16);
    }
    if (strcmp(c, "U16") == 0 || strcmp(c, "u16") == 0)
    {
        return type_new(TYPE_U16);
    }
    if (strcmp(c, "I32") == 0 || strcmp(c, "i32") == 0 || strcmp(c, "int32_t") == 0)
    {
        return type_new(TYPE_I32);
    }
    if (strcmp(c, "U32") == 0 || strcmp(c, "u32") == 0 || strcmp(c, "uint32_t") == 0)
    {
        return type_new(TYPE_U32);
    }
    if (strcmp(c, "I64") == 0 || strcmp(c, "i64") == 0 || strcmp(c, "int64_t") == 0)
    {
        return type_new(TYPE_I64);
    }
    if (strcmp(c, "U64") == 0 || strcmp(c, "u64") == 0 || strcmp(c, "uint64_t") == 0)
    {
        return type_new(TYPE_U64);
    }
    if (strcmp(c, "float") == 0 || strcmp(c, "f32") == 0)
    {
        return type_new(TYPE_F32);
    }
    if (strcmp(c, "double") == 0 || strcmp(c, "f64") == 0)
    {
        return type_new(TYPE_F64);
    }
    if (strcmp(c, "I128") == 0 || strcmp(c, "i128") == 0)
    {
        return type_new(TYPE_I128);
    }
    if (strcmp(c, "U128") == 0 || strcmp(c, "u128") == 0)
    {
        return type_new(TYPE_U128);
    }
    if (strcmp(c, "rune") == 0)
    {
        return type_new(TYPE_RUNE);
    }
    if (strcmp(c, "uint") == 0)
    {
        return type_new(TYPE_UINT);
    }

    if (strcmp(c, "byte") == 0)
    {
        return type_new(TYPE_BYTE);
    }
    if (strcmp(c, "usize") == 0)
    {
        return type_new(TYPE_USIZE);
    }
    if (strcmp(c, "isize") == 0)
    {
        return type_new(TYPE_ISIZE);
    }

    Type *n = type_new(TYPE_STRUCT);
    n->name = sanitize_mangled_name(c);
    return n;
}

Type *replace_type_formal(Type *t, const char *p, const char *c, const char *os, const char *ns)
{
    if (!t || (uintptr_t)t < 0x10000)
    {
        return NULL;
    }

    // Defensive check: Ensure kind is valid
    if ((int)t->kind < 0 || (int)t->kind > 100) // 100 is a safe upper bound for TypeKind
    {
        return NULL;
    }

    // Exact Match Logic (with multi-param splitting)
    if ((t->kind == TYPE_STRUCT || t->kind == TYPE_GENERIC) && t->name)
    {

        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                if ((int)strlen(t->name) == p_len && strncmp(t->name, p_ptr, p_len) == 0)
                {
                    char *c_part = xmalloc(c_len + 1);
                    strncpy(c_part, c_ptr, c_len);
                    c_part[c_len] = 0;

                    Type *res = type_from_string_helper(c_part);
                    zfree(c_part);
                    return res;
                }
                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else if (p && strcmp(t->name, p) == 0)
        {
            return type_from_string_helper(c);
        }
    }

    Type *n = xmalloc(sizeof(Type));
    *n = *t;

    if (t->name)
    {
        if (os && ns && strcmp(t->name, os) == 0)
        {
            n->name = xstrdup(ns);
            n->kind = TYPE_STRUCT;
            n->arg_count = 0;
            n->args = NULL;
        }
        else if (p && c)
        {
            // Suffix Match Logic (with multi-param splitting)
            char p_suffix[4096];
            p_suffix[0] = 0;

            const char *p_ptr = p;
            while (p_ptr && *p_ptr)
            {
                const char *p_next = strchr(p_ptr, ',');
                int sub_len = p_next ? (int)(p_next - p_ptr) : (int)strlen(p_ptr);
                char *sub = xmalloc(sub_len + 1);
                strncpy(sub, p_ptr, sub_len);
                sub[sub_len] = 0;

                char *clean_sub = sanitize_mangled_name(sub);
                strcat(p_suffix, "__");
                strcat(p_suffix, clean_sub);
                zfree(clean_sub);
                zfree(sub);

                if (p_next)
                {
                    p_ptr = p_next + 1;
                }
                else
                {
                    break;
                }
            }

            size_t nlen = strlen(t->name);
            size_t slen = strlen(p_suffix);

            int match = 0;
            int found_slen = 0;
            int num_ptr_suffixes = 0;
            if (nlen >= slen && strcmp(t->name + nlen - slen, p_suffix) == 0)
            {
                match = 1;
                found_slen = slen;
            }
            else if (nlen > slen)
            {
                // Try matching with Ptr suffix
                const char *p_match = strstr(t->name, p_suffix);
                while (p_match)
                {
                    const char *after = p_match + slen;
                    int is_all_ptr = 1;
                    if (*after == '\0')
                    {
                        is_all_ptr = 0; // Handled by exact match above
                    }
                    while (*after)
                    {
                        if (strncmp(after, "Ptr", 3) == 0)
                        {
                            after += 3;
                        }
                        else
                        {
                            is_all_ptr = 0;
                            break;
                        }
                    }
                    if (is_all_ptr)
                    {
                        match = 1;
                        found_slen = nlen - (p_match - t->name);
                        num_ptr_suffixes = (nlen - (p_match - t->name) - slen) / 3;
                        break;
                    }
                    p_match = strstr(p_match + 1, p_suffix);
                }
            }

            if (match)
            {
                slen = found_slen;
                char c_suffix[MAX_ERROR_MSG_LEN];
                c_suffix[0] = 0;
                const char *c_ptr = c;
                while (c_ptr && *c_ptr)
                {
                    const char *c_next = strchr(c_ptr, ',');
                    int sub_len = c_next ? (int)(c_next - c_ptr) : (int)strlen(c_ptr);

                    char *sub = xmalloc(sub_len + 1);
                    strncpy(sub, c_ptr, sub_len);
                    sub[sub_len] = 0;

                    char *clean = sanitize_mangled_name(sub);
                    // Standardize: always use __ for mangled part
                    strcat(c_suffix, "__");
                    strcat(c_suffix, clean);
                    zfree(clean);
                    zfree(sub);

                    if (c_next)
                    {
                        c_ptr = c_next + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                // Calculate required size more accurately
                size_t c_suffix_len = strlen(c_suffix);
                size_t total_needed =
                    (nlen > slen ? nlen - slen : 0) + c_suffix_len + (num_ptr_suffixes * 3) + 1;
                char *new_name = xmalloc(total_needed);
                if (nlen > slen)
                {
                    strncpy(new_name, t->name, nlen - slen);
                    new_name[nlen - slen] = 0;
                }
                else
                {
                    new_name[0] = 0;
                }

                // Handle underscore merging: ensure exactly two underscores
                char *p_end = new_name + strlen(new_name);
                while (p_end > new_name && *(p_end - 1) == '_')
                {
                    *(--p_end) = '\0';
                }
                strcat(new_name, c_suffix);

                // Restore Ptr suffixes
                for (int k = 0; k < num_ptr_suffixes; k++)
                {
                    strcat(new_name, "Ptr");
                }
                n->name = new_name;
                n->kind = TYPE_STRUCT;
                n->arg_count = 0;
                n->args = NULL;
            }
            else
            {
                n->name = xstrdup(t->name);
            }
        }
        else
        {
            n->name = xstrdup(t->name);
        }
    }

    if (t->kind == TYPE_POINTER || t->kind == TYPE_ARRAY || t->kind == TYPE_FUNCTION ||
        t->kind == TYPE_VECTOR)
    {
        n->inner = replace_type_formal(t->inner, p, c, os, ns);
    }

    if (t->arg_count > 0 && t->args)
    {
        n->args = xmalloc(sizeof(Type *) * t->arg_count);
        for (int i = 0; i < t->arg_count; i++)
        {
            n->args[i] = replace_type_formal(t->args[i], p, c, os, ns);
        }
    }

    return n;
}

ASTNode *copy_ast_replacing(ASTNode *n, const char *p, const char *c, const char *os,
                            const char *ns)
{
    if (!n)
    {
        return NULL;
    }

    ASTNode *new_node = ast_create(n->type);
    ASTNode *old_next =
        new_node->next; // Preserve next if ast_create did something (it doesn't currently)
    *new_node = *n;
    new_node->next = old_next; // Restore next before recursion

    if (n->resolved_type)
    {
        new_node->resolved_type = replace_type_str(n->resolved_type, p, c, os, ns);
    }
    new_node->type_info = replace_type_formal(n->type_info, p, c, os, ns);

    new_node->next = copy_ast_replacing(n->next, p, c, os, ns);

    switch (n->type)
    {
    case NODE_FUNCTION:
        new_node->func.name = n->func.name ? xstrdup(n->func.name) : NULL;
        new_node->func.ret_type = replace_type_str(n->func.ret_type, p, c, os, ns);

        char *tmp_args = n->func.args ? xstrdup(n->func.args) : NULL;
        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                char *p_part = xmalloc(p_len + 1);
                strncpy(p_part, p_ptr, p_len);
                p_part[p_len] = 0;

                char *c_part = xmalloc(c_len + 1);
                strncpy(c_part, c_ptr, c_len);
                c_part[c_len] = 0;

                char *t1 = replace_in_string(tmp_args, p_part, c_part);
                zfree(tmp_args);
                tmp_args = t1;

                char *clean_c = sanitize_mangled_name(c_part);
                char *t2 = replace_mangled_part(tmp_args, p_part, clean_c);
                zfree(tmp_args);
                tmp_args = t2;

                zfree(p_part);
                zfree(c_part);
                zfree(clean_c);

                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            char *t1 = replace_in_string(tmp_args, p, c);
            zfree(tmp_args);
            tmp_args = t1;

            if (p && c)
            {
                char *clean_c = sanitize_mangled_name(c);
                char *t2 = replace_mangled_part(tmp_args, p, clean_c);
                zfree(tmp_args);
                tmp_args = t2;
                zfree(clean_c);
            }
        }

        if (os && ns)
        {
            char *tmp2 = replace_in_string(tmp_args, os, ns);
            zfree(tmp_args);
            tmp_args = tmp2;
        }
        new_node->func.arg_count = n->func.arg_count;
        if (n->func.arg_count > 0 && n->func.arg_types)
        {
            new_node->func.arg_types = xmalloc(sizeof(Type *) * n->func.arg_count);
            for (int i = 0; i < n->func.arg_count; i++)
            {
                new_node->func.arg_types[i] =
                    replace_type_formal(n->func.arg_types[i], p, c, os, ns);
            }
        }
        else
        {
            new_node->func.arg_types = NULL;
        }
        new_node->func.args = tmp_args;

        new_node->func.ret_type_info = replace_type_formal(n->func.ret_type_info, p, c, os, ns);

        // Deep copy default values AST if present
        if (n->func.default_values && n->func.arg_count > 0)
        {
            new_node->func.default_values = xmalloc(sizeof(ASTNode *) * n->func.arg_count);
            // We also need to regenerate the string defaults array based on the substituted ASTs
            // This ensures potential generic params in default values (T{}) are updated (i32{})
            // in the string representation used by codegen.
            char **new_defaults_strs = xmalloc(sizeof(char *) * n->func.arg_count);

            for (int i = 0; i < n->func.arg_count; i++)
            {
                if (n->func.default_values[i])
                {
                    new_node->func.default_values[i] =
                        copy_ast_replacing(n->func.default_values[i], p, c, os, ns);
                    new_defaults_strs[i] = ast_to_string(new_node->func.default_values[i]);
                }
                else
                {
                    new_node->func.default_values[i] = NULL;
                    new_defaults_strs[i] = NULL;
                }
            }
            // Replace the old string-based defaults with our regenerated ones
            // Note: We leak the old 'tmp_args' calculated above, but that's just a single string
            // for valid args The 'defaults' array in func struct is what matters for function
            // definition. Wait, NODE_FUNCTION has char *args (legacy) AND char **defaults (array).
            // parse_and_convert_args populated both.
            // We need to update new_node->func.defaults.
            new_node->func.defaults = new_defaults_strs;
        }

        new_node->func.body = copy_ast_replacing(n->func.body, p, c, os, ns);
        break;
    case NODE_BLOCK:
        new_node->block.statements = copy_ast_replacing(n->block.statements, p, c, os, ns);
        break;
    case NODE_RAW_STMT:
    {
        char *s1 = xstrdup(n->raw_stmt.content);
        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                char *p_part = xmalloc(p_len + 1);
                strncpy(p_part, p_ptr, p_len);
                p_part[p_len] = 0;

                char *c_part = xmalloc(c_len + 1);
                strncpy(c_part, c_ptr, c_len);
                c_part[c_len] = 0;

                char *t1 = replace_in_string(s1, p_part, c_part);
                zfree(s1);
                s1 = t1;

                char *clean_c = sanitize_mangled_name(c_part);
                char *t2 = replace_mangled_part(s1, p_part, clean_c);
                zfree(s1);
                s1 = t2;

                zfree(p_part);
                zfree(c_part);
                zfree(clean_c);

                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            char *t1 = replace_in_string(s1, p, c);
            zfree(s1);
            s1 = t1;

            if (p && c)
            {
                char *clean_c = sanitize_mangled_name(c);
                char *t2 = replace_mangled_part(s1, p, clean_c);
                zfree(s1);
                s1 = t2;
                zfree(clean_c);
            }
        }

        if (os && ns)
        {
            char *s2 = replace_in_string(s1, os, ns);
            zfree(s1);
            s1 = s2;
        }

        new_node->raw_stmt.content = s1;
    }
    break;
    case NODE_VAR_DECL:
        new_node->var_decl.name = n->var_decl.name ? xstrdup(n->var_decl.name) : NULL;
        new_node->var_decl.type_str = replace_type_str(n->var_decl.type_str, p, c, os, ns);
        new_node->var_decl.type_info = replace_type_formal(n->var_decl.type_info, p, c, os, ns);
        new_node->var_decl.init_expr = copy_ast_replacing(n->var_decl.init_expr, p, c, os, ns);
        break;
    case NODE_RETURN:
        new_node->ret.value = copy_ast_replacing(n->ret.value, p, c, os, ns);
        break;
    case NODE_EXPR_BINARY:
        new_node->binary.left = copy_ast_replacing(n->binary.left, p, c, os, ns);
        new_node->binary.right = copy_ast_replacing(n->binary.right, p, c, os, ns);
        new_node->binary.op = n->binary.op ? xstrdup(n->binary.op) : NULL;
        break;
    case NODE_EXPR_UNARY:
        new_node->unary.op = n->unary.op ? xstrdup(n->unary.op) : NULL;
        new_node->unary.operand = copy_ast_replacing(n->unary.operand, p, c, os, ns);
        break;
    case NODE_EXPR_CALL:
        new_node->call.callee = copy_ast_replacing(n->call.callee, p, c, os, ns);
        new_node->call.args = copy_ast_replacing(n->call.args, p, c, os, ns);
        new_node->call.arg_names = n->call.arg_names; // Share pointer (shallow copy)
        new_node->call.arg_count = n->call.arg_count;
        break;
    case NODE_EXPR_VAR:
    {
        char *n1 = n->var_ref.name ? xstrdup(n->var_ref.name) : NULL;
        if (p && c && strchr(p, ','))
        {
            char *p_ptr = (char *)p;
            char *c_ptr = (char *)c;
            while (*p_ptr && *c_ptr)
            {
                char *p_end = strchr(p_ptr, ',');
                int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                char *c_end = strchr(c_ptr, ',');
                int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                char *p_part = xmalloc(p_len + 1);
                strncpy(p_part, p_ptr, p_len);
                p_part[p_len] = 0;

                char *c_part = xmalloc(c_len + 1);
                strncpy(c_part, c_ptr, c_len);
                c_part[c_len] = 0;

                char *t1 = replace_in_string(n1, p_part, c_part);
                zfree(n1);
                n1 = t1;

                char *clean_c = sanitize_mangled_name(c_part);
                char *t2 = replace_mangled_part(n1, p_part, clean_c);
                zfree(n1);
                n1 = t2;

                zfree(p_part);
                zfree(c_part);
                zfree(clean_c);

                if (p_end)
                {
                    p_ptr = p_end + 1;
                }
                else
                {
                    break;
                }
                if (c_end)
                {
                    c_ptr = c_end + 1;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            if (p && c)
            {
                char *t1 = replace_in_string(n1, p, c);
                zfree(n1);
                n1 = t1;

                char *clean_c = sanitize_mangled_name(c);
                char *n2 = replace_mangled_part(n1, p, clean_c);
                zfree(clean_c);
                zfree(n1);
                n1 = n2;
            }
        }

        if (os && ns)
        {
            int os_len = strlen(os);
            int ns_len = strlen(ns);
            // Only replace if it starts with os__ and DOES NOT already start with ns__
            if (strncmp(n1, os, os_len) == 0 && n1[os_len] == '_' && n1[os_len + 1] == '_' &&
                strncmp(n1, ns, ns_len) != 0)
            {
                char *suffix = n1 + os_len;
                char buf[MAX_ERROR_MSG_LEN];
                snprintf(buf, sizeof(buf), "%s%s", ns, suffix);
                char *n3 = merge_underscores(buf);
                zfree(n1);
                n1 = n3;
            }
        }
        new_node->var_ref.name = n1;
    }
    break;
    case NODE_FIELD:
        new_node->field.name = n->field.name ? xstrdup(n->field.name) : NULL;
        new_node->field.type = replace_type_str(n->field.type, p, c, os, ns);
        break;
    case NODE_EXPR_LITERAL:
        if (n->literal.type_kind == LITERAL_STRING)
        {
            new_node->literal.string_val =
                n->literal.string_val ? xstrdup(n->literal.string_val) : NULL;
        }
        break;
    case NODE_EXPR_MEMBER:
        new_node->member.target = copy_ast_replacing(n->member.target, p, c, os, ns);
        new_node->member.field = n->member.field ? xstrdup(n->member.field) : NULL;
        break;
    case NODE_EXPR_INDEX:
        new_node->index.array = copy_ast_replacing(n->index.array, p, c, os, ns);
        new_node->index.index = copy_ast_replacing(n->index.index, p, c, os, ns);
        break;
    case NODE_EXPR_CAST:
        new_node->cast.target_type = replace_type_str(n->cast.target_type, p, c, os, ns);
        new_node->cast.expr = copy_ast_replacing(n->cast.expr, p, c, os, ns);
        break;
    case NODE_EXPR_STRUCT_INIT:
    {
        char *new_name = replace_type_str(n->struct_init.struct_name, p, c, os, ns);

        int is_ptr = 0;
        size_t len = strlen(new_name);
        if (len > 0 && new_name[len - 1] == '*')
        {
            is_ptr = 1;
        }

        int is_primitive = is_primitive_type_name(new_name);

        if ((is_ptr || is_primitive) && !n->struct_init.fields)
        {
            new_node->type = NODE_EXPR_LITERAL;
            new_node->literal.type_kind = LITERAL_INT;
            new_node->literal.int_val = 0;
            zfree(new_name);
        }
        else
        {
            new_node->struct_init.struct_name = new_name;
            ASTNode *h = NULL, *t = NULL, *curr = n->struct_init.fields;
            while (curr)
            {
                ASTNode *cp = copy_ast_replacing(curr, p, c, os, ns);
                cp->next = NULL;
                if (!h)
                {
                    h = cp;
                }
                else
                {
                    t->next = cp;
                }
                t = cp;
                curr = curr->next;
            }
            new_node->struct_init.fields = h;
        }
        break;
    }
    case NODE_IF:
        new_node->if_stmt.condition = copy_ast_replacing(n->if_stmt.condition, p, c, os, ns);
        new_node->if_stmt.then_body = copy_ast_replacing(n->if_stmt.then_body, p, c, os, ns);
        new_node->if_stmt.else_body = copy_ast_replacing(n->if_stmt.else_body, p, c, os, ns);
        break;
    case NODE_WHILE:
        new_node->while_stmt.condition = copy_ast_replacing(n->while_stmt.condition, p, c, os, ns);
        new_node->while_stmt.body = copy_ast_replacing(n->while_stmt.body, p, c, os, ns);
        break;
    case NODE_FOR:
        new_node->for_stmt.init = copy_ast_replacing(n->for_stmt.init, p, c, os, ns);
        new_node->for_stmt.condition = copy_ast_replacing(n->for_stmt.condition, p, c, os, ns);
        new_node->for_stmt.step = copy_ast_replacing(n->for_stmt.step, p, c, os, ns);
        new_node->for_stmt.body = copy_ast_replacing(n->for_stmt.body, p, c, os, ns);
        break;
    case NODE_FOR_RANGE:
        new_node->for_range.start = copy_ast_replacing(n->for_range.start, p, c, os, ns);
        new_node->for_range.end = copy_ast_replacing(n->for_range.end, p, c, os, ns);
        new_node->for_range.body = copy_ast_replacing(n->for_range.body, p, c, os, ns);
        break;

    case NODE_MATCH_CASE:
        if (n->match_case.pattern)
        {
            char *s1 = xstrdup(n->match_case.pattern);
            if (p && c && strchr(p, ','))
            {
                char *p_ptr = (char *)p;
                char *c_ptr = (char *)c;
                while (*p_ptr && *c_ptr)
                {
                    char *p_end = strchr(p_ptr, ',');
                    int p_len = p_end ? (int)(p_end - p_ptr) : (int)strlen(p_ptr);
                    char *c_end = strchr(c_ptr, ',');
                    int c_len = c_end ? (int)(c_end - c_ptr) : (int)strlen(c_ptr);

                    char *p_part = xmalloc(p_len + 1);
                    strncpy(p_part, p_ptr, p_len);
                    p_part[p_len] = 0;

                    char *c_part = xmalloc(c_len + 1);
                    strncpy(c_part, c_ptr, c_len);
                    c_part[c_len] = 0;

                    char *t1 = replace_mangled_part(s1, p_part, c_part);
                    zfree(s1);
                    s1 = t1;

                    zfree(p_part);
                    zfree(c_part);

                    if (p_end)
                    {
                        p_ptr = p_end + 1;
                    }
                    else
                    {
                        break;
                    }
                    if (c_end)
                    {
                        c_ptr = c_end + 1;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            else
            {
                char *t1 = replace_in_string(s1, p, c);
                zfree(s1);
                s1 = t1;
                char *t2 = replace_mangled_part(s1, p, c);
                zfree(s1);
                s1 = t2;
            }

            if (os && ns)
            {
                char *s2 = replace_in_string(s1, os, ns);
                zfree(s1);
                s1 = s2;
                char *colons = strstr(s1, "::");
                if (colons)
                {
                    colons[0] = '_';
                    memmove(colons + 1, colons + 2, strlen(colons + 2) + 1);
                }
            }
            new_node->match_case.pattern = s1;
        }
        new_node->match_case.binding_count = n->match_case.binding_count;
        if (n->match_case.binding_names)
        {
            new_node->match_case.binding_names =
                xmalloc(sizeof(char *) * n->match_case.binding_count);
            for (int i = 0; i < n->match_case.binding_count; i++)
            {
                if (n->match_case.binding_names[i])
                {
                    new_node->match_case.binding_names[i] = xstrdup(n->match_case.binding_names[i]);
                }
                else
                {
                    new_node->match_case.binding_names[i] = NULL;
                }
            }
        }
        if (n->match_case.binding_refs)
        {
            new_node->match_case.binding_refs = xmalloc(sizeof(int) * n->match_case.binding_count);
            memcpy(new_node->match_case.binding_refs, n->match_case.binding_refs,
                   sizeof(int) * n->match_case.binding_count);
        }
        new_node->match_case.is_default = n->match_case.is_default;
        new_node->match_case.is_destructuring = n->match_case.is_destructuring;

        new_node->match_case.body = copy_ast_replacing(n->match_case.body, p, c, os, ns);
        if (n->match_case.guard)
        {
            new_node->match_case.guard = copy_ast_replacing(n->match_case.guard, p, c, os, ns);
        }
        break;

    case NODE_IMPL:
        new_node->impl.struct_name = replace_type_str(n->impl.struct_name, p, c, os, ns);
        new_node->impl.methods = copy_ast_replacing(n->impl.methods, p, c, os, ns);
        break;
    case NODE_IMPL_TRAIT:
        new_node->impl_trait.trait_name =
            n->impl_trait.trait_name ? xstrdup(n->impl_trait.trait_name) : NULL;
        new_node->impl_trait.target_type =
            replace_type_str(n->impl_trait.target_type, p, c, os, ns);
        new_node->impl_trait.methods = copy_ast_replacing(n->impl_trait.methods, p, c, os, ns);
        break;
    case NODE_TYPEOF:
    case NODE_EXPR_SIZEOF:
        new_node->size_of.target_type = replace_type_str(n->size_of.target_type, p, c, os, ns);
        new_node->size_of.expr = copy_ast_replacing(n->size_of.expr, p, c, os, ns);
        new_node->size_of.is_type = n->size_of.is_type;
        if (n->size_of.target_type_info)
        {
            new_node->size_of.target_type_info =
                replace_type_formal(n->size_of.target_type_info, p, c, os, ns);
        }
        break;
    case NODE_LAMBDA:
        // Use a new lambda ID for each instantiation to ensure unique C function names
        new_node->lambda.lambda_id = g_parser_ctx->lambda_counter++;
        new_node->lambda.num_params = n->lambda.num_params;
        if (n->lambda.num_params > 0)
        {
            new_node->lambda.param_names = xmalloc(sizeof(char *) * n->lambda.num_params);
            new_node->lambda.param_types = xmalloc(sizeof(char *) * n->lambda.num_params);
            for (int i = 0; i < n->lambda.num_params; i++)
            {
                new_node->lambda.param_names[i] = xstrdup(n->lambda.param_names[i]);
                new_node->lambda.param_types[i] =
                    replace_type_str(n->lambda.param_types[i], p, c, os, ns);
            }
        }
        new_node->lambda.return_type = replace_type_str(n->lambda.return_type, p, c, os, ns);
        new_node->lambda.num_captures = n->lambda.num_captures;
        if (n->lambda.num_captures > 0)
        {
            new_node->lambda.captured_vars = xmalloc(sizeof(char *) * n->lambda.num_captures);
            new_node->lambda.captured_types = xmalloc(sizeof(char *) * n->lambda.num_captures);
            new_node->lambda.captured_types_info = xmalloc(sizeof(Type *) * n->lambda.num_captures);
            if (n->lambda.capture_modes)
            {
                new_node->lambda.capture_modes = xmalloc(sizeof(int) * n->lambda.num_captures);
            }

            for (int i = 0; i < n->lambda.num_captures; i++)
            {
                new_node->lambda.captured_vars[i] = xstrdup(n->lambda.captured_vars[i]);
                new_node->lambda.captured_types[i] =
                    replace_type_str(n->lambda.captured_types[i], p, c, os, ns);
                new_node->lambda.captured_types_info[i] =
                    replace_type_formal(n->lambda.captured_types_info[i], p, c, os, ns);
                if (n->lambda.capture_modes)
                {
                    new_node->lambda.capture_modes[i] = n->lambda.capture_modes[i];
                }
            }
        }
        new_node->lambda.body = copy_ast_replacing(n->lambda.body, p, c, os, ns);
        new_node->lambda.is_bare = n->lambda.is_bare;
        register_lambda(g_parser_ctx, new_node);
        break;
    case NODE_DESTRUCT_VAR:
        if (n->destruct.count > 0)
        {
            new_node->destruct.names = xmalloc(sizeof(char *) * n->destruct.count);
            new_node->destruct.types = xmalloc(sizeof(char *) * n->destruct.count);
            new_node->destruct.type_infos = xmalloc(sizeof(Type *) * n->destruct.count);
            if (n->destruct.field_names)
            {
                new_node->destruct.field_names = xmalloc(sizeof(char *) * n->destruct.count);
            }

            for (int i = 0; i < n->destruct.count; i++)
            {
                new_node->destruct.names[i] = xstrdup(n->destruct.names[i]);
                new_node->destruct.types[i] = replace_type_str(n->destruct.types[i], p, c, os, ns);
                new_node->destruct.type_infos[i] =
                    replace_type_formal(n->destruct.type_infos[i], p, c, os, ns);
                if (n->destruct.field_names && n->destruct.field_names[i])
                {
                    new_node->destruct.field_names[i] = xstrdup(n->destruct.field_names[i]);
                }
                else if (n->destruct.field_names)
                {
                    new_node->destruct.field_names[i] = NULL;
                }
            }
        }
        new_node->destruct.init_expr = copy_ast_replacing(n->destruct.init_expr, p, c, os, ns);
        new_node->destruct.struct_name = replace_type_str(n->destruct.struct_name, p, c, os, ns);
        new_node->destruct.else_block = copy_ast_replacing(n->destruct.else_block, p, c, os, ns);
        break;
    case NODE_MATCH:
        new_node->match_stmt.expr = copy_ast_replacing(n->match_stmt.expr, p, c, os, ns);
        new_node->match_stmt.cases = copy_ast_replacing(n->match_stmt.cases, p, c, os, ns);
        break;
    case NODE_LOOP:
        new_node->loop_stmt.body = copy_ast_replacing(n->loop_stmt.body, p, c, os, ns);
        break;
    case NODE_REPEAT:
        new_node->repeat_stmt.count = n->repeat_stmt.count ? xstrdup(n->repeat_stmt.count) : NULL;
        new_node->repeat_stmt.body = copy_ast_replacing(n->repeat_stmt.body, p, c, os, ns);
        break;
    case NODE_UNLESS:
        new_node->unless_stmt.condition =
            copy_ast_replacing(n->unless_stmt.condition, p, c, os, ns);
        new_node->unless_stmt.body = copy_ast_replacing(n->unless_stmt.body, p, c, os, ns);
        break;
    case NODE_GUARD:
        new_node->guard_stmt.condition = copy_ast_replacing(n->guard_stmt.condition, p, c, os, ns);
        new_node->guard_stmt.body = copy_ast_replacing(n->guard_stmt.body, p, c, os, ns);
        break;
    case NODE_BREAK:
    case NODE_CONTINUE:
        // No members to copy besides next (handled at end)
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        new_node->array_literal.elements =
            copy_ast_replacing(n->array_literal.elements, p, c, os, ns);
        new_node->array_literal.count = n->array_literal.count;
        break;
    case NODE_EXPR_TUPLE_LITERAL:
        new_node->tuple_literal.elements =
            copy_ast_replacing(n->tuple_literal.elements, p, c, os, ns);
        new_node->tuple_literal.count = n->tuple_literal.count;
        break;
    case NODE_EXPR_SLICE:
        new_node->slice.array = copy_ast_replacing(n->slice.array, p, c, os, ns);
        new_node->slice.start = copy_ast_replacing(n->slice.start, p, c, os, ns);
        new_node->slice.end = copy_ast_replacing(n->slice.end, p, c, os, ns);
        break;
    case NODE_EXPECT:
    case NODE_ASSERT:
        new_node->assert_stmt.condition =
            copy_ast_replacing(n->assert_stmt.condition, p, c, os, ns);
        new_node->assert_stmt.message =
            n->assert_stmt.message ? xstrdup(n->assert_stmt.message) : NULL;
        break;
    case NODE_DEFER:
        new_node->defer_stmt.stmt = copy_ast_replacing(n->defer_stmt.stmt, p, c, os, ns);
        break;
    case NODE_TERNARY:
        new_node->ternary.cond = copy_ast_replacing(n->ternary.cond, p, c, os, ns);
        new_node->ternary.true_expr = copy_ast_replacing(n->ternary.true_expr, p, c, os, ns);
        new_node->ternary.false_expr = copy_ast_replacing(n->ternary.false_expr, p, c, os, ns);
        break;
    case NODE_ASM:
        new_node->asm_stmt.code = n->asm_stmt.code ? xstrdup(n->asm_stmt.code) : NULL;
        new_node->asm_stmt.is_volatile = n->asm_stmt.is_volatile;
        new_node->asm_stmt.num_outputs = n->asm_stmt.num_outputs;
        new_node->asm_stmt.num_inputs = n->asm_stmt.num_inputs;
        new_node->asm_stmt.num_clobbers = n->asm_stmt.num_clobbers;
        // ASM usually doesn't contain generic parameters in constraints, but we could harden here
        // if needed
        break;
    case NODE_GOTO:
        new_node->goto_stmt.label_name =
            n->goto_stmt.label_name ? xstrdup(n->goto_stmt.label_name) : NULL;
        break;
    case NODE_LABEL:
        new_node->label_stmt.label_name =
            n->label_stmt.label_name ? xstrdup(n->label_stmt.label_name) : NULL;
        break;
    case NODE_DO_WHILE:
        new_node->while_stmt.condition = copy_ast_replacing(n->while_stmt.condition, p, c, os, ns);
        new_node->while_stmt.body = copy_ast_replacing(n->while_stmt.body, p, c, os, ns);
        break;
    case NODE_TRY:
        new_node->try_stmt.expr = copy_ast_replacing(n->try_stmt.expr, p, c, os, ns);
        break;
    case NODE_REFLECTION:
        new_node->reflection.kind = n->reflection.kind;
        new_node->reflection.target_type =
            replace_type_formal(n->reflection.target_type, p, c, os, ns);
        break;
    case NODE_REPL_PRINT:
        new_node->repl_print.expr = copy_ast_replacing(n->repl_print.expr, p, c, os, ns);
        break;
    case NODE_CUDA_LAUNCH:
        new_node->cuda_launch.call = copy_ast_replacing(n->cuda_launch.call, p, c, os, ns);
        new_node->cuda_launch.grid = copy_ast_replacing(n->cuda_launch.grid, p, c, os, ns);
        new_node->cuda_launch.block = copy_ast_replacing(n->cuda_launch.block, p, c, os, ns);
        new_node->cuda_launch.shared_mem =
            copy_ast_replacing(n->cuda_launch.shared_mem, p, c, os, ns);
        new_node->cuda_launch.stream = copy_ast_replacing(n->cuda_launch.stream, p, c, os, ns);
        break;
    case NODE_VA_START:
        new_node->va_start_args.ap = copy_ast_replacing(n->va_start_args.ap, p, c, os, ns);
        new_node->va_start_args.last_arg =
            copy_ast_replacing(n->va_start_args.last_arg, p, c, os, ns);
        break;
    case NODE_VA_END:
        new_node->va_end_args.ap = copy_ast_replacing(n->va_end_args.ap, p, c, os, ns);
        break;
    case NODE_VA_COPY:
        new_node->va_copy_args.dest = copy_ast_replacing(n->va_copy_args.dest, p, c, os, ns);
        new_node->va_copy_args.src = copy_ast_replacing(n->va_copy_args.src, p, c, os, ns);
        break;
    case NODE_VA_ARG:
        new_node->va_arg_val.ap = copy_ast_replacing(n->va_arg_val.ap, p, c, os, ns);
        new_node->va_arg_val.type_info = replace_type_formal(n->va_arg_val.type_info, p, c, os, ns);
        break;
    default:
        break;
    }
    return new_node;
}

// Helper to sanitize type names for mangling (e.g. "int*" -> "intPtr")
char *sanitize_mangled_name(const char *s)
{
    char *buf = xmalloc(strlen(s) * 4 + 1);

    // Skip "struct " prefix if present to avoid "struct_" in mangled names
    if (strncmp(s, "struct ", 7) == 0)
    {
        s += 7;
    }

    char *p = buf;
    while (*s)
    {
        if (*s == '*')
        {
            strcpy(p, "Ptr");
            p += 3;
        }
        else if (*s == '<' || *s == ',' || *s == ' ')
        {
            *p++ = '_';
            *p++ = '_';
        }
        else if (*s == '>' || *s == '&')
        {
            // Skip > and & (often used in references) to keep names clean
        }
        else if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') ||
                 *s == '_')
        {
            *p++ = *s;
        }
        else
        {
            *p++ = '_';
        }
        s++;
    }
    *p = 0;
    return buf;
}

// Helper to unmangle Ptr suffix back to pointer type ("intPtr" -> "int*")
char *unmangle_ptr_suffix(const char *s)
{
    if (!s)
    {
        return NULL;
    }

    size_t len = strlen(s);
    if (len <= 3 || strcmp(s + len - 3, "Ptr") != 0)
    {
        return xstrdup(s); // No Ptr suffix, return as-is
    }

    // Extract base type (everything before "Ptr")
    char *base = xmalloc(len - 2);
    strncpy(base, s, len - 3);
    base[len - 3] = '\0';

    char *result = xmalloc(strlen(base) + 16);

    // Check if base is a primitive type
    if (is_primitive_type_name(base))
    {
        sprintf(result, "%s*", base);
    }
    else
    {
        // Don't unmangle non-primitives ending in Ptr (like Vec_intPtr)
        strcpy(result, s);
    }

    zfree(base);
    return result;
}

// Helper function to recursively scan AST for sizeof types AND generic calls to trigger
// instantiation
static void trigger_type_instantiation(ParserContext *ctx, Type *t)
{
    if (!t)
    {
        return;
    }

    // Handle slices
    if (t->kind == TYPE_ARRAY && t->array_size == 0 && t->inner)
    {
        char *inner_str = type_to_string(t->inner);
        register_slice(ctx, inner_str);
        zfree(inner_str);
    }

    // Handle mangled types (instantiations)
    if (t->name && strchr(t->name, '_'))
    {
        char *type_copy = xstrdup(t->name);
        char *underscore = strchr(type_copy, '_');
        if (underscore)
        {
            char *concrete_arg = underscore;
            while (*concrete_arg == '_')
            {
                concrete_arg++;
            }
            *underscore = '\0';
            char *template_name = type_copy;

            GenericTemplate *gt = ctx->templates;
            int found = 0;
            while (gt)
            {
                if (strcmp(gt->name, template_name) == 0)
                {
                    found = 1;
                    break;
                }
                gt = gt->next;
            }

            if (found)
            {
                char *unmangled = unmangle_ptr_suffix(concrete_arg);
                Token dummy_tok = {0};
                instantiate_generic(ctx, template_name, concrete_arg, unmangled, dummy_tok);
                zfree(unmangled);
            }
        }
        zfree(type_copy);
    }

    // Recursive scan
    trigger_type_instantiation(ctx, t->inner);
    if (t->args)
    {
        for (int i = 0; i < t->arg_count; i++)
        {
            trigger_type_instantiation(ctx, t->args[i]);
        }
    }
}

static void trigger_instantiations(ParserContext *ctx, ASTNode *node)
{
    if (!node)
    {
        return;
    }

    // Process type information
    if (node->type_info)
    {
        trigger_type_instantiation(ctx, node->type_info);
    }

    // Process current node
    if (node->type == NODE_EXPR_SIZEOF && node->size_of.target_type)
    {
        const char *type_str = node->size_of.target_type;
        if (strchr(type_str, '_'))
        {
            // Remove trailing '*' or 'Ptr' if present
            char *type_copy = xstrdup(type_str);
            char *star = strchr(type_copy, '*');
            if (star)
            {
                *star = '\0';
            }
            else
            {
                // Check for "Ptr" suffix and remove it
                size_t len = strlen(type_copy);
                if (len > 3 && strcmp(type_copy + len - 3, "Ptr") == 0)
                {
                    type_copy[len - 3] = '\0';
                }
            }

            char *underscore = strchr(type_copy, '_');
            if (underscore)
            {
                char *concrete_arg = underscore;
                while (*concrete_arg == '_')
                {
                    concrete_arg++;
                }
                *underscore = '\0';
                char *template_name = type_copy;

                // Check if this is a known generic template
                GenericTemplate *gt = ctx->templates;
                int found = 0;
                while (gt)
                {
                    if (strcmp(gt->name, template_name) == 0)
                    {
                        found = 1;
                        break;
                    }
                    gt = gt->next;
                }

                if (found)
                {
                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    Token dummy_tok = {0};
                    instantiate_generic(ctx, template_name, concrete_arg, unmangled, dummy_tok);
                    zfree(unmangled);
                }
            }
            zfree(type_copy);
        }
    }
    else if (node->type == NODE_EXPR_VAR)
    {
        const char *name = node->var_ref.name;
        if (strchr(name, '_'))
        {
            GenericFuncTemplate *t = ctx->func_templates;
            while (t)
            {
                size_t tlen = strlen(t->name);
                if (strncmp(name, t->name, tlen) == 0 && name[tlen] == '_' && name[tlen + 1] == '_')
                {
                    char *template_name = t->name;
                    char *concrete_arg = (char *)name + tlen + 2;

                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    instantiate_function_template(ctx, template_name, concrete_arg, unmangled);
                    zfree(unmangled);
                    break; // Found match, stop searching
                }
                t = t->next;
            }
        }
    }
    else if (node->type == NODE_EXPR_STRUCT_INIT && node->struct_init.struct_name)
    {
        const char *name = node->struct_init.struct_name;
        if (strchr(name, '_'))
        {
            char *type_copy = xstrdup(name);
            char *underscore = strchr(type_copy, '_');
            if (underscore)
            {
                char *concrete_arg = underscore;
                while (*concrete_arg == '_')
                {
                    concrete_arg++;
                }
                *underscore = '\0';
                char *template_name = type_copy;

                GenericTemplate *gt = ctx->templates;
                int found = 0;
                while (gt)
                {
                    if (strcmp(gt->name, template_name) == 0)
                    {
                        found = 1;
                        break;
                    }
                    gt = gt->next;
                }

                if (found)
                {
                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    Token dummy_tok = {0};
                    instantiate_generic(ctx, template_name, concrete_arg, unmangled, dummy_tok);
                    zfree(unmangled);
                }
            }
            zfree(type_copy);
        }
    }

    switch (node->type)
    {
    case NODE_FUNCTION:
        trigger_instantiations(ctx, node->func.body);
        break;
    case NODE_BLOCK:
        trigger_instantiations(ctx, node->block.statements);
        break;
    case NODE_VAR_DECL:
        trigger_instantiations(ctx, node->var_decl.init_expr);
        break;
    case NODE_RETURN:
        trigger_instantiations(ctx, node->ret.value);
        break;
    case NODE_EXPR_BINARY:
        trigger_instantiations(ctx, node->binary.left);
        trigger_instantiations(ctx, node->binary.right);
        break;
    case NODE_EXPR_UNARY:
        trigger_instantiations(ctx, node->unary.operand);
        break;
    case NODE_EXPR_CALL:
        trigger_instantiations(ctx, node->call.callee);
        trigger_instantiations(ctx, node->call.args);
        break;
    case NODE_EXPR_MEMBER:
        trigger_instantiations(ctx, node->member.target);
        break;
    case NODE_EXPR_INDEX:
        trigger_instantiations(ctx, node->index.array);
        trigger_instantiations(ctx, node->index.index);
        break;
    case NODE_EXPR_CAST:
        trigger_instantiations(ctx, node->cast.expr);
        break;
    case NODE_IF:
        trigger_instantiations(ctx, node->if_stmt.condition);
        trigger_instantiations(ctx, node->if_stmt.then_body);
        trigger_instantiations(ctx, node->if_stmt.else_body);
        break;
    case NODE_WHILE:
        trigger_instantiations(ctx, node->while_stmt.condition);
        trigger_instantiations(ctx, node->while_stmt.body);
        break;
    case NODE_FOR:
        trigger_instantiations(ctx, node->for_stmt.init);
        trigger_instantiations(ctx, node->for_stmt.condition);
        trigger_instantiations(ctx, node->for_stmt.step);
        trigger_instantiations(ctx, node->for_stmt.body);
        break;
    case NODE_FOR_RANGE:
        trigger_instantiations(ctx, node->for_range.start);
        trigger_instantiations(ctx, node->for_range.end);
        trigger_instantiations(ctx, node->for_range.body);
        break;
    case NODE_EXPR_STRUCT_INIT:
        trigger_instantiations(ctx, node->struct_init.fields);
        break;
    case NODE_MATCH:
        trigger_instantiations(ctx, node->match_stmt.expr);
        trigger_instantiations(ctx, node->match_stmt.cases);
        break;
    case NODE_MATCH_CASE:
        trigger_instantiations(ctx, node->match_case.guard);
        trigger_instantiations(ctx, node->match_case.body);
        break;
    case NODE_EXPECT:
    case NODE_ASSERT:
        trigger_instantiations(ctx, node->assert_stmt.condition);
        break;
    case NODE_DEFER:
        trigger_instantiations(ctx, node->defer_stmt.stmt);
        break;
    case NODE_UNLESS:
        trigger_instantiations(ctx, node->unless_stmt.condition);
        trigger_instantiations(ctx, node->unless_stmt.body);
        break;
    case NODE_GUARD:
        trigger_instantiations(ctx, node->guard_stmt.condition);
        trigger_instantiations(ctx, node->guard_stmt.body);
        break;
    case NODE_LOOP:
        trigger_instantiations(ctx, node->loop_stmt.body);
        break;
    case NODE_REPEAT:
        trigger_instantiations(ctx, node->repeat_stmt.body);
        break;
    case NODE_DO_WHILE:
        trigger_instantiations(ctx, node->while_stmt.condition);
        trigger_instantiations(ctx, node->while_stmt.body);
        break;
    case NODE_TERNARY:
        trigger_instantiations(ctx, node->ternary.cond);
        trigger_instantiations(ctx, node->ternary.true_expr);
        trigger_instantiations(ctx, node->ternary.false_expr);
        break;
    case NODE_EXPR_ARRAY_LITERAL:
        trigger_instantiations(ctx, node->array_literal.elements);
        break;
    case NODE_EXPR_TUPLE_LITERAL:
        trigger_instantiations(ctx, node->tuple_literal.elements);
        break;
    case NODE_EXPR_SLICE:
        trigger_instantiations(ctx, node->slice.array);
        trigger_instantiations(ctx, node->slice.start);
        trigger_instantiations(ctx, node->slice.end);
        break;
    case NODE_DESTRUCT_VAR:
        trigger_instantiations(ctx, node->destruct.init_expr);
        trigger_instantiations(ctx, node->destruct.else_block);
        break;
    case NODE_LAMBDA:
        trigger_instantiations(ctx, node->lambda.body);
        break;
    case NODE_TRY:
        trigger_instantiations(ctx, node->try_stmt.expr);
        break;
    case NODE_CUDA_LAUNCH:
        trigger_instantiations(ctx, node->cuda_launch.call);
        trigger_instantiations(ctx, node->cuda_launch.grid);
        trigger_instantiations(ctx, node->cuda_launch.block);
        trigger_instantiations(ctx, node->cuda_launch.shared_mem);
        trigger_instantiations(ctx, node->cuda_launch.stream);
        break;
    case NODE_VA_START:
        trigger_instantiations(ctx, node->va_start_args.ap);
        trigger_instantiations(ctx, node->va_start_args.last_arg);
        break;
    case NODE_VA_END:
        trigger_instantiations(ctx, node->va_end_args.ap);
        break;
    case NODE_VA_COPY:
        trigger_instantiations(ctx, node->va_copy_args.dest);
        trigger_instantiations(ctx, node->va_copy_args.src);
        break;
    case NODE_VA_ARG:
        trigger_instantiations(ctx, node->va_arg_val.ap);
        break;
    default:
        break;
    }

    // Visit next sibling
    trigger_instantiations(ctx, node->next);
}

char *instantiate_function_template(ParserContext *ctx, const char *name, const char *concrete_type,
                                    const char *unmangled_type)
{
    GenericFuncTemplate *tpl = find_func_template(ctx, name);
    if (!tpl)
    {
        return NULL;
    }

    char *clean_type = sanitize_mangled_name(concrete_type);

    int is_still_generic = 0;
    if (strlen(clean_type) == 1 && isupper(clean_type[0]))
    {
        is_still_generic = 1;
    }

    if (is_known_generic(ctx, clean_type))
    {
        is_still_generic = 1;
    }

    char buf[MAX_ERROR_MSG_LEN];
    snprintf(buf, sizeof(buf), "%s__%s", name, clean_type);
    char *mangled = merge_underscores(buf);
    zfree(clean_type);

    if (is_still_generic)
    {
        return mangled;
    }

    if (find_func(ctx, mangled))
    {
        return mangled;
    }

    const char *subst_arg = unmangled_type ? unmangled_type : concrete_type;

    // Scan the original return type for generic struct patterns like "Triple_X_Y_Z"
    // and instantiate them with the concrete types
    if (tpl->func_node && tpl->func_node->func.ret_type)
    {
        const char *ret = tpl->func_node->func.ret_type;

        // Build the param suffix (e.g., for "X,Y,Z" -> "_X_Y_Z")
        size_t suffix_cap = strlen(tpl->generic_param) * 2 + 64;
        char *param_suffix = xmalloc(suffix_cap);
        param_suffix[0] = 0;
        const char *p_ptr = tpl->generic_param;
        while (p_ptr && *p_ptr)
        {
            strcat(param_suffix, "__");
            const char *p_next = strchr(p_ptr, ',');
            int sub_len = p_next ? (int)(p_next - p_ptr) : (int)strlen(p_ptr);
            strncat(param_suffix, p_ptr, sub_len);
            if (p_next)
            {
                p_ptr = p_next + 1;
            }
            else
            {
                break;
            }
        }

        // Check if ret_type ends with param_suffix (e.g., "Triple_X_Y_Z" ends with "_X_Y_Z")
        size_t ret_len = strlen(ret);
        size_t suffix_len = strlen(param_suffix);
        if (ret_len > suffix_len && strcmp(ret + ret_len - suffix_len, param_suffix) == 0)
        {
            // Extract base struct name (e.g., "Triple" from "Triple_X_Y_Z")
            size_t base_len = ret_len - suffix_len;
            char *struct_base = xmalloc(base_len + 1);
            strncpy(struct_base, ret, base_len);
            struct_base[base_len] = 0;

            // Check if it's a known generic template
            GenericTemplate *gt = ctx->templates;
            while (gt && strcmp(gt->name, struct_base) != 0)
            {
                gt = gt->next;
            }
            if (gt)
            {
                // Parse the concrete types from unmangled_type or concrete_type
                const char *types_src = unmangled_type ? unmangled_type : concrete_type;

                // Count params in template
                int template_param_count = 1;
                for (const char *p = tpl->generic_param; *p; p++)
                {
                    if (*p == ',')
                    {
                        template_param_count++;
                    }
                }

                // Split concrete types
                char **args = xmalloc(sizeof(char *) * template_param_count);
                int arg_count = 0;
                const char *types_ptr = types_src;
                while (types_ptr && *types_ptr && arg_count < template_param_count)
                {
                    const char *types_next = strchr(types_ptr, ',');
                    int types_len =
                        types_next ? (int)(types_next - types_ptr) : (int)strlen(types_ptr);

                    args[arg_count] = xmalloc(types_len + 1);
                    strncpy(args[arg_count], types_ptr, types_len);
                    args[arg_count][types_len] = 0;
                    arg_count++;

                    if (types_next)
                    {
                        types_ptr = types_next + 1;
                    }
                    else
                    {
                        break;
                    }
                }

                // Now instantiate the struct with these args
                Token dummy_tok = {0};
                if (arg_count == 1)
                {
                    // Unmangle Ptr suffix if needed (e.g., intPtr -> int*)
                    char *unmangled = xstrdup(args[0]);
                    size_t alen = strlen(args[0]);
                    if (alen > 3 && strcmp(args[0] + alen - 3, "Ptr") == 0)
                    {
                        char *base = xstrdup(args[0]);
                        base[alen - 3] = '\0';
                        zfree(unmangled);
                        unmangled = xmalloc(strlen(base) + 16);
                        if (is_unmangle_primitive(base))
                        {
                            sprintf(unmangled, "%s*", base);
                        }
                        else
                        {
                            sprintf(unmangled, "struct %s*", base);
                        }
                        zfree(base);
                    }
                    instantiate_generic(ctx, struct_base, args[0], unmangled, dummy_tok);
                    zfree(unmangled);
                }
                else if (arg_count > 1)
                {
                    instantiate_generic_multi(ctx, struct_base, args, arg_count, dummy_tok);
                }

                // Cleanup
                for (int i = 0; i < arg_count; i++)
                {
                    zfree(args[i]);
                }
                zfree(args);
            }
            zfree(struct_base);
        }
        zfree(param_suffix);
    }

    ASTNode *new_fn = copy_ast_replacing(tpl->func_node, tpl->generic_param, subst_arg, NULL, NULL);
    if (!new_fn || new_fn->type != NODE_FUNCTION)
    {
        return NULL;
    }

    zfree(new_fn->func.name);
    new_fn->func.name = xstrdup(mangled);
    new_fn->func.generic_params = NULL;

    add_instantiated_func(ctx, new_fn);

    register_func(ctx, ctx->global_scope, mangled, new_fn->func.arg_count, new_fn->func.defaults,
                  new_fn->func.arg_types, new_fn->func.ret_type_info, new_fn->func.is_varargs, 0,
                  new_fn->func.pure, new_fn->link_name, new_fn->token, new_fn->func.is_export);

    trigger_instantiations(ctx, new_fn->func.body);

    if (new_fn->func.arg_types)
    {
        for (int i = 0; i < new_fn->func.arg_count; i++)
        {
            Type *at = new_fn->func.arg_types[i];
            if (at && at->kind == TYPE_ARRAY && at->array_size == 0 && at->inner)
            {
                char *inner_str = type_to_string(at->inner);
                register_slice(ctx, inner_str);
                zfree(inner_str);
            }
        }
    }

    return mangled;
}

char *process_fstring(ParserContext *ctx, const char *content, char ***used_syms, int *count)
{
    (void)used_syms;
    (void)count;
    char *gen = xmalloc(8192); // Increased buffer size

    strcpy(gen, "({ static char _b[4096]; _b[0]=0; char _t[1024]; ");

    char *s = xstrdup(content);
    char *cur = s;

    while (*cur)
    {
        char *brace = cur;
        while (*brace && *brace != '{')
        {
            brace++;
        }

        if (brace > cur)
        {
            char tmp = *brace;
            *brace = 0;
            strcat(gen, "strcat(_b, \"");
            strcat(gen, cur);
            strcat(gen, "\"); ");
            *brace = tmp;
        }

        if (*brace == 0)
        {
            break;
        }

        char *p = brace + 1;
        char *colon = NULL;
        int depth = 1;

        while (*p && depth > 0)
        {
            if (*p == '{')
            {
                depth++;
            }
            if (*p == '}')
            {
                depth--;
            }
            if (depth == 1 && *p == ':' && !colon)
            {
                colon = p;
            }
            if (depth == 0)
            {
                break;
            }
            p++;
        }

        *p = 0;
        char *expr_str = brace + 1;
        char *fmt = NULL;
        if (colon)
        {
            *colon = 0;
            fmt = colon + 1;
        }

        // Parse expression fully to handle default arguments etc.
        Lexer expr_lex;
        lexer_init(&expr_lex, expr_str, ctx->config, ctx->current_filename);
        ASTNode *expr_node = parse_expression(ctx, &expr_lex);

        // Codegen expression to temporary buffer
        char *code_buffer = format_expression_as_c(ctx, expr_node);

        if (fmt)
        {
            strcat(gen, "sprintf(_t, \"%");
            strcat(gen, fmt);
            strcat(gen, "\", ");
            if (code_buffer)
            {
                strcat(gen, code_buffer);
            }
            else
            {
                strcat(gen, expr_str); // Fallback
            }
            strcat(gen, "); strcat(_b, _t); ");
        }
        else
        {
            strcat(gen, "sprintf(_t, _z_str(");
            if (code_buffer)
            {
                strcat(gen, code_buffer);
            }
            else
            {
                strcat(gen, expr_str);
            }
            strcat(gen, "), ");
            if (code_buffer)
            {
                strcat(gen, code_buffer);
            }
            else
            {
                strcat(gen, expr_str);
            }
            strcat(gen, "); strcat(_b, _t); ");
        }

        if (code_buffer)
        {
            zfree(code_buffer);
        }

        cur = p + 1;
    }

    strcat(gen, "_b; })");
    zfree(s);
    return gen;
}

static int is_unmangle_primitive(const char *base)
{
    return (strcmp(base, "int") == 0 || strcmp(base, "uint") == 0 || strcmp(base, "char") == 0 ||
            strcmp(base, "bool") == 0 || strcmp(base, "void") == 0 || strcmp(base, "byte") == 0 ||
            strcmp(base, "rune") == 0 || strcmp(base, "float") == 0 ||
            strcmp(base, "double") == 0 || strcmp(base, "f32") == 0 || strcmp(base, "f64") == 0 ||
            strcmp(base, "size_t") == 0 || strcmp(base, "usize") == 0 ||
            strcmp(base, "isize") == 0 || strcmp(base, "ptrdiff_t") == 0 ||
            strncmp(base, "i8", 2) == 0 || strncmp(base, "u8", 2) == 0 ||
            strncmp(base, "int8", 4) == 0 || strncmp(base, "int16", 5) == 0 ||
            strncmp(base, "int32", 5) == 0 || strncmp(base, "int64", 5) == 0 ||
            strncmp(base, "uint8", 5) == 0 || strncmp(base, "uint16", 6) == 0 ||
            strncmp(base, "uint32", 6) == 0 || strncmp(base, "uint64", 6) == 0);
}

void register_template(ParserContext *ctx, const char *name, ASTNode *node)
{
    GenericTemplate *t = xcalloc(1, sizeof(GenericTemplate));
    t->name = xstrdup(name);
    t->struct_node = node;
    t->next = ctx->templates;
    ctx->templates = t;
}

ASTNode *copy_fields_replacing(ParserContext *ctx, ASTNode *fields, const char *param,
                               const char *concrete)
{
    if (!fields)
    {
        return NULL;
    }
    ASTNode *n = ast_create(NODE_FIELD);
    n->field.name = xstrdup(fields->field.name);

    // Replace strings
    n->field.type = replace_type_str(fields->field.type, param, concrete, NULL, NULL);

    // Replace formal types (Deep Copy)
    n->type_info = replace_type_formal(fields->type_info, param, concrete, NULL, NULL);

    if (n->field.type && strchr(n->field.type, '_'))
    {
        // Parse potential generic: e.g. "MapEntry_int" -> instantiate("MapEntry",
        // "int")
        char *underscore = strrchr(n->field.type, '_');
        if (underscore && underscore > n->field.type)
        {
            // Remove trailing '*' if present
            char *type_copy = xstrdup(n->field.type);
            char *star = strchr(type_copy, '*');
            if (star)
            {
                *star = '\0';
            }

            underscore = strrchr(type_copy, '_');
            if (underscore)
            {
                *underscore = '\0';
                char *template_name = type_copy;
                char *concrete_arg = underscore + 1;

                // Check if this is actually a known generic template
                GenericTemplate *gt = ctx->templates;
                int found = 0;
                while (gt)
                {
                    if (strcmp(gt->name, template_name) == 0)
                    {
                        found = 1;
                        break;
                    }
                    gt = gt->next;
                }

                if (found)
                {
                    char *unmangled = unmangle_ptr_suffix(concrete_arg);
                    if (concrete)
                    {
                        char *clean_concrete = sanitize_mangled_name(concrete);
                        if (strcmp(concrete_arg, clean_concrete) == 0)
                        {
                            zfree(unmangled);
                            unmangled = xstrdup(concrete);
                        }
                        zfree(clean_concrete);
                    }

                    instantiate_generic(ctx, template_name, concrete_arg, unmangled, fields->token);
                    zfree(unmangled);
                }
            }
            zfree(type_copy);
        }
    }

    // Additional check: if type_info is a pointer to a struct with a mangled name,
    // instantiate that struct as well (fixes cases like RcInner<T>* where the
    // string check above might not catch it)
    if (n->type_info && n->type_info->kind == TYPE_POINTER && n->type_info->inner)
    {
        Type *inner = n->type_info->inner;
        if (inner->kind == TYPE_STRUCT && inner->name && strchr(inner->name, '_'))
        {
            // Extract template name by checking against known templates
            // We can't use strrchr because types like "Inner_int32_t" have multiple underscores
            char *template_name = NULL;
            char *concrete_arg = NULL;

            // Try each known template to see if the type name starts with it
            GenericTemplate *gt = ctx->templates;
            while (gt)
            {
                size_t tlen = strlen(gt->name);
                // Check if name starts with template name followed by double underscore
                if (strncmp(inner->name, gt->name, tlen) == 0 && inner->name[tlen] == '_' &&
                    inner->name[tlen + 1] == '_')
                {
                    template_name = gt->name;
                    concrete_arg =
                        inner->name + tlen + 2; // Skip template name and double underscore
                    break;
                }
                gt = gt->next;
            }

            if (template_name && concrete_arg)
            {
                char *unmangled = unmangle_ptr_suffix(concrete_arg);
                if (concrete)
                {
                    char *clean_concrete = sanitize_mangled_name(concrete);
                    if (strcmp(concrete_arg, clean_concrete) == 0)
                    {
                        zfree(unmangled);
                        unmangled = xstrdup(concrete);
                    }
                    zfree(clean_concrete);
                }
                instantiate_generic(ctx, template_name, concrete_arg, unmangled, fields->token);
                zfree(unmangled);
            }
        }
    }

    n->next = copy_fields_replacing(ctx, fields->next, param, concrete);
    return n;
}

void instantiate_methods(ParserContext *ctx, GenericImplTemplate *it,
                         const char *mangled_struct_name, const char *arg,
                         const char *unmangled_arg)
{
    if (check_impl(ctx, "Methods", mangled_struct_name))
    {
        return; // Simple dedupe check
    }

    ASTNode *backup_next = it->impl_node->next;
    it->impl_node->next = NULL; // Break link to isolate node

    // Use unmangled_arg if provided, otherwise arg
    char *raw = (char *)(unmangled_arg ? unmangled_arg : arg);
    char *subst_arg = unmangle_ptr_suffix(raw);

    ASTNode *new_impl = copy_ast_replacing(it->impl_node, it->generic_param, subst_arg,
                                           it->struct_name, mangled_struct_name);

    // Also replace mangled template name (both List__G and List_G)
    if (strchr(it->struct_name, '<'))
    {
        char *sanitized = sanitize_mangled_name(it->struct_name);
        if (strcmp(sanitized, it->struct_name) != 0)
        {
            ASTNode *tmp = copy_ast_replacing(new_impl, NULL, NULL, sanitized, mangled_struct_name);
            new_impl = tmp;
        }

        char *old_sanitized = xstrdup(sanitized);
        char *double_underscore = strstr(old_sanitized, "__");
        if (double_underscore)
        {
            memmove(double_underscore, double_underscore + 1, strlen(double_underscore + 1) + 1);
        }

        if (strcmp(old_sanitized, it->struct_name) != 0 && strcmp(old_sanitized, sanitized) != 0)
        {
            ASTNode *tmp =
                copy_ast_replacing(new_impl, NULL, NULL, old_sanitized, mangled_struct_name);
            new_impl = tmp;
        }

        zfree(old_sanitized);
        zfree(sanitized);
    }
    zfree(subst_arg);
    it->impl_node->next = backup_next; // Restore

    ASTNode *meth = NULL;

    if (new_impl->type == NODE_IMPL)
    {
        new_impl->impl.struct_name = xstrdup(mangled_struct_name);
        meth = new_impl->impl.methods;
    }
    else if (new_impl->type == NODE_IMPL_TRAIT)
    {
        new_impl->impl_trait.target_type = xstrdup(mangled_struct_name);
        meth = new_impl->impl_trait.methods;
    }

    while (meth)
    {
        // Standardize: ensure __ between type and method
        // If it's already correctly mangled (e.g. Vec__int32_t__with_capacity), skip
        size_t mlen = strlen(mangled_struct_name);
        int correctly_mangled = (strncmp(meth->func.name, mangled_struct_name, mlen) == 0 &&
                                 meth->func.name[mlen] == '_' && meth->func.name[mlen + 1] == '_');

        if (!correctly_mangled)
        {
            // Find the method part in the original name (e.g. "with_capacity" in
            // "Vec_with_capacity")
            char *original_method = meth->func.name;
            if (strncmp(original_method, it->struct_name, strlen(it->struct_name)) == 0)
            {
                original_method += strlen(it->struct_name);
            }
            while (*original_method == '_')
            {
                original_method++;
            }

            char *temp = xmalloc(strlen(mangled_struct_name) + strlen(original_method) + 3);
            sprintf(temp, "%s__%s", mangled_struct_name, original_method);
            char *new_name = merge_underscores(temp);
            zfree(temp);
            zfree(meth->func.name);
            meth->func.name = new_name;
        }

        register_func(ctx, ctx->global_scope, meth->func.name, meth->func.arg_count,
                      meth->func.defaults, meth->func.arg_types, meth->func.ret_type_info,
                      meth->func.is_varargs, (meth->type == NODE_FUNCTION && meth->func.is_async),
                      meth->func.pure, meth->link_name, meth->token, meth->func.is_export);

        // Handle generic return types in methods (e.g., Option<T> -> Option_int)
        if (meth->func.ret_type &&
            (strchr(meth->func.ret_type, '_') || strchr(meth->func.ret_type, '<')))
        {
            GenericTemplate *gt = ctx->templates;

            while (gt)
            {
                size_t tlen = strlen(gt->name);
                char delim = meth->func.ret_type[tlen];
                if (strncmp(meth->func.ret_type, gt->name, tlen) == 0 &&
                    (delim == '_' || delim == '<'))
                {
                    // Found matching template prefix
                    const char *type_arg = meth->func.ret_type + tlen;
                    while (*type_arg == '_' || *type_arg == '<')
                    {
                        type_arg++;
                    }

                    // Simple approach: instantiate 'Template' with 'Arg'.
                    char *clean_arg = xstrdup(type_arg);
                    if (delim == '<')
                    {
                        char *closer = strrchr(clean_arg, '>');
                        if (closer)
                        {
                            *closer = 0;
                        }
                    }

                    // Unmangle Ptr suffix if present (e.g., intPtr -> int*)
                    char *inner_unmangled_arg = xstrdup(clean_arg);
                    size_t alen = strlen(clean_arg);
                    if (alen > 3 && strcmp(clean_arg + alen - 3, "Ptr") == 0)
                    {
                        char *base = xstrdup(clean_arg);
                        base[alen - 3] = '\0';
                        zfree(inner_unmangled_arg);
                        inner_unmangled_arg = xmalloc(strlen(base) + 16);
                        // Check if base is a primitive type
                        if (is_unmangle_primitive(base))
                        {
                            sprintf(inner_unmangled_arg, "%s*", base);
                        }
                        else
                        {
                            sprintf(inner_unmangled_arg, "struct %s*", base);
                        }
                        zfree(base);
                    }

                    instantiate_generic(ctx, gt->name, clean_arg, inner_unmangled_arg, meth->token);
                    zfree(clean_arg);
                }
                gt = gt->next;
            }
        }

        trigger_instantiations(ctx, meth->func.body);

        meth = meth->next;
    }
    add_instantiated_func(ctx, new_impl);
}

static void register_enum_constructor(ParserContext *ctx, const char *m, const char *var_name,
                                      int tag_id, Type *payload, Token token, int is_export)
{
    size_t mangled_var_sz = strlen(m) + strlen(var_name) + 3;
    char *mangled_var = xmalloc(mangled_var_sz);
    snprintf(mangled_var, mangled_var_sz, "%s__%s", m, var_name);
    register_enum_variant(ctx, m, mangled_var, tag_id);

    Type *ret_t = type_new(TYPE_ENUM);
    ret_t->name = xstrdup(m);

    if (payload)
    {
        Type **at = xmalloc(sizeof(Type *));
        at[0] = payload;
        register_func(ctx, ctx->global_scope, mangled_var, 1, NULL, at, ret_t, 0, 0, 0, NULL, token,
                      is_export);
    }
    else
    {
        register_func(ctx, ctx->global_scope, mangled_var, 0, NULL, NULL, ret_t, 0, 0, 0, NULL,
                      token, is_export);
    }
    zfree(mangled_var);
}

void instantiate_generic(ParserContext *ctx, const char *tpl, const char *arg,
                         const char *unmangled_arg, Token token)
{
    // Ignore generic placeholders
    if (strlen(arg) == 1 && isupper(arg[0]))
    {
        return;
    }
    if (strcmp(arg, "T") == 0)
    {
        return;
    }

    char *clean_arg = sanitize_mangled_name(arg);
    char *m = xmalloc(strlen(tpl) + strlen(clean_arg) + 4);
    strcpy(m, tpl);
    char *m_end = m + strlen(m);
    while (m_end > m && *(m_end - 1) == '_')
    {
        *(--m_end) = '\0';
    }
    strcat(m, "__");
    strcat(m, clean_arg);
    zfree(clean_arg);

    Instantiation *c = ctx->instantiations;
    while (c)
    {
        if (strcmp(c->name, m) == 0)
        {
            zfree(m);
            return; // Already instantiated, DO NOTHING.
        }
        c = c->next;
    }

    GenericTemplate *t = ctx->templates;
    while (t)
    {
        if (strcmp(t->name, tpl) == 0)
        {
            break;
        }
        t = t->next;
    }
    if (!t)
    {
        zpanic_at(token, "Unknown generic: %s", tpl);
        return; // fault tolerance: zpanic_at returned, bail out
    }

    Instantiation *ni = xcalloc(1, sizeof(Instantiation));
    ni->name = xstrdup(m);
    ni->template_name = xstrdup(tpl);
    ni->concrete_arg = xstrdup(arg);
    ni->unmangled_arg = unmangled_arg ? xstrdup(unmangled_arg)
                                      : xstrdup(arg); // Fallback to arg if unmangled is generic
    ni->struct_node = NULL;                           // Placeholder to break cycles
    ni->next = ctx->instantiations;
    ctx->instantiations = ni;

    ASTNode *struct_node_copy = NULL;

    if (t->struct_node->type == NODE_STRUCT)
    {
        ASTNode *i = ast_create(NODE_STRUCT);
        i->strct.name = xstrdup(m);
        i->strct.is_template = 0;
        i->strct.is_export = t->struct_node->strct.is_export;

        // Copy type attributes (e.g. has_drop)
        i->type_info = type_new(TYPE_STRUCT);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
            i->type_info->is_restrict = t->struct_node->type_info->is_restrict;
        }
        i->strct.is_packed = t->struct_node->strct.is_packed;
        i->strct.is_union = t->struct_node->strct.is_union;
        i->strct.align = t->struct_node->strct.align;
        if (t->struct_node->strct.parent)
        {
            i->strct.parent = xstrdup(t->struct_node->strct.parent);
        }
        const char *gp = (t->struct_node->strct.generic_param_count > 0)
                             ? t->struct_node->strct.generic_params[0]
                             : "T";
        const char *subst_arg = unmangled_arg ? unmangled_arg : arg;
        i->strct.fields = copy_fields_replacing(ctx, t->struct_node->strct.fields, gp, subst_arg);
        struct_node_copy = i;
        register_struct_def(ctx, m, i);

        // Register slice types used in the instantiated struct's fields
        ASTNode *fld = i->strct.fields;
        while (fld)
        {
            if (fld->field.type && strncmp(fld->field.type, "Slice__", 7) == 0)
            {
                register_slice(ctx, fld->field.type + 7);
            }
            fld = fld->next;
        }
    }
    else if (t->struct_node->type == NODE_ENUM)
    {
        ASTNode *i = ast_create(NODE_ENUM);
        i->enm.name = xstrdup(m);
        i->enm.is_template = 0;
        i->enm.is_export = t->struct_node->enm.is_export;

        // Copy type attributes (e.g. has_drop)
        i->type_info = type_new(TYPE_ENUM);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
        }

        ASTNode *h = 0, *tl = 0;
        ASTNode *v = t->struct_node->enm.variants;
        while (v)
        {
            ASTNode *nv = ast_create(NODE_ENUM_VARIANT);
            nv->variant.name = xstrdup(v->variant.name);
            nv->variant.tag_id = v->variant.tag_id;
            const char *subst_arg = unmangled_arg ? unmangled_arg : arg;
            nv->variant.payload = replace_type_formal(
                v->variant.payload, t->struct_node->enm.generic_param, subst_arg, NULL, NULL);

            register_enum_constructor(ctx, m, nv->variant.name, nv->variant.tag_id,
                                      nv->variant.payload, token, i->enm.is_export);

            if (!h)
            {
                h = nv;
            }
            else
            {
                tl->next = nv;
            }
            tl = nv;
            v = v->next;
        }
        i->enm.variants = h;
        struct_node_copy = i;
    }

    ni->struct_node = struct_node_copy;

    if (struct_node_copy)
    {
        // Cache in hash table for fast lookup.
        if (struct_node_copy->type == NODE_STRUCT && struct_node_copy->strct.name)
        {
            struct_hash_insert(ctx, struct_node_copy->strct.name, struct_node_copy);
        }
        else if (struct_node_copy->type == NODE_ENUM && struct_node_copy->enm.name)
        {
            struct_hash_insert(ctx, struct_node_copy->enm.name, struct_node_copy);
        }

        struct_node_copy->next = ctx->instantiated_structs;
        ctx->instantiated_structs = struct_node_copy;
    }

    GenericImplTemplate *it = ctx->impl_templates;
    while (it)
    {
        if (strcmp(it->struct_name, tpl) == 0)
        {
            instantiate_methods(ctx, it, m, arg, unmangled_arg);
        }
        it = it->next;
    }
    zfree(m);
}

static void free_field_list(ASTNode *fields)
{
    while (fields)
    {
        ASTNode *next = fields->next;
        if (fields->field.name)
        {
            zfree(fields->field.name);
        }
        if (fields->field.type)
        {
            zfree(fields->field.type);
        }
        zfree(fields);
        fields = next;
    }
}

void instantiate_generic_multi(ParserContext *ctx, const char *tpl, char **args, int arg_count,
                               Token token)
{
    // Build mangled name from all args
    size_t m_len = strlen(tpl) + 1;
    for (int i = 0; i < arg_count; i++)
    {
        char *clean = sanitize_mangled_name(args[i]);
        m_len += 2 + strlen(clean);
        zfree(clean);
    }
    char *m = xmalloc(m_len + 1);
    strcpy(m, tpl);
    char *m_end = m + strlen(m);
    while (m_end > m && *(m_end - 1) == '_')
    {
        *(--m_end) = '\0';
    }
    for (int i = 0; i < arg_count; i++)
    {
        char *clean = sanitize_mangled_name(args[i]);
        strcat(m, "__");
        strcat(m, clean);
        zfree(clean);
    }

    // Check if already instantiated
    Instantiation *c = ctx->instantiations;
    while (c)
    {
        if (strcmp(c->name, m) == 0)
        {
            zfree(m);
            return; // Already done
        }
        c = c->next;
    }

    // Find the template
    GenericTemplate *t = ctx->templates;
    while (t)
    {
        if (strcmp(t->name, tpl) == 0)
        {
            break;
        }
        t = t->next;
    }
    if (!t)
    {
        zpanic_at(token, "Unknown generic: %s", tpl);
        return;
    }

    // Register instantiation first (to break cycles)
    Instantiation *ni = xcalloc(1, sizeof(Instantiation));
    ni->name = xstrdup(m);
    ni->template_name = xstrdup(tpl);
    ni->concrete_arg = (arg_count > 0) ? xstrdup(args[0]) : xstrdup("T");

    // For multi-param, build a comma-separated string for unmangled_arg
    size_t u_len = 0;
    for (int i = 0; i < arg_count; i++)
    {
        u_len += strlen(args[i]) + 1;
    }
    char *u_buf = xmalloc(u_len + 1);
    u_buf[0] = 0;
    for (int i = 0; i < arg_count; i++)
    {
        if (i > 0)
        {
            strcat(u_buf, ",");
        }
        strcat(u_buf, args[i]);
    }
    ni->unmangled_arg = u_buf;

    ni->struct_node = NULL;
    ni->next = ctx->instantiations;
    ctx->instantiations = ni;

    if (t->struct_node->type == NODE_STRUCT)
    {
        ASTNode *i = ast_create(NODE_STRUCT);
        i->strct.name = xstrdup(m);
        i->strct.is_template = 0;
        i->strct.is_export = t->struct_node->strct.is_export;

        // Copy struct attributes
        i->strct.is_packed = t->struct_node->strct.is_packed;
        i->strct.is_union = t->struct_node->strct.is_union;
        i->strct.align = t->struct_node->strct.align;
        if (t->struct_node->strct.parent)
        {
            i->strct.parent = xstrdup(t->struct_node->strct.parent);
        }

        // Copy fields with sequential substitutions for each param
        ASTNode *fields = t->struct_node->strct.fields;
        int param_count = t->struct_node->strct.generic_param_count;

        if (param_count > 0 && arg_count > 0)
        {
            // First substitution
            i->strct.fields = copy_fields_replacing(
                ctx, fields, t->struct_node->strct.generic_params[0], args[0]);

            // Subsequent substitutions (for params B, C, etc.)
            for (int j = 1; j < param_count && j < arg_count; j++)
            {
                ASTNode *prev_fields = i->strct.fields;
                ASTNode *tmp = copy_fields_replacing(
                    ctx, prev_fields, t->struct_node->strct.generic_params[j], args[j]);
                free_field_list(prev_fields);
                i->strct.fields = tmp;
            }
        }
        else
        {
            i->strct.fields = copy_fields_replacing(ctx, fields, "T", "int");
        }

        ni->struct_node = i;
        register_struct_def(ctx, m, i);

        i->next = ctx->instantiated_structs;
        ctx->instantiated_structs = i;
    }
    else if (t->struct_node->type == NODE_ENUM)
    {
        ASTNode *i = ast_create(NODE_ENUM);
        i->enm.name = xstrdup(m);
        i->enm.is_template = 0;
        i->enm.is_export = t->struct_node->enm.is_export;

        // Copy type attributes
        i->type_info = type_new(TYPE_ENUM);
        i->type_info->name = xstrdup(m);
        if (t->struct_node->type_info)
        {
            i->type_info->traits = t->struct_node->type_info->traits;
        }

        ASTNode *h = 0, *tl = 0;
        ASTNode *v = t->struct_node->enm.variants;

        // Construct comma-separated concrete args string
        size_t c_args_len = 1;
        for (int j = 0; j < arg_count; j++)
        {
            c_args_len += strlen(args[j]) + 1;
        }
        char *c_args = xmalloc(c_args_len);
        c_args[0] = 0;
        for (int j = 0; j < arg_count; j++)
        {
            if (j > 0)
            {
                strcat(c_args, ",");
            }
            strcat(c_args, args[j]);
        }

        while (v)
        {
            ASTNode *nv = ast_create(NODE_ENUM_VARIANT);
            nv->variant.name = xstrdup(v->variant.name);
            nv->variant.tag_id = v->variant.tag_id;

            // Use multi-parameter substitution for payload
            Type *payload = v->variant.payload;
            nv->variant.payload = NULL;
            if (payload)
            {
                nv->variant.payload = replace_type_formal(
                    payload, t->struct_node->enm.generic_param, c_args, NULL, NULL);
            }

            register_enum_constructor(ctx, m, nv->variant.name, nv->variant.tag_id,
                                      nv->variant.payload, token, i->enm.is_export);

            if (!h)
            {
                h = nv;
            }
            else
            {
                tl->next = nv;
            }
            tl = nv;
            v = v->next;
        }
        zfree(c_args);
        i->enm.variants = h;
        ni->struct_node = i;
        register_struct_def(ctx, m, i);

        i->next = ctx->instantiated_structs;
        ctx->instantiated_structs = i;
    }
    zfree(m);
}
