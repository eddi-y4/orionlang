#ifndef ORION_TRACE_H
#define ORION_TRACE_H

#include <stddef.h>
#include <stdint.h>

#define ORION_TRACE_MAX_FRAMES 8
#define ORION_TRACE_MAX_VARS 6
#define ORION_TRACE_MAX_OBJECTS 16
#define ORION_TRACE_MAX_FIELDS 64
#define ORION_TRACE_NAME 32
#define ORION_TRACE_VALUE 32
#define ORION_TRACE_MESSAGE 160

typedef struct {
    char name[ORION_TRACE_NAME];
    char type[ORION_TRACE_NAME];
    char value[ORION_TRACE_VALUE];
    uint64_t raw_value;
    uint64_t address;
    int is_pointer;
    char target_name[ORION_TRACE_NAME];
    uint64_t pointer_target;
    int read;
} OrionTraceVariable;

typedef struct {
    char name[ORION_TRACE_NAME];
    char type[ORION_TRACE_NAME];
    char value[ORION_TRACE_VALUE];
    uint64_t raw_value;
    uint64_t pointer_target;
    int is_pointer;
    int read;
    char target_name[ORION_TRACE_NAME];
} OrionTraceField;

typedef struct {
    char type[ORION_TRACE_NAME];
    uint64_t address;
    size_t field_count;
    size_t total_elements;
    int is_array;
    int has_more;
    int freed;
    OrionTraceField *fields;
} OrionTraceObject;

typedef struct {
    char function[ORION_TRACE_NAME];
    OrionTraceVariable vars[ORION_TRACE_MAX_VARS];
    size_t var_count;
} OrionTraceFrame;

typedef struct {
    int source_line;
    uintptr_t statement_id;
    char instruction[ORION_TRACE_MESSAGE];
    OrionTraceFrame *frames;
    size_t frame_count;
    OrionTraceObject *objects;
    size_t object_count;
} OrionTraceStep;

typedef struct {
    OrionTraceStep *steps;
    size_t count;
    size_t capacity;
} OrionTrace;

void orion_trace_free(OrionTrace *trace);

#endif
