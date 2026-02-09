#include "diagnostics.h"
#include "parser.h"
#include "lsp/cJSON.h"
#include <stdio.h>

static void emit_json(const char *level, Token t, const char *msg, const char *suggestion)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "file", g_current_filename ? g_current_filename : "unknown");
    cJSON_AddNumberToObject(root, "line", t.line);
    cJSON_AddNumberToObject(root, "col", t.col);
    cJSON_AddStringToObject(root, "level", level);
    cJSON_AddStringToObject(root, "message", msg);
    if (suggestion)
    {
        cJSON_AddStringToObject(root, "suggestion", suggestion);
    }

    char *json = cJSON_PrintUnformatted(root);
    fprintf(stderr, "%s\n", json);
    free(json);
    cJSON_Delete(root);
}

void zpanic(const char *fmt, ...)
{
    if (g_config.json_output)
    {
        char msg[1024];
        va_list a;
        va_start(a, fmt);
        vsnprintf(msg, sizeof(msg), fmt, a);
        va_end(a);
        emit_json("error", (Token){0}, msg, NULL);
        exit(1);
    }
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD);
    vfprintf(stderr, fmt, a);
    fprintf(stderr, COLOR_RESET "\n");
    va_end(a);
    exit(1);
}

void zfatal(const char *fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, "Fatal: ");
    vfprintf(stderr, fmt, a);
    fprintf(stderr, "\n");
    va_end(a);
    exit(1);
}

// Warning system (non-fatal).
void zwarn(const char *fmt, ...)
{
    if (g_config.quiet)
    {
        return;
    }
    if (g_config.json_output)
    {
        char msg[1024];
        va_list a;
        va_start(a, fmt);
        vsnprintf(msg, sizeof(msg), fmt, a);
        va_end(a);
        emit_json("warning", (Token){0}, msg, NULL);
        return;
    }
    g_warning_count++;
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, COLOR_YELLOW "warning: " COLOR_RESET COLOR_BOLD);
    vfprintf(stderr, fmt, a);
    fprintf(stderr, COLOR_RESET "\n");
    va_end(a);
}

void zwarn_at(Token t, const char *fmt, ...)
{
    if (g_config.quiet)
    {
        return;
    }
    if (g_config.json_output)
    {
        char msg[1024];
        va_list a;
        va_start(a, fmt);
        vsnprintf(msg, sizeof(msg), fmt, a);
        va_end(a);
        emit_json("warning", t, msg, NULL);
        return;
    }
    // Header: 'warning: message'.
    g_warning_count++;
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, COLOR_YELLOW "warning: " COLOR_RESET COLOR_BOLD);
    vfprintf(stderr, fmt, a);
    fprintf(stderr, COLOR_RESET "\n");
    va_end(a);

    // Location.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context. Only if token has valid data.
    if (t.start)
    {
        const char *line_start = t.start - (t.col - 1);
        const char *line_end = t.start;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        int line_len = line_end - line_start;

        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
        fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);

        // Caret.
        for (int i = 0; i < t.col - 1; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, COLOR_YELLOW "^ here" COLOR_RESET "\n");
        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
    }
}

void zwarn_with_suggestion(Token t, const char *msg, const char *suggestion)
{
    if (g_config.quiet)
    {
        return;
    }
    if (g_config.json_output)
    {
        emit_json("warning", t, msg, suggestion);
        return;
    }

    // Header: 'warning: message'.
    g_warning_count++;
    fprintf(stderr, COLOR_YELLOW "warning: " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "\n", msg);

    // Location.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context.
    if (t.start)
    {
        const char *line_start = t.start - (t.col - 1);
        const char *line_end = t.start;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        int line_len = line_end - line_start;

        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
        fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);

        // Caret.
        for (int i = 0; i < t.col - 1; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, COLOR_YELLOW "^ here" COLOR_RESET "\n");
        // Suggestion.
        if (suggestion)
        {
            fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
            fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "%s\n", suggestion);
        }
    }
}

void zpanic_at(Token t, const char *fmt, ...)
{
    if (g_config.json_output)
    {
        char msg[1024];
        va_list a;
        va_start(a, fmt);
        vsnprintf(msg, sizeof(msg), fmt, a);
        va_end(a);
        emit_json("error", t, msg, NULL);
        if (g_parser_ctx && g_parser_ctx->is_fault_tolerant && g_parser_ctx->on_error)
        {
            g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, msg);
            return;
        }
        exit(1);
    }
    // Header: 'error: message'.
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD);
    vfprintf(stderr, fmt, a);
    fprintf(stderr, COLOR_RESET "\n");
    va_end(a);

    // Location: '--> file:line:col'.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context line.
    const char *line_start = t.start - (t.col - 1);
    const char *line_end = t.start;
    while (*line_end && *line_end != '\n')
    {
        line_end++;
    }
    int line_len = line_end - line_start;

    // Visual bar.
    fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
    fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
    fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);

    // caret
    for (int i = 0; i < t.col - 1; i++)
    {
        fprintf(stderr, " ");
    }
    fprintf(stderr, COLOR_RED "^ here" COLOR_RESET "\n");
    fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);

    if (g_parser_ctx && g_parser_ctx->is_fault_tolerant && g_parser_ctx->on_error)
    {
        // Construct error message buffer
        char msg[1024];
        va_list args2;
        va_start(args2, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args2);
        va_end(args2);

        g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, msg);
        return; // Recover!
    }

    exit(1);
}

// Enhanced error with suggestion.
void zpanic_with_suggestion(Token t, const char *msg, const char *suggestion)
{
    if (g_config.json_output)
    {
        emit_json("error", t, msg, suggestion);
        if (g_parser_ctx && g_parser_ctx->is_fault_tolerant && g_parser_ctx->on_error)
        {
            char full_msg[1024];
            snprintf(full_msg, sizeof(full_msg), "%s (Suggestion: %s)", msg,
                     suggestion ? suggestion : "");
            g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
            return;
        }
        exit(1);
    }
    // Header.
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "\n", msg);

    // Location.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context.
    const char *line_start = t.start - (t.col - 1);
    const char *line_end = t.start;
    while (*line_end && *line_end != '\n')
    {
        line_end++;
    }
    int line_len = line_end - line_start;

    fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
    fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
    fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);
    for (int i = 0; i < t.col - 1; i++)
    {
        fprintf(stderr, " ");
    }
    fprintf(stderr, COLOR_RED "^ here" COLOR_RESET "\n");

    // Suggestion.
    if (suggestion)
    {
        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_CYAN "   = help: " COLOR_RESET "%s\n", suggestion);
    }

    if (g_parser_ctx && g_parser_ctx->is_fault_tolerant && g_parser_ctx->on_error)
    {
        // Construct error message buffer
        char full_msg[1024];
        snprintf(full_msg, sizeof(full_msg), "%s (Suggestion: %s)", msg,
                 suggestion ? suggestion : "");
        g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
        return; // Recover!
    }

    exit(1);
}

void zpanic_with_hints(Token t, const char *msg, const char *const *hints)
{
    if (g_config.json_output)
    {
        char combined_hints[4096] = {0};
        if (hints)
        {
            const char *const *h = hints;
            while (*h)
            {
                if (combined_hints[0])
                {
                    strncat(combined_hints, "\n", sizeof(combined_hints) - 1);
                }
                strncat(combined_hints, *h, sizeof(combined_hints) - strlen(combined_hints) - 1);
                h++;
            }
        }
        emit_json("error", t, msg, combined_hints[0] ? combined_hints : NULL);

        if (g_parser_ctx && g_parser_ctx->is_fault_tolerant && g_parser_ctx->on_error)
        {
            char full_msg[4096];
            snprintf(full_msg, sizeof(full_msg), "%s\n%s", msg, combined_hints);
            g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
            return;
        }
        exit(1);
    }

    // Header.
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "\n", msg);

    // Location.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context.
    const char *line_start = t.start - (t.col - 1);
    const char *line_end = t.start;
    while (*line_end && *line_end != '\n')
    {
        line_end++;
    }
    int line_len = line_end - line_start;

    fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
    fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
    fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);
    for (int i = 0; i < t.col - 1; i++)
    {
        fprintf(stderr, " ");
    }
    fprintf(stderr, COLOR_RED "^ here" COLOR_RESET "\n");

    // Hints.
    if (hints)
    {
        const char *const *h = hints;
        while (*h)
        {
            fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
            fprintf(stderr, COLOR_CYAN "   = help: " COLOR_RESET "%s\n", *h);
            h++;
        }
    }

    if (g_parser_ctx && g_parser_ctx->is_fault_tolerant && g_parser_ctx->on_error)
    {
        // Construct error message buffer
        char full_msg[4096];
        char combined_hints[2048] = {0};
        if (hints)
        {
            const char *const *h = hints;
            while (*h)
            {
                strncat(combined_hints,
                        "\nHelp: ", sizeof(combined_hints) - strlen(combined_hints) - 1);
                strncat(combined_hints, *h, sizeof(combined_hints) - strlen(combined_hints) - 1);
                h++;
            }
        }
        // Construct error message buffer
        // char full_msg[4096]; // Removed redeclaration
        int header_len = snprintf(full_msg, sizeof(full_msg), "%s", msg);
        if (header_len < (int)sizeof(full_msg))
        {
            strncat(full_msg, combined_hints, sizeof(full_msg) - strlen(full_msg) - 1);
        }
        g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
        return; // Recover!
    }

    exit(1);
}

void zerror_at(Token t, const char *fmt, ...)
{
    if (g_config.json_output)
    {
        char msg[1024];
        va_list a;
        va_start(a, fmt);
        vsnprintf(msg, sizeof(msg), fmt, a);
        va_end(a);
        emit_json("error", t, msg, NULL);
        if (g_parser_ctx && g_parser_ctx->on_error)
        {
            g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, msg);
        }
        return;
    }
    // Header: 'error: message'.
    va_list a;
    va_start(a, fmt);
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD);
    vfprintf(stderr, fmt, a);
    fprintf(stderr, COLOR_RESET "\n");
    va_end(a);

    // Location: '--> file:line:col'.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context line.
    if (t.start)
    {
        const char *line_start = t.start - (t.col - 1);
        const char *line_end = t.start;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        int line_len = line_end - line_start;

        // Visual bar.
        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
        fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);

        // caret
        for (int i = 0; i < t.col - 1; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, COLOR_RED "^ here" COLOR_RESET "\n");
        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
    }

    if (g_parser_ctx && g_parser_ctx->on_error)
    {
        // Construct error message buffer
        char msg[1024];
        va_list args2;
        va_start(args2, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args2);
        va_end(args2);

        g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, msg);
    }
}

void zerror_with_suggestion(Token t, const char *msg, const char *suggestion)
{
    if (g_config.json_output)
    {
        emit_json("error", t, msg, suggestion);
        if (g_parser_ctx && g_parser_ctx->on_error)
        {
            char full_msg[1024];
            snprintf(full_msg, sizeof(full_msg), "%s (Suggestion: %s)", msg,
                     suggestion ? suggestion : "");
            g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
        }
        return;
    }
    // Header.
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "\n", msg);

    // Location.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context.
    if (t.start)
    {
        const char *line_start = t.start - (t.col - 1);
        const char *line_end = t.start;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        int line_len = line_end - line_start;

        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
        fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);
        for (int i = 0; i < t.col - 1; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, COLOR_RED "^ here" COLOR_RESET "\n");

        // Suggestion.
        if (suggestion)
        {
            fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
            fprintf(stderr, COLOR_CYAN "   = help: " COLOR_RESET "%s\n", suggestion);
        }
    }

    {
        // Construct error message buffer
        char full_msg[1024];
        snprintf(full_msg, sizeof(full_msg), "%s (Suggestion: %s)", msg,
                 suggestion ? suggestion : "");
        g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
    }
}

void zerror_with_hints(Token t, const char *msg, const char *const *hints)
{
    char combined_hints[4096] = {0};
    if (hints)
    {
        const char *const *h = hints;
        while (*h)
        {
            if (combined_hints[0])
            {
                strncat(combined_hints, "\n", sizeof(combined_hints) - 1);
            }
            strncat(combined_hints, *h, sizeof(combined_hints) - strlen(combined_hints) - 1);
            h++;
        }
    }

    if (g_config.json_output)
    {
        emit_json("error", t, msg, combined_hints[0] ? combined_hints : NULL);
        if (g_parser_ctx && g_parser_ctx->on_error)
        {
            char full_msg[4096];
            int header_len = snprintf(full_msg, sizeof(full_msg), "%s\n", msg);
            if (header_len < (int)sizeof(full_msg))
            {
                strncat(full_msg, combined_hints, sizeof(full_msg) - strlen(full_msg) - 1);
            }
            g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
        }
        return;
    }

    // Header.
    fprintf(stderr, COLOR_RED "error: " COLOR_RESET COLOR_BOLD "%s" COLOR_RESET "\n", msg);

    // Location.
    fprintf(stderr, COLOR_BLUE "  --> " COLOR_RESET "%s:%d:%d\n", g_current_filename, t.line,
            t.col);

    // Context.
    if (t.start)
    {
        const char *line_start = t.start - (t.col - 1);
        const char *line_end = t.start;
        while (*line_end && *line_end != '\n')
        {
            line_end++;
        }
        int line_len = line_end - line_start;

        fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
        fprintf(stderr, COLOR_BLUE "%-3d| " COLOR_RESET "%.*s\n", t.line, line_len, line_start);
        fprintf(stderr, COLOR_BLUE "   | " COLOR_RESET);
        for (int i = 0; i < t.col - 1; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, COLOR_RED "^ here" COLOR_RESET "\n");

        // Hints.
        if (hints)
        {
            const char *const *h = hints;
            while (*h)
            {
                fprintf(stderr, COLOR_BLUE "   |\n" COLOR_RESET);
                fprintf(stderr, COLOR_CYAN "   = help: " COLOR_RESET "%s\n", *h);
                h++;
            }
        }
    }

    if (g_parser_ctx && g_parser_ctx->on_error)
    {
        // Construct error message buffer
        char full_msg[4096];
        int header_len = snprintf(full_msg, sizeof(full_msg), "%s", msg);
        if (header_len < (int)sizeof(full_msg))
        {
            strncat(full_msg, "\n", sizeof(full_msg) - strlen(full_msg) - 1);
            strncat(full_msg, combined_hints, sizeof(full_msg) - strlen(full_msg) - 1);
        }
        g_parser_ctx->on_error(g_parser_ctx->error_callback_data, t, full_msg);
    }
}

// Specific error types with helpful messages.
void error_undefined_function(Token t, const char *func_name, const char *suggestion)
{
    char msg[256];
    sprintf(msg, "Undefined function '%s'", func_name);

    if (suggestion)
    {
        char help[512];
        sprintf(help, "Did you mean '%s'?", suggestion);
        zerror_with_suggestion(t, msg, help);
    }
    else
    {
        zerror_with_suggestion(t, msg, "Check if the function is defined or imported");
    }
}

void error_wrong_arg_count(Token t, const char *func_name, int expected, int got)
{
    char msg[256];
    sprintf(msg, "Wrong number of arguments to function '%s'", func_name);

    char help[256];
    sprintf(help, "Expected %d argument%s, but got %d", expected, expected == 1 ? "" : "s", got);

    zerror_with_suggestion(t, msg, help);
}

void error_undefined_field(Token t, const char *struct_name, const char *field_name,
                           const char *suggestion)
{
    char msg[256];
    sprintf(msg, "Struct '%s' has no field '%s'", struct_name, field_name);

    if (suggestion)
    {
        char help[256];
        sprintf(help, "Did you mean '%s'?", suggestion);
        zerror_with_suggestion(t, msg, help);
    }
    else
    {
        zerror_with_suggestion(t, msg, "Check the struct definition");
    }
}

void error_type_expected(Token t, const char *expected, const char *got)
{
    char msg[256];
    sprintf(msg, "Type mismatch");

    char help[512];
    sprintf(help, "Expected type '%s', but found '%s'", expected, got);

    zerror_with_suggestion(t, msg, help);
}

void error_cannot_index(Token t, const char *type_name)
{
    char msg[256];
    sprintf(msg, "Cannot index into type '%s'", type_name);

    zerror_with_suggestion(t, msg, "Only arrays and slices can be indexed");
}

void warn_unused_variable(Token t, const char *var_name)
{
    char msg[256];
    sprintf(msg, "Unused variable '%s'", var_name);
    zwarn_with_suggestion(t, msg, "Consider removing it or prefixing with '_'");
}

void warn_shadowing(Token t, const char *var_name)
{
    char msg[256];
    sprintf(msg, "Variable '%s' shadows a previous declaration", var_name);
    zwarn_with_suggestion(t, msg, "This can lead to confusion");
}

void warn_unreachable_code(Token t)
{
    zwarn_with_suggestion(t, "Unreachable code detected", "This code will never execute");
}

void warn_implicit_conversion(Token t, const char *from_type, const char *to_type)
{
    char msg[256];
    sprintf(msg, "Implicit conversion from '%s' to '%s'", from_type, to_type);
    zwarn_with_suggestion(t, msg, "Consider using an explicit cast");
}

void warn_missing_return(Token t, const char *func_name)
{
    char msg[256];
    sprintf(msg, "Function '%s' may not return a value in all paths", func_name);
    zwarn_with_suggestion(t, msg, "Add a return statement or make the function return 'void'");
}

void warn_comparison_always_true(Token t, const char *reason)
{
    zwarn_with_suggestion(t, "Comparison is always true", reason);
}

void warn_comparison_always_false(Token t, const char *reason)
{
    zwarn_with_suggestion(t, "Comparison is always false", reason);
}

void warn_unused_parameter(Token t, const char *param_name, const char *func_name)
{
    char msg[256];
    sprintf(msg, "Unused parameter '%s' in function '%s'", param_name, func_name);
    zwarn_with_suggestion(t, msg, "Consider prefixing with '_' if intentionally unused");
}

void warn_narrowing_conversion(Token t, const char *from_type, const char *to_type)
{
    char msg[256];
    sprintf(msg, "Narrowing conversion from '%s' to '%s'", from_type, to_type);
    zwarn_with_suggestion(t, msg, "This may cause data loss");
}

void warn_division_by_zero(Token t)
{
    zwarn_with_suggestion(t, "Division by zero", "This will cause undefined behavior at runtime");
}

void warn_integer_overflow(Token t, const char *type_name, long long value)
{
    char msg[256];
    sprintf(msg, "Integer literal %lld overflows type '%s'", value, type_name);
    zwarn_with_suggestion(t, msg, "Value will be truncated");
}

void warn_array_bounds(Token t, int index, int size)
{
    char msg[256];
    sprintf(msg, "Array index %d is out of bounds for array of size %d", index, size);
    char note[256];
    sprintf(note, "Valid indices are 0 to %d", size - 1);
    zwarn_with_suggestion(t, msg, note);
}

void warn_format_string(Token t, int arg_num, const char *expected, const char *got)
{
    char msg[256];
    sprintf(msg, "Format argument %d: expected '%s', got '%s'", arg_num, expected, got);
    zwarn_with_suggestion(t, msg, "Mismatched format specifier may cause undefined behavior");
}

void warn_null_pointer(Token t, const char *expr)
{
    char msg[256];
    sprintf(msg, "Potential null pointer access in '%s'", expr);
    zwarn_with_suggestion(t, msg, "Add a null check before accessing");
}

void warn_void_main(Token t)
{
    zwarn_with_suggestion(t, "'void main()' is non-standard and leads to undefined behavior",
                          "Consider using 'fn main()' or 'fn main() -> c_int' instead");
}
