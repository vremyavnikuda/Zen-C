
#include "diagnostics.h"
#include "parser.h"

void zpanic(const char *fmt, ...)
{
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
    }
}

void zpanic_at(Token t, const char *fmt, ...)
{
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

void zerror_at(Token t, const char *fmt, ...)
{
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

    if (g_parser_ctx && g_parser_ctx->on_error)
    {
        // Construct error message buffer
        char full_msg[1024];
        snprintf(full_msg, sizeof(full_msg), "%s (Suggestion: %s)", msg,
                 suggestion ? suggestion : "");
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
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Unused variable '%s'", var_name);
    zwarn_at(t, "%s", msg);
    fprintf(stderr,
            COLOR_CYAN "   = note: " COLOR_RESET "Consider removing it or prefixing with '_'\n");
}

void warn_shadowing(Token t, const char *var_name)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Variable '%s' shadows a previous declaration", var_name);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "This can lead to confusion\n");
}

void warn_unreachable_code(Token t)
{
    if (g_config.quiet)
    {
        return;
    }
    zwarn_at(t, "Unreachable code detected");
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "This code will never execute\n");
}

void warn_implicit_conversion(Token t, const char *from_type, const char *to_type)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Implicit conversion from '%s' to '%s'", from_type, to_type);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "Consider using an explicit cast\n");
}

void warn_missing_return(Token t, const char *func_name)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Function '%s' may not return a value in all paths", func_name);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET
                               "Add a return statement or make the function return 'void'\n");
}

void warn_comparison_always_true(Token t, const char *reason)
{
    if (g_config.quiet)
    {
        return;
    }
    zwarn_at(t, "Comparison is always true");
    if (reason)
    {
        fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "%s\n", reason);
    }
}

void warn_comparison_always_false(Token t, const char *reason)
{
    if (g_config.quiet)
    {
        return;
    }
    zwarn_at(t, "Comparison is always false");
    if (reason)
    {
        fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "%s\n", reason);
    }
}

void warn_unused_parameter(Token t, const char *param_name, const char *func_name)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Unused parameter '%s' in function '%s'", param_name, func_name);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET
                               "Consider prefixing with '_' if intentionally unused\n");
}

void warn_narrowing_conversion(Token t, const char *from_type, const char *to_type)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Narrowing conversion from '%s' to '%s'", from_type, to_type);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "This may cause data loss\n");
}

void warn_division_by_zero(Token t)
{
    if (g_config.quiet)
    {
        return;
    }
    zwarn_at(t, "Division by zero");
    fprintf(stderr,
            COLOR_CYAN "   = note: " COLOR_RESET "This will cause undefined behavior at runtime\n");
}

void warn_integer_overflow(Token t, const char *type_name, long long value)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Integer literal %lld overflows type '%s'", value, type_name);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "Value will be truncated\n");
}

void warn_array_bounds(Token t, int index, int size)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Array index %d is out of bounds for array of size %d", index, size);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "Valid indices are 0 to %d\n", size - 1);
}

void warn_format_string(Token t, int arg_num, const char *expected, const char *got)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Format argument %d: expected '%s', got '%s'", arg_num, expected, got);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET
                               "Mismatched format specifier may cause undefined behavior\n");
}

void warn_null_pointer(Token t, const char *expr)
{
    if (g_config.quiet)
    {
        return;
    }
    char msg[256];
    sprintf(msg, "Potential null pointer access in '%s'", expr);
    zwarn_at(t, "%s", msg);
    fprintf(stderr, COLOR_CYAN "   = note: " COLOR_RESET "Add a null check before accessing\n");
}
