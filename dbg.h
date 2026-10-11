/*
 * dbg.h
 *
 * Small C23 print-debugging library.
 *
 * ================================================================
 * BASIC USAGE
 * ================================================================
 *
 * In exactly one translation unit:
 *
 *     #define DBG_IMPLEMENTATION
 *     #include "dbg.h"
 *
 * Everywhere else:
 *
 *     #include "dbg.h"
 *
 *
 * Interface:
 *
 *     DBG_MSG("x = %d", x)
 *     DBG_LOG(code)
 *     DBG_LOG_OPEN(code)
 *     DBG_TRACK(a, b, c)
 *
 *
 * DBG_MSG(...)
 *     Prints source location, log-site ID, execution count, and
 *     the supplied printf-style message.
 *
 * DBG_LOG(code)
 *     Prints source location, log-site ID, execution count, and
 *     the source representation of `code`, then executes it.
 *
 * DBG_LOG_OPEN(code)
 *     Same as DBG_LOG, but intentionally expands to multiple
 *     statements rather than a do/while wrapper.
 *
 * DBG_TRACK(...)
 *     Prints the name, type, size, address, and raw bytes of each
 *     supplied object.
 *
 *
 * ================================================================
 * DISABLING THE DEBUGGING MACROS
 * ================================================================
 *
 * Define these BEFORE including dbg.h:
 *
 *     #define DBG_MSG(...)       ((void)0)
 *     #define DBG_LOG(code)      code
 *     #define DBG_LOG_OPEN(code) code
 *     #define DBG_TRACK(...)     ((void)0)
 *
 * The header will not redefine macros which already exist.
 *
 *
 * ================================================================
 * USER CONFIGURATION
 * ================================================================
 *
 * These may be defined before including dbg.h:
 *
 *     DBG_STREAM
 *         Output stream. Defaults to stderr.
 *
 *     DBG_LOG_FUNCTION
 *         Function used by DBG_LOG. Defaults to dbg_log.
 *
 *     DBG_TRACK_FUNCTION
 *         Function used by DBG_TRACK. Defaults to dbg_track.
 *
 *
 * For example:
 *
 *     #define DBG_STREAM my_debug_file
 *     #define DBG_LOG_FUNCTION my_log
 *     #define DBG_TRACK_FUNCTION my_track
 *
 *     #include "dbg.h"
 *
 *
 * The user functions must have the same signatures as the default
 * functions declared below.
 */

#ifndef DBG_H
#define DBG_H


#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


/* ================================================================
 * USER CONFIGURATION
 * ================================================================ */

/*
 * These are intentionally ordinary macros so that they can be
 * overridden before including this header.
 */

#ifndef DBG_STREAM
#    define DBG_STREAM stderr
#endif

#ifndef DBG_LOG_FUNCTION
#    define DBG_LOG_FUNCTION dbg_log
#endif

#ifndef DBG_TRACK_FUNCTION
#    define DBG_TRACK_FUNCTION dbg_track
#endif


/*
 * ================================================================
 * USER MACRO REDEFINITION
 * ================================================================
 *
 * This section is intentionally empty.
 *
 * The public macros below are defined only when they have not
 * already been defined by the user.
 *
 * Example:
 *
 *     #define DBG_MSG(...)       ((void)0)
 *     #define DBG_LOG(code)      code
 *     #define DBG_LOG_OPEN(code) code
 *     #define DBG_TRACK(...)     ((void)0)
 *
 *     #include "dbg.h"
 *
 * This makes it possible to compile the same source with debugging
 * either enabled or completely removed.
 */


/* ================================================================
 * PUBLIC TYPES
 * ================================================================ */

typedef struct {
    const char *file;
    unsigned    line;
    const char *function;
} dbg_location;


typedef struct {
    const char *name;
    const char *type_name;
    const void *address;
    size_t      size;
} dbg_object;


/* ================================================================
 * PUBLIC FUNCTIONS
 * ================================================================ */

void dbg_log(
    dbg_location location,
    unsigned long long site,
    unsigned long long count,
    const char *code
);


void dbg_track(
    dbg_location location,
    const dbg_object *object
);


/* ================================================================
 * INTERNAL MACROS
 * ================================================================ */

#define DBG__CAT_I(a, b) a ## b
#define DBG__CAT(a, b)   DBG__CAT_I(a, b)


#define DBG__LOCATION \
    (dbg_location){   \
        .file     = __FILE__, \
        .line     = __LINE__, \
        .function = __func__  \
    }


/*
 * ================================================================
 * TYPE INFORMATION
 * ================================================================
 */

#define DBG__TYPE_NAME(x) _Generic((x),       \
    char:               "char",               \
    signed char:        "signed char",        \
    unsigned char:      "unsigned char",      \
    short:              "short",              \
    unsigned short:     "unsigned short",     \
    int:                "int",               \
    unsigned int:       "unsigned int",       \
    long:               "long",              \
    unsigned long:      "unsigned long",     \
    long long:          "long long",         \
    unsigned long long: "unsigned long long",\
    float:              "float",             \
    double:             "double",            \
    long double:        "long double",       \
    _Bool:              "_Bool",             \
    void *:             "void *",            \
    const void *:       "const void *",      \
    char *:             "char *",            \
    const char *:       "const char *",      \
    default:            "<unknown type>"     \
)


/*
 * Print a value while the original C type is still available.
 *
 * This is deliberately performed by the macro rather than
 * dbg_track(), since dbg_track() only receives a void pointer.
 */

#define DBG__PRINT_VAR(x)                                      \
    do {                                                       \
        fprintf(DBG_STREAM, "%s = ", #x);                      \
        _Generic((x),                                          \
            char:               fprintf(DBG_STREAM, "%c\n", (x)), \
            signed char:        fprintf(DBG_STREAM, "%hhd\n", (x)), \
            unsigned char:      fprintf(DBG_STREAM, "%hhu\n", (x)), \
            short:              fprintf(DBG_STREAM, "%hd\n", (x)), \
            unsigned short:     fprintf(DBG_STREAM, "%hu\n", (x)), \
            int:                fprintf(DBG_STREAM, "%d\n", (x)), \
            unsigned int:       fprintf(DBG_STREAM, "%u\n", (x)), \
            long:               fprintf(DBG_STREAM, "%ld\n", (x)), \
            unsigned long:      fprintf(DBG_STREAM, "%lu\n", (x)), \
            long long:          fprintf(DBG_STREAM, "%lld\n", (x)), \
            unsigned long long: fprintf(DBG_STREAM, "%llu\n", (x)), \
            float:              fprintf(DBG_STREAM, "%f\n", (x)), \
            double:             fprintf(DBG_STREAM, "%f\n", (x)), \
            long double:        fprintf(DBG_STREAM, "%Lf\n", (x)), \
            _Bool:              fprintf(DBG_STREAM, "%s\n",       \
                                      (x) ? "true" : "false"),    \
            char *:             fprintf(DBG_STREAM, "\"%s\"\n", (x)), \
            const char *:       fprintf(DBG_STREAM, "\"%s\"\n", (x)), \
            void *:             fprintf(DBG_STREAM, "%p\n", (x)), \
            const void *:       fprintf(DBG_STREAM, "%p\n", (x)), \
            default:            fprintf(DBG_STREAM,              \
                                      "<unsupported type>\n")   \
        );                                                     \
    } while (0)


/*
 * ================================================================
 * DBG_TRACK
 * ================================================================
 */

#ifndef DBG_TRACK

#    define DBG_TRACK_ONE(x)                                   \
        do {                                                   \
            dbg_object DBG__object = {                         \
                .name      = #x,                               \
                .type_name = DBG__TYPE_NAME(x),                \
                .address   = &(x),                             \
                .size      = sizeof(x)                         \
            };                                                 \
                                                               \
            DBG_TRACK_FUNCTION(                               \
                DBG__LOCATION,                                \
                &DBG__object                                   \
            );                                                 \
                                                               \
            DBG__PRINT_VAR(x);                                \
        } while (0)


#    define DBG_TRACK_A(x, ...)                                \
        DBG_TRACK_ONE(x)                                       \
        __VA_OPT__(; DBG_TRACK_B(__VA_ARGS__))


#    define DBG_TRACK_B(x, ...)                                \
        DBG_TRACK_ONE(x)                                       \
        __VA_OPT__(; DBG_TRACK_A(__VA_ARGS__))


#    define DBG_TRACK(...)                                     \
        DBG_TRACK_A(__VA_ARGS__)

#endif /* DBG_TRACK */


/*
 * ================================================================
 * DBG_MSG
 * ================================================================
 *
 * This is independent of DBG_LOG because a message is not
 * necessarily associated with a block of code.
 */

#ifndef DBG_MSG

#    ifdef __COUNTER__

#        define DBG_MSG(...)                                   \
            DBG__MSG_COUNTER(__COUNTER__, __VA_ARGS__)


#        define DBG__MSG_COUNTER(id, ...)                      \
            do {                                               \
                static unsigned long long                      \
                    DBG__CAT(dbg_msg_count_, id);              \
                                                               \
                unsigned long long DBG__count =                \
                    ++DBG__CAT(dbg_msg_count_, id);            \
                                                               \
                fprintf(                                       \
                    DBG_STREAM,                                \
                    "%s:%u %s() [%llu, #%llu] ",               \
                    __FILE__,                                  \
                    __LINE__,                                  \
                    __func__,                                  \
                    (unsigned long long)(id),                  \
                    DBG__count                                  \
                );                                             \
                                                               \
                fprintf(DBG_STREAM, __VA_ARGS__);              \
                fputc('\n', DBG_STREAM);                       \
            } while (0)

#    else

/*
 * __LINE__ is used as a fallback when __COUNTER__ is unavailable.
 */

#        define DBG_MSG(...)                                   \
            DBG__MSG_LINE(__LINE__, __VA_ARGS__)


#        define DBG__MSG_LINE(line, ...)                       \
            do {                                               \
                static unsigned long long                      \
                    DBG__CAT(dbg_msg_count_, line);            \
                                                               \
                unsigned long long DBG__count =                \
                    ++DBG__CAT(dbg_msg_count_, line);          \
                                                               \
                fprintf(                                       \
                    DBG_STREAM,                                \
                    "%s:%u %s() [line %u, #%llu] ",            \
                    __FILE__,                                  \
                    __LINE__,                                  \
                    __func__,                                  \
                    (unsigned)(line),                          \
                    DBG__count                                  \
                );                                             \
                                                               \
                fprintf(DBG_STREAM, __VA_ARGS__);              \
                fputc('\n', DBG_STREAM);                       \
            } while (0)

#    endif

#endif /* DBG_MSG */


/*
 * ================================================================
 * DBG_LOG
 * ================================================================
 */

#ifndef DBG_LOG

#    ifdef __COUNTER__

#        define DBG_LOG(code)                                   \
            DBG__LOG_COUNTER(__COUNTER__, code)


#        define DBG__LOG_COUNTER(id, code)                      \
            do {                                                 \
                static unsigned long long                        \
                    DBG__CAT(dbg_log_count_, id);                \
                                                                 \
                unsigned long long DBG__count =                  \
                    ++DBG__CAT(dbg_log_count_, id);              \
                                                                 \
                DBG_LOG_FUNCTION(                               \
                    DBG__LOCATION,                              \
                    (unsigned long long)(id),                    \
                    DBG__count,                                  \
                    #code                                        \
                );                                               \
                                                                 \
                code                                            \
                fprintf(DBG_STREAM, "[SUCCESS]\n");                \
            } while (0)

#    else

#        define DBG_LOG(code)                                   \
            DBG__LOG_LINE(__LINE__, code)


#        define DBG__LOG_LINE(line, code)                        \
            do {                                                 \
                static unsigned long long                        \
                    DBG__CAT(dbg_log_count_, line);              \
                                                                 \
                unsigned long long DBG__count =                  \
                    ++DBG__CAT(dbg_log_count_, line);            \
                                                                 \
                DBG_LOG_FUNCTION(                               \
                    DBG__LOCATION,                              \
                    (unsigned long long)(line),                  \
                    DBG__count,                                  \
                    #code                                        \
                );                                               \
                                                                 \
                code                                            \
            fprintf(DBG_STREAM, "[SUCCESS]\n");                \
            } while (0)

#    endif

#endif /* DBG_LOG */


/*
 * ================================================================
 * DBG_LOG_OPEN
 * ================================================================
 *
 * Unlike DBG_LOG, this deliberately does not use a do/while
 * wrapper.
 *
 * Therefore:
 *
 *     DBG_LOG_OPEN({
 *         foo();
 *         bar();
 *     });
 *
 * expands into several independent statements.
 *
 * This is useful when the expansion itself needs to behave more
 * like source-level declarations/statements.
 */

#ifndef DBG_LOG_OPEN

#    ifdef __COUNTER__

#        define DBG_LOG_OPEN(code)                              \
            DBG__LOG_OPEN_COUNTER(__COUNTER__, code)


#        define DBG__LOG_OPEN_COUNTER(id, code)                 \
            static unsigned long long                           \
                DBG__CAT(dbg_log_count_, id);                   \
                                                                 \
            ++DBG__CAT(dbg_log_count_, id);                     \
                                                                 \
            DBG_LOG_FUNCTION(                                  \
                DBG__LOCATION,                                 \
                (unsigned long long)(id),                       \
                DBG__CAT(dbg_log_count_, id),                   \
                #code                                           \
            );                                                  \
                                                                 \
            code                                                 \
            fprintf(DBG_STREAM, "[SUCCESS]\n");                \

#    else

#        define DBG_LOG_OPEN(code)                              \
            DBG__LOG_OPEN_LINE(__LINE__, code)


#        define DBG__LOG_OPEN_LINE(line, code)                   \
            static unsigned long long                            \
                DBG__CAT(dbg_log_count_, line);                  \
                                                                 \
            ++DBG__CAT(dbg_log_count_, line);                    \
                                                                 \
            DBG_LOG_FUNCTION(                                   \
                DBG__LOCATION,                                  \
                (unsigned long long)(line),                     \
                DBG__CAT(dbg_log_count_, line),                 \
                #code                                           \
            );                                                   \
                                                                 \
            code                                             \
            fprintf(DBG_STREAM, "[SUCCESS]\n");                \

#    endif

#endif /* DBG_LOG_OPEN */


/*
 * ================================================================
 * IMPLEMENTATION
 * ================================================================
 *
 * Define DBG_IMPLEMENTATION in exactly one translation unit.
 */

#ifdef DBG_IMPLEMENTATION


/*
 * Print raw bytes of an object.
 */

static void
DBG__print_bytes(const void *address, size_t size)
{
    const unsigned char *bytes = address;

    fprintf(DBG_STREAM, "    bytes:");

    for (size_t i = 0; i < size; ++i)
        fprintf(DBG_STREAM, " %02X", bytes[i]);

    fputc('\n', DBG_STREAM);
}


/*
 * Default object tracker.
 *
 * Users can replace this by defining:
 *
 *     #define DBG_TRACK_FUNCTION my_track
 *
 * before including dbg.h.
 */

void
dbg_track(
    dbg_location location,
    const dbg_object *object
)
{
    fprintf(
        DBG_STREAM,
        "%s:%u %s(): %s\n",
        location.file,
        location.line,
        location.function,
        object->name
    );

    fprintf(
        DBG_STREAM,
        "    type:    %s\n"
        "    size:    %zu\n"
        "    address: %p\n",
        object->type_name,
        object->size,
        object->address
    );

    DBG__print_bytes(
        object->address,
        object->size
    );
}


/*
 * Default execution logger.
 *
 * Users can replace this by defining:
 *
 *     #define DBG_LOG_FUNCTION my_log
 *
 * before including dbg.h.
 */

void
dbg_log(
    dbg_location location,
    unsigned long long site,
    unsigned long long count,
    const char *code
)
{
    fprintf(
        DBG_STREAM,
        "%s:%u %s() [log %llu, #%llu]: %s\n",
        location.file,
        location.line,
        location.function,
        site,
        count,
        code
    );
}


#endif /* DBG_IMPLEMENTATION */

/* ================================================================
 * USER DEBUG FUNCTIONS
 * ================================================================
 *
 * Put project-specific debugging wrappers here.
 *
 * These are ordinary C functions/macros. They are not part of the
 * generic dbg.h interface.
 *
 * Example:
 *
 *     static void *dbg_malloc(size_t size, const char *file, int line)
 *     {
 *         void *p = malloc(size);
 *
 *         fprintf(stderr,
 *             "malloc(%zu) = %p at %s:%d\n",
 *             size, p, file, line);
 *
 *         return p;
 *     }
 *
 *     #define malloc(x) dbg_malloc((x), __FILE__, __LINE__)
 *
 * ================================================================ */

#ifdef DBG_USER_FUNCTIONS

/* User code goes here. */

#endif


#endif /* DBG_H */

