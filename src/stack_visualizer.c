#include "raylib.h"
#include "orion/colors.h"
#include "orion/language.h"
#include "orion/lexer.h"
#include "orion/listing.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW_W 1280
#define WINDOW_H 800
#define SOURCE_LINE_HEIGHT 24
#define SOURCE_TEXT_SIZE 19

typedef struct {
    char **lines;
    size_t count;
} SourceFile;

typedef struct {
    int source_line;
    uintptr_t statement_id;
    char source_token[ORION_TRACE_NAME];
    char text[256];
} BytecodeInstruction;

typedef struct FlowLayout {
    ASTNode *ast;
    struct FlowLayout *next, *body, *otherwise;
    float w, h, total_w, total_h, local_h;
    float x, y, body_x, body_y, else_x, else_y;
    Rectangle region;
} FlowLayout;

typedef struct {
    ASTNode *ast;
    Vector2 delta;
} FlowOffset;

static FlowOffset flow_offsets[4096];
static size_t flow_offset_count;
static FlowLayout *flow_drag_node;
static Vector2 flow_drag_last_mouse;

static Vector2 flow_offset_for(ASTNode *ast) {
    for (size_t i = 0; i < flow_offset_count; ++i)
        if (flow_offsets[i].ast == ast) return flow_offsets[i].delta;
    return (Vector2){0};
}

static void flow_add_offset(ASTNode *ast, Vector2 delta) {
    for (size_t i = 0; i < flow_offset_count; ++i) {
        if (flow_offsets[i].ast == ast) {
            flow_offsets[i].delta.x += delta.x;
            flow_offsets[i].delta.y += delta.y;
            return;
        }
    }
    if (flow_offset_count < sizeof(flow_offsets) / sizeof(flow_offsets[0]))
        flow_offsets[flow_offset_count++] = (FlowOffset){ast, delta};
}

static void flow_apply_offsets(FlowLayout *list) {
    for (FlowLayout *n = list; n; n = n->next) {
        Vector2 d = flow_offset_for(n->ast);
        n->x += d.x; n->y += d.y;
        n->body_x += d.x; n->body_y += d.y;
        n->else_x += d.x; n->else_y += d.y;
        flow_apply_offsets(n->body);
        flow_apply_offsets(n->otherwise);
    }
}

static FlowLayout *flow_pick_node(FlowLayout *list, Vector2 point) {
    for (FlowLayout *n = list; n; n = n->next) {
        FlowLayout *found = flow_pick_node(n->body, point);
        if (!found) found = flow_pick_node(n->otherwise, point);
        if (found) return found;
        if (CheckCollisionPointRec(point, (Rectangle){n->x, n->y, n->w, n->h})) return n;
    }
    return NULL;
}

typedef struct { Font font; int size; uint64_t last_used; int loaded; } UIFontCacheEntry;
static UIFontCacheEntry ui_fonts[64];
static const char *ui_font_path = "assets/NotoSans-VariableFont.ttf";
static float ui_font_scale = 1.0f;
static float ui_window_scale = 1.0f;
static uint64_t ui_font_clock;
static int ui_font_error;

static Font ui_font_for_size(float size) {
    int raster_size = (int)roundf(size * ui_font_scale);
    if (raster_size < 8) raster_size = 8;
    if (raster_size > 128) raster_size = 128;
    size_t slot = 0;
    for (size_t i = 0; i < sizeof(ui_fonts) / sizeof(ui_fonts[0]); ++i) {
        if (ui_fonts[i].loaded && ui_fonts[i].size == raster_size) {
            ui_fonts[i].last_used = ++ui_font_clock;
            return ui_fonts[i].font;
        }
        if (!ui_fonts[i].loaded) { slot = i; break; }
        if (ui_fonts[i].last_used < ui_fonts[slot].last_used) slot = i;
    }
    if (ui_fonts[slot].loaded) UnloadFont(ui_fonts[slot].font);
    Font font = LoadFontEx(ui_font_path, raster_size, NULL, 0);
    if (font.texture.id == 0) { ui_font_error = 1; return GetFontDefault(); }
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    ui_fonts[slot] = (UIFontCacheEntry){font, raster_size, ++ui_font_clock, 1};
    return font;
}

static Vector2 ui_measure_text(const char *text, float size, float spacing) {
    float draw_size = size * ui_window_scale;
    return MeasureTextEx(ui_font_for_size(draw_size), text, draw_size, spacing * ui_window_scale);
}

static void ui_draw_text(const char *text, int x, int y, float size, Color color) {
    float draw_size = size * ui_window_scale;
    DrawTextEx(ui_font_for_size(draw_size), text, (Vector2){(float)x, (float)y}, draw_size, 0.25f * ui_window_scale, color);
}

static void ui_draw_fitted(const char *text, int x, int y, float size, float max_width, Color color) {
    char fitted[2048];
    size_t len = strlen(text);
    if (len >= sizeof(fitted)) len = sizeof(fitted) - 1;
    memcpy(fitted, text, len);
    fitted[len] = '\0';
    if (ui_measure_text(fitted, size, 0.25f).x > max_width) {
        while (len > 3) {
            len--;
            memcpy(fitted, text, len);
            memcpy(fitted + len, "...", 4);
            if (ui_measure_text(fitted, size, 0.25f).x <= max_width) break;
        }
    }
    ui_draw_text(fitted, x, y, size, color);
}

static const OrionTraceVariable *find_previous_variable(const OrionTraceStep *step, uint64_t address) {
    if (!step) return NULL;
    for (size_t i = 0; i < step->frame_count; ++i)
        for (size_t v = 0; v < step->frames[i].var_count; ++v)
            if (step->frames[i].vars[v].address == address) return &step->frames[i].vars[v];
    return NULL;
}

static int variable_changed(const OrionTraceStep *previous, const OrionTraceVariable *value) {
    const OrionTraceVariable *old = find_previous_variable(previous, value->address);
    return !old || old->pointer_target != value->pointer_target || strcmp(old->value, value->value) != 0;
}

static const OrionTraceObject *find_previous_object(const OrionTraceStep *step, uint64_t address) {
    if (!step) return NULL;
    for (size_t i = 0; i < step->object_count; ++i)
        if (step->objects[i].address == address) return &step->objects[i];
    return NULL;
}

static int field_changed(const OrionTraceStep *previous, uint64_t object_address, const OrionTraceField *field) {
    const OrionTraceObject *old_object = find_previous_object(previous, object_address);
    if (!old_object) return 1;
    for (size_t i = 0; i < old_object->field_count; ++i)
        if (!strcmp(old_object->fields[i].name, field->name))
            return old_object->fields[i].pointer_target != field->pointer_target || strcmp(old_object->fields[i].value, field->value) != 0;
    return 1;
}

static char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return NULL; }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return NULL; }
    char *contents = malloc((size_t)length + 1);
    if (!contents) { fclose(file); return NULL; }
    size_t read_count = fread(contents, 1, (size_t)length, file);
    contents[read_count] = '\0';
    fclose(file);
    return contents;
}

static SourceFile split_lines(const char *contents) {
    SourceFile source = {0};
    size_t capacity = 0;
    const char *start = contents;
    for (const char *p = contents;; ++p) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            if (len && start[len - 1] == '\r') --len;
            if (source.count == capacity) {
                capacity = capacity ? capacity * 2 : 64;
                source.lines = realloc(source.lines, capacity * sizeof(*source.lines));
                if (!source.lines) { perror("realloc"); exit(1); }
            }
            source.lines[source.count] = malloc(len + 1);
            if (!source.lines[source.count]) { perror("malloc"); exit(1); }
            memcpy(source.lines[source.count], start, len);
            source.lines[source.count][len] = '\0';
            ++source.count;
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    return source;
}

static void free_source(SourceFile *source) {
    for (size_t i = 0; i < source->count; ++i) free(source->lines[i]);
    free(source->lines);
    memset(source, 0, sizeof(*source));
}

static BytecodeInstruction *parse_bytecode_instructions(SourceFile *listing, size_t *count_out) {
    BytecodeInstruction *instructions = NULL;
    size_t count = 0, capacity = 0;
    int source_line = 0;
    uintptr_t current_statement_id = 0;
    char source_token[ORION_TRACE_NAME] = "";
    for (size_t i = 0; i < listing->count; ++i) {
        const char *line = listing->lines[i];
        int marker_line;
        char marker_token[ORION_TRACE_NAME];
        void *statement = NULL;
        if (sscanf(line, ";@ %d %31s %p", &marker_line, marker_token, &statement) == 3) {
            source_line = marker_line;
            snprintf(source_token, sizeof(source_token), "%s", marker_token);
            current_statement_id = (uintptr_t)statement;
            continue;
        }
        while (*line == ' ' || *line == '\t') ++line;
        if (source_line <= 0 || !*line || *line == ';' ||
            (line[0] == 'L' && line[1] >= '0' && line[1] <= '9' && strchr(line, ':')) ||
            strncmp(line, "FUNC ", 5) == 0 || strncmp(line, "END_FUNC", 8) == 0 ||
            strncmp(line, "STRUCT ", 7) == 0 || strncmp(line, "END_STRUCT", 10) == 0 ||
            strncmp(line, "FIELD ", 6) == 0 || strncmp(line, "PARAM ", 6) == 0) continue;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 64;
            BytecodeInstruction *grown = realloc(instructions, next * sizeof(*grown));
            if (!grown) { free(instructions); return NULL; }
            instructions = grown;
            capacity = next;
        }
        instructions[count].source_line = source_line;
        instructions[count].statement_id = current_statement_id;
        snprintf(instructions[count].source_token, sizeof(instructions[count].source_token), "%s", source_token);
        snprintf(instructions[count].text, sizeof(instructions[count].text), "%s", line);
        ++count;
    }
    *count_out = count;
    return instructions;
}

static size_t *build_bytecode_timeline(const OrionTrace *trace, const BytecodeInstruction *instructions,
                                       size_t instruction_count, size_t **event_indexes_out) {
    size_t *step_offsets = calloc(trace->count + 1, sizeof(*step_offsets));
    size_t *event_indexes = NULL, event_count = 0, event_capacity = 0;
    if (!step_offsets) return NULL;
    for (size_t s = 0; s < trace->count; ++s) {
        const OrionTraceStep *step = &trace->steps[s];
        step_offsets[s] = event_count;
        int found_exact = 0;
        for (size_t i = 0; i < instruction_count; ++i)
            if (instructions[i].statement_id == step->statement_id) found_exact = 1;
        for (size_t i = 0; i < instruction_count; ++i) {
            if (found_exact ? instructions[i].statement_id != step->statement_id :
                (instructions[i].source_line != step->source_line || strcmp(instructions[i].source_token, step->instruction) != 0)) continue;
            if (event_count == event_capacity) {
                size_t next = event_capacity ? event_capacity * 2 : 128;
                size_t *grown = realloc(event_indexes, next * sizeof(*grown));
                if (!grown) { free(step_offsets); free(event_indexes); return NULL; }
                event_indexes = grown;
                event_capacity = next;
            }
            event_indexes[event_count++] = i;
        }
    }
    step_offsets[trace->count] = event_count;
    *event_indexes_out = event_indexes;
    return step_offsets;
}

static void draw_bytecode_queue(Rectangle panel, const BytecodeInstruction *instructions,
                               const size_t *step_offsets, const size_t *event_indexes,
                               size_t trace_count, size_t cursor, size_t animated_event,
                               int animation_direction, float animation_progress) {
    float center_x = panel.x + panel.width * 0.5f;
    DrawLineEx((Vector2){center_x, 148}, (Vector2){center_x, panel.y + panel.height - 14}, 1.0f, ORI_COLOR_FRAME_OUTLINE);
    ui_draw_text("POPPED / EXECUTED", (int)panel.x + 16, 143, 14, ORI_COLOR_STEP_TEXT);
    ui_draw_text("PUSHED / QUEUED", (int)center_x + 12, 143, 14, ORI_COLOR_INSTRUCTION_TEXT);

    size_t executed = cursor;
    size_t total = step_offsets ? step_offsets[trace_count] : 0;
    int row_h = 30;
    int visible = ((int)(panel.y + panel.height) - 185) / row_h;
    if (visible < 1) visible = 1;
    int half_visible = visible / 2;
    size_t history_show = executed < (size_t)half_visible ? executed : (size_t)half_visible;
    for (size_t row = 0; row < history_show; ++row) {
        size_t i = executed - 1 - row;
        if (animation_direction > 0 && animation_progress < 1.0f && i == animated_event) continue;
        int y = 169 + (int)row * row_h;
        Rectangle card = {panel.x + 12, (float)y, panel.width * 0.5f - 30, (float)row_h - 4};
        DrawRectangleRounded(card, 0.12f, 5, ORI_COLOR_FRAME_INACTIVE);
        ui_draw_fitted(TextFormat("#%zu  %s", i + 1, instructions[event_indexes[i]].text), (int)card.x + 7, y + 5, 14, card.width - 14, ORI_COLOR_MUTED_TEXT);
    }
    size_t pending_count = total - executed;
    size_t pending_show = pending_count < (size_t)(visible - half_visible) ? pending_count : (size_t)(visible - half_visible);
    for (size_t i = 0; i < pending_show; ++i) {
        if (animation_direction < 0 && animation_progress < 1.0f && executed + i == animated_event) continue;
        int y = 169 + (int)i * row_h;
        Rectangle card = {center_x + 10, (float)y, panel.width * 0.5f - 30, (float)row_h - 4};
        Color fill = i == 0 ? ORI_COLOR_CHANGED_FILL : ORI_COLOR_FRAME_INACTIVE;
        Color ink = i == 0 ? ORI_COLOR_ACTIVE_TEXT : ORI_COLOR_TEXT;
        DrawRectangleRounded(card, 0.12f, 5, fill);
        ui_draw_fitted(TextFormat("#%zu  %s", executed + i + 1, instructions[event_indexes[executed + i]].text), (int)card.x + 7, y + 5, 14, card.width - 14, ink);
    }
    if (total && animation_direction && animation_progress < 1.0f && animated_event < total) {
        float t = animation_progress * animation_progress * (3.0f - 2.0f * animation_progress);
        float left_x = panel.x + 12.0f;
        float right_x = center_x + 10.0f;
        float card_w = panel.width * 0.5f - 30.0f;
        float start_x = animation_direction > 0 ? right_x : left_x;
        float end_x = animation_direction > 0 ? left_x : right_x;
        Rectangle moving = {start_x + (end_x - start_x) * t, 169.0f, card_w, (float)row_h - 4};
        DrawRectangleRounded(moving, 0.12f, 5, ORI_COLOR_CHANGED_FILL);
        ui_draw_fitted(TextFormat("#%zu  %s", animated_event + 1, instructions[event_indexes[animated_event]].text), (int)moving.x + 7,
                       (int)moving.y + 5, 14, moving.width - 14, ORI_COLOR_ACTIVE_TEXT);
    }
    if (!total) ui_draw_text("No traced instructions for this program.", (int)panel.x + 18, 180, 16, ORI_COLOR_MUTED_TEXT);
}

static void draw_button(Rectangle r, const char *label, int enabled) {
    Color fill = enabled ? ORI_COLOR_BUTTON_ACTIVE : ORI_COLOR_BUTTON_DISABLED;
    DrawRectangleRounded(r, 0.18f, 8, fill);
    DrawRectangleRoundedLines(r, 0.18f, 8, enabled ? ORI_COLOR_BUTTON_BORDER : ORI_COLOR_BUTTON_DISABLED_BORDER);
    Vector2 extent = ui_measure_text(label, 18, 0.25f);
    ui_draw_text(label, (int)(r.x + (r.width - extent.x) / 2), (int)(r.y + 12), 18, enabled ? ORI_COLOR_BRIGHT_TEXT : ORI_COLOR_MUTED_TEXT);
}

static void move_step(int *index, int delta, size_t count) {
    if (delta < 0 && *index > 0) --*index;
    if (delta > 0 && (size_t)(*index + 1) < count) ++*index;
}

static void navigate_step(int *trace_index, int delta, size_t trace_count, int bytecode_mode,
                          size_t *bytecode_cursor, const size_t *step_offsets,
                          size_t total_events, size_t *animated_event, int *animation_direction,
                          float *animation_progress) {
    if (!trace_count) return;
    if (bytecode_mode && step_offsets) {
        if (delta > 0 && *bytecode_cursor < total_events) {
            ++*bytecode_cursor;
            *animated_event = *bytecode_cursor - 1;
            *animation_direction = 1;
            *animation_progress = 0.0f;
        } else if (delta < 0 && *bytecode_cursor > 0) {
            --*bytecode_cursor;
            *animated_event = *bytecode_cursor;
            *animation_direction = -1;
            *animation_progress = 0.0f;
        }
        if (*bytecode_cursor >= total_events) *trace_index = (int)trace_count - 1;
        else {
            for (size_t s = 0; s < trace_count; ++s) {
                if (*bytecode_cursor < step_offsets[s + 1]) { *trace_index = (int)s; break; }
            }
        }
        return;
    }
    move_step(trace_index, delta, trace_count);
    *bytecode_cursor = step_offsets ? step_offsets[*trace_index] : 0;
}

static const char *flow_label(ASTNode *node, SourceFile *source) {
    if (!node) return "";
    if (node->type == AST_IF_STMT || node->type == AST_WHILE_STMT) {
        static char condition_label[ORION_TRACE_MESSAGE];
        int line = node->opToken.line;
        const char *source_text = (line > 0 && (size_t)line <= source->count) ? source->lines[line - 1] : "";
        while (*source_text == ' ' || *source_text == '\t') ++source_text;
        snprintf(condition_label, sizeof(condition_label), "%s", source_text);
        return condition_label;
    }
    int line = node->opToken.line;
    if (line > 0 && (size_t)line <= source->count) {
        const char *text = source->lines[line - 1];
        while (*text == ' ' || *text == '\t') ++text;
        return text;
    }
    return node->opToken.lexeme ? node->opToken.lexeme : "statement";
}

static int line_in_subtree(ASTNode *node, int line) {
    for (ASTNode *n = node; n; n = n->next) {
        if (n->opToken.line == line) return 1;
        if ((n->type == AST_IF_STMT || n->type == AST_WHILE_STMT) &&
            (line_in_subtree(n->right, line) || line_in_subtree(n->extra, line))) return 1;
    }
    return 0;
}

/* Measure deepest branches first, then place them into disjoint branch lanes. */
static FlowLayout *flow_layout_build(ASTNode *ast, SourceFile *source) {
    if (!ast) return NULL;
    FlowLayout *n = calloc(1, sizeof(*n));
    if (!n) { perror("calloc flow layout"); exit(1); }
    n->ast = ast;
    const char *label = flow_label(ast, source);
    float font = 18.0f;
    float label_w = ui_measure_text(label, font, 0.2f).x;
    if (label_w > 300.0f) label_w *= 12.0f / font;
    n->w = fmaxf(126.0f, fminf(320.0f, label_w + 26.0f));
    n->h = 46.0f;
    if (ast->type == AST_IF_STMT || ast->type == AST_WHILE_STMT) {
        n->w = fmaxf(170.0f, fminf(340.0f, label_w * 1.75f + 30.0f));
        n->h = 64.0f;
        n->body = flow_layout_build(ast->right, source);
        if (ast->type == AST_IF_STMT) n->otherwise = flow_layout_build(ast->extra, source);
    }
    n->next = flow_layout_build(ast->next, source);
    float local_w = n->w;
    if (ast->type == AST_IF_STMT) {
        float a = n->body ? n->body->total_w : 120.0f;
        float b = n->otherwise ? n->otherwise->total_w : 120.0f;
        local_w = fmaxf(local_w, a + b + 100.0f);
        float ah = n->body ? n->body->total_h : 46.0f;
        float bh = n->otherwise ? n->otherwise->total_h : 46.0f;
        n->local_h = n->h + 90.0f + fmaxf(ah, bh) + 70.0f;
    } else if (ast->type == AST_WHILE_STMT) {
        float bw = n->body ? n->body->total_w : 120.0f;
        float bh = n->body ? n->body->total_h : 46.0f;
        local_w = fmaxf(local_w, n->w + bw + 180.0f);
        n->local_h = n->h + 90.0f + bh + 68.0f;
    } else n->local_h = n->h;
    n->total_w = fmaxf(local_w, n->next ? n->next->total_w : 0.0f);
    n->total_h = n->local_h + (n->next ? 66.0f + n->next->total_h : 0.0f);
    return n;
}

static void flow_layout_place(FlowLayout *n, float x, float y, float available_w) {
    if (!n) return;
    float local_x = x + (available_w - n->total_w) / 2.0f;
    if (n->ast->type == AST_IF_STMT) {
        float aw = n->body ? n->body->total_w : 120.0f;
        float bw = n->otherwise ? n->otherwise->total_w : 120.0f;
        float branches = aw + bw + 100.0f;
        float group = fmaxf(n->w, branches);
        float gx = local_x + (n->total_w - group) / 2.0f;
        n->x = gx + (group - n->w) / 2.0f; n->y = y;
        float bx = gx + (group - branches) / 2.0f;
        n->body_x = bx; n->body_y = y + n->h + 90.0f;
        n->else_x = bx + aw + 100.0f; n->else_y = n->body_y;
        flow_layout_place(n->body, n->body_x, n->body_y, aw);
        flow_layout_place(n->otherwise, n->else_x, n->else_y, bw);
    } else if (n->ast->type == AST_WHILE_STMT) {
        float bw = n->body ? n->body->total_w : 120.0f;
        float group = n->w + bw + 180.0f;
        float gx = local_x + (n->total_w - group) / 2.0f;
        n->x = gx + 50.0f; n->y = y;
        n->body_x = n->x + n->w + 80.0f; n->body_y = y + n->h + 90.0f;
        flow_layout_place(n->body, n->body_x, n->body_y, bw);
    } else {
        n->x = local_x + (n->total_w - n->w) / 2.0f; n->y = y;
    }
    if (n->next) flow_layout_place(n->next, x, y + n->local_h + 66.0f, available_w);
}

/* Each region is the bounding rectangle around a node and all nested regions.
   Parent regions reserve the empty corridors used by the orthogonal router. */
static Rectangle flow_layout_region(FlowLayout *n) {
    if (!n) return (Rectangle){0};
    float left = n->x, top = n->y, right = n->x + n->w, bottom = n->y + n->h;
    FlowLayout *parts[] = {n->body, n->otherwise, n->next};
    for (size_t i = 0; i < sizeof(parts)/sizeof(parts[0]); ++i) {
        if (!parts[i]) continue;
        Rectangle r = flow_layout_region(parts[i]);
        if (r.x < left) left = r.x;
        if (r.y < top) top = r.y;
        if (r.x + r.width > right) right = r.x + r.width;
        if (r.y + r.height > bottom) bottom = r.y + r.height;
    }
    const float clearance = 22.0f;
    n->region = (Rectangle){left-clearance, top-clearance,
                            right-left+clearance*2, bottom-top+clearance*2};
    return n->region;
}

static void flow_layout_free(FlowLayout *n) {
    if (!n) return;
    flow_layout_free(n->body); flow_layout_free(n->otherwise);
    flow_layout_free(n->next); free(n);
}

static Vector2 layout_top(FlowLayout *n) { return (Vector2){n->x + n->w/2, n->y}; }
static Vector2 layout_bottom(FlowLayout *n) { return (Vector2){n->x + n->w/2, n->y + n->h}; }
static Vector2 layout_exit(FlowLayout *n) {
    if (!n) return (Vector2){0, 0};
    while (n->next) n = n->next;
    if (n->ast->type == AST_IF_STMT || n->ast->type == AST_WHILE_STMT)
        return (Vector2){n->x + n->w/2, n->y + n->local_h};
    return layout_bottom(n);
}

static void draw_flow_path(const Vector2 *points, int count, int active) {
    if (count < 2) return;
    (void)active;
    Color color = ORI_COLOR_FLOW_EDGE;
    float thickness = 1.7f;
    for (int i = 1; i < count; ++i) {
        if (fabsf(points[i].x - points[i-1].x) > 0.1f || fabsf(points[i].y - points[i-1].y) > 0.1f)
            DrawLineEx(points[i - 1], points[i], thickness, color);
    }
    Vector2 end = points[count - 1];
    int previous = count - 2;
    while (previous >= 0 && fabsf(points[previous].x-end.x) < 0.1f && fabsf(points[previous].y-end.y) < 0.1f) --previous;
    if (previous < 0) return;
    Vector2 prev = points[previous];
    float angle = atan2f(end.y - prev.y, end.x - prev.x), len = 10.0f;
    Vector2 a = {end.x - len*cosf(angle - 0.48f), end.y - len*sinf(angle - 0.48f)};
    Vector2 b = {end.x - len*cosf(angle + 0.48f), end.y - len*sinf(angle + 0.48f)};
    DrawLineEx(end, a, 1.8f, color);
    DrawLineEx(end, b, 1.8f, color);
    DrawTriangle(end, a, b, color);
}

static void route_down(Vector2 from, Vector2 to, float lane_y, int active) {
    Vector2 path[] = {from, {from.x, lane_y}, {to.x, lane_y}, to};
    draw_flow_path(path, 4, active);
}

static void draw_flow_layout_edges(FlowLayout *list, int path_line) {
    for (FlowLayout *n = list; n; n = n->next) {
        ASTNodeType type = n->ast->type;
        if (type == AST_IF_STMT) {
            Vector2 split = {n->x + n->w/2, n->y + n->h};
            Vector2 yes = n->body ? layout_top(n->body) : (Vector2){n->body_x + 60, n->body_y + 23};
            Vector2 no = n->otherwise ? layout_top(n->otherwise) : (Vector2){n->else_x + 60, n->else_y + 23};
            int then_taken = line_in_subtree(n->ast->right, path_line) ||
                (!n->ast->right && n->next && n->next->ast->opToken.line == path_line);
            int else_taken = line_in_subtree(n->ast->extra, path_line) ||
                (!n->ast->extra && n->next && n->next->ast->opToken.line == path_line);
            ui_draw_text("true", (int)yes.x + 5, (int)(split.y + 30), 14, ORI_COLOR_BRANCH_LABEL);
            ui_draw_text("false", (int)no.x + 5, (int)(split.y + 30), 14, ORI_COLOR_BRANCH_LABEL);
            float fork_y = split.y + 34.0f;
            route_down(split, yes, fork_y, then_taken);
            route_down(split, no, fork_y, else_taken);
            draw_flow_layout_edges(n->body, path_line);
            draw_flow_layout_edges(n->otherwise, path_line);
            if (n->next) {
                Vector2 target = layout_top(n->next);
                float branch_h = fmaxf(n->body ? n->body->total_h : 46.0f,
                                       n->otherwise ? n->otherwise->total_h : 46.0f);
                float merge_y = n->y + n->h + 90.0f + branch_h + 50.0f;
                int active = n->next->ast->opToken.line == path_line;
                if (n->body) {
                    Vector2 end = layout_exit(n->body);
                    route_down(end, target, merge_y, active);
                }
                if (n->otherwise) {
                    Vector2 end = layout_exit(n->otherwise);
                    route_down(end, target, merge_y + 12.0f, active);
                }
                if (!n->body) route_down(yes, target, merge_y, then_taken);
                if (!n->otherwise) route_down(no, target, merge_y + 12.0f, else_taken);
            }
        } else if (type == AST_WHILE_STMT) {
            Vector2 split = {n->x + n->w/2, n->y + n->h};
            Vector2 body_entry = n->body ? layout_top(n->body) : (Vector2){n->body_x + 60, n->body_y + 23};
            int body_taken = line_in_subtree(n->ast->right, path_line);
            route_down(split, body_entry, split.y + 34.0f, body_taken);
            ui_draw_text("repeat", (int)body_entry.x + 5, (int)(split.y + 30), 14, ORI_COLOR_BRANCH_LABEL);
            draw_flow_layout_edges(n->body, path_line);
            if (n->body) {
                Vector2 end = layout_exit(n->body);
                float lane_x = n->body->region.x + n->body->region.width + 10.0f;
                Vector2 target = {n->x + n->w, n->y + n->h/2};
                Vector2 path[] = {end, {end.x, end.y + 26.0f}, {lane_x, end.y + 26.0f},
                    {lane_x, target.y}, target};
                draw_flow_path(path, 5, path_line == n->ast->opToken.line);
            }
            if (n->next) {
                Vector2 exit = {n->x, n->y + n->h/2};
                Vector2 target = layout_top(n->next);
                float lane_x = n->x - 35.0f;
                float lower_y = target.y - 28.0f;
                int active = n->next->ast->opToken.line == path_line && !body_taken;
                Vector2 path[] = {exit, {lane_x, exit.y}, {lane_x, lower_y},
                    {target.x, lower_y}, target};
                draw_flow_path(path, 5, active);
            }
        } else if (n->next) {
            Vector2 from = layout_bottom(n), to = layout_top(n->next);
            route_down(from, to, from.y + (to.y - from.y) / 2.0f,
                       n->next->ast->opToken.line == path_line);
        }
    }
}

static void draw_flow_layout_nodes(FlowLayout *list, SourceFile *source, int current_line) {
    for (FlowLayout *n = list; n; n = n->next) {
        ASTNodeType type = n->ast->type;
        int active = n->ast->opToken.line == current_line;
        Rectangle box = {n->x, n->y, n->w, n->h};
        Vector2 center = {box.x + box.width/2, box.y + box.height/2};
        Color fill = ORI_COLOR_FLOW_DEFAULT, outline = ORI_COLOR_FLOW_OUTLINE;
        if (type == AST_IF_STMT) fill = ORI_COLOR_FLOW_IF;
        else if (type == AST_WHILE_STMT) fill = ORI_COLOR_FLOW_WHILE;
        else if (type == AST_VAR_DECL) fill = ORI_COLOR_FLOW_DECL;
        else if (type == AST_ASSIGN) fill = ORI_COLOR_FLOW_ASSIGN;
        else if (type == AST_RETURN) fill = ORI_COLOR_FLOW_RETURN;
        else if (type == AST_FREE) fill = ORI_COLOR_FLOW_FREE;
        else if (type == AST_CALL) fill = ORI_COLOR_FLOW_CALL;
        if (active) fill = ORI_COLOR_FLOW_ACTIVE;
        if (type == AST_IF_STMT) {
            Vector2 p1={center.x,box.y}, p2={box.x+box.width,center.y};
            Vector2 p3={center.x,box.y+box.height}, p4={box.x,center.y};
            DrawTriangle(p1,p4,p2,fill); DrawTriangle(p3,p4,p2,fill);
            DrawLineEx(p1,p2,1.7f,outline); DrawLineEx(p2,p3,1.7f,outline);
            DrawLineEx(p3,p4,1.7f,outline); DrawLineEx(p4,p1,1.7f,outline);
        } else if (type == AST_WHILE_STMT) {
            DrawRectangleRounded(box,0.22f,6,fill);
            DrawRectangleRoundedLines(box,0.22f,6,outline);
        } else if (type == AST_RETURN) {
            DrawEllipse((int)center.x,(int)center.y,box.width/2,box.height/2,fill);
            DrawEllipseLines((int)center.x,(int)center.y,box.width/2,box.height/2,outline);
        } else if (type == AST_FREE) {
            Vector2 p1={box.x+12,box.y}, p2={box.x+box.width-12,box.y};
            Vector2 p3={box.x+box.width,center.y}, p4={box.x+box.width-12,box.y+box.height};
            Vector2 p5={box.x+12,box.y+box.height}, p6={box.x,center.y};
            DrawTriangle(p1,p6,p2,fill); DrawTriangle(p2,p6,p3,fill);
            DrawTriangle(p2,p3,p5,fill); DrawTriangle(p3,p4,p5,fill);
            DrawLineEx(p1,p2,1.5f,outline); DrawLineEx(p2,p3,1.5f,outline);
            DrawLineEx(p3,p4,1.5f,outline); DrawLineEx(p4,p5,1.5f,outline);
            DrawLineEx(p5,p6,1.5f,outline); DrawLineEx(p6,p1,1.5f,outline);
        } else if (type == AST_VAR_DECL) {
            Vector2 p1={box.x+14,box.y}, p2={box.x+box.width,box.y};
            Vector2 p3={box.x+box.width-14,box.y+box.height}, p4={box.x,box.y+box.height};
            DrawTriangle(p1,p4,p2,fill); DrawTriangle(p2,p4,p3,fill);
            DrawLineEx(p1,p2,1.5f,outline); DrawLineEx(p2,p3,1.5f,outline);
            DrawLineEx(p3,p4,1.5f,outline); DrawLineEx(p4,p1,1.5f,outline);
        } else {
            DrawRectangleRounded(box,type==AST_CALL?0.4f:0.12f,6,fill);
            DrawRectangleRoundedLines(box,type==AST_CALL?0.4f:0.12f,6,outline);
        }
        const char *label = flow_label(n->ast, source);
        float font = 18.0f;
        float avail = box.width - ((type == AST_IF_STMT || type == AST_WHILE_STMT) ? box.width*0.46f : 22.0f);
        float measured = ui_measure_text(label,font,0.2f).x;
        if (measured > avail) font *= avail / measured;
        int label_x=(int)center.x-(int)(avail/2),label_y=(int)center.y-(int)(font/2);
        if(active) ui_draw_fitted(label,label_x+1,label_y+1,font,avail,ORI_COLOR_ACTIVE_TEXT_SHADOW);
        Color label_color=active?ORI_COLOR_ACTIVE_TEXT:ORI_COLOR_BRIGHT_TEXT;
        ui_draw_fitted(label,label_x,label_y,font,avail,label_color);
        draw_flow_layout_nodes(n->body,source,current_line);
        draw_flow_layout_nodes(n->otherwise,source,current_line);
    }
}

static FlowLayout *flow_layout_find_line(FlowLayout *list, int line) {
    for (FlowLayout *n = list; n; n = n->next) {
        if (n->ast->opToken.line == line) return n;
        FlowLayout *found = flow_layout_find_line(n->body, line);
        if (found) return found;
        found = flow_layout_find_line(n->otherwise, line);
        if (found) return found;
    }
    return NULL;
}

typedef struct {
    Rectangle box;
    uint64_t address, target;
    const char *name, *type, *value;
    int changed, read, is_heap, freed, stale;
    OrionTraceVariable *var;
    OrionTraceObject *object;
    char owner_history[192];
} MemoryNode;

typedef struct {
    uint64_t address;
    Vector2 delta;
} MemoryOffset;

typedef struct {
    uint64_t key;
    Vector2 delta;
} FunctionOffset;

static MemoryOffset memory_offsets[ORION_TRACE_MAX_FRAMES * ORION_TRACE_MAX_VARS + ORION_TRACE_MAX_OBJECTS];
static size_t memory_offset_count;
static FunctionOffset function_offsets[ORION_TRACE_MAX_FRAMES * 16];
static size_t function_offset_count;
static uint64_t memory_drag_address;
static uint64_t memory_drag_frame_key;
static Vector2 memory_drag_last_mouse;
static int memory_box_dragging;
static int memory_dragging_frame;

static uint64_t memory_frame_key(const OrionTraceFrame *frame) {
    uint64_t hash=UINT64_C(1469598103934665603);
    for(const unsigned char *p=(const unsigned char *)frame->function;*p;++p) { hash^=*p; hash*=UINT64_C(1099511628211); }
    uint64_t identity=frame->var_count ? frame->vars[0].address : 0;
    for(size_t b=0;b<sizeof(identity);++b) { hash^=(unsigned char)(identity>>(b*8)); hash*=UINT64_C(1099511628211); }
    return hash ? hash : 1;
}

static Vector2 function_offset_for(uint64_t key) {
    for(size_t i=0;i<function_offset_count;++i)
        if(function_offsets[i].key==key) return function_offsets[i].delta;
    return (Vector2){0};
}

static void function_add_offset(uint64_t key, Vector2 delta) {
    for(size_t i=0;i<function_offset_count;++i) if(function_offsets[i].key==key) {
        function_offsets[i].delta.x+=delta.x; function_offsets[i].delta.y+=delta.y; return;
    }
    if(function_offset_count<sizeof(function_offsets)/sizeof(function_offsets[0]))
        function_offsets[function_offset_count++]=(FunctionOffset){key,delta};
}

static int memory_frame_has_address(const OrionTraceFrame *frame,uint64_t address) {
    for(size_t i=0;i<frame->var_count;++i) if(frame->vars[i].address==address) return 1;
    return 0;
}

static Vector2 memory_offset_for(uint64_t address) {
    for (size_t i = 0; i < memory_offset_count; ++i)
        if (memory_offsets[i].address == address) return memory_offsets[i].delta;
    return (Vector2){0};
}

static void memory_add_offset(uint64_t address, Vector2 delta) {
    for (size_t i = 0; i < memory_offset_count; ++i) {
        if (memory_offsets[i].address == address) {
            memory_offsets[i].delta.x += delta.x;
            memory_offsets[i].delta.y += delta.y;
            return;
        }
    }
    if (memory_offset_count < sizeof(memory_offsets) / sizeof(memory_offsets[0]))
        memory_offsets[memory_offset_count++] = (MemoryOffset){address, delta};
}

#define MEMORY_ARRAY_COLUMNS 8
#define MEMORY_ARRAY_CELL_W 52.0f
#define MEMORY_ARRAY_CELL_H 42.0f
#define MEMORY_ARRAY_STEP_X 56.0f
#define MEMORY_ARRAY_STEP_Y 46.0f
#define MEMORY_ARRAY_WIDTH 460.0f

static float memory_object_width(const OrionTraceObject *obj) {
    return obj->is_array ? MEMORY_ARRAY_WIDTH : 226.0f;
}

static float memory_object_height(const OrionTraceObject *obj) {
    if (!obj->is_array) return 72.0f + (float)obj->field_count * 34.0f;
    size_t rows = (obj->field_count + MEMORY_ARRAY_COLUMNS - 1) / MEMORY_ARRAY_COLUMNS;
    if (!rows) rows = 1;
    return 52.0f + (float)rows * MEMORY_ARRAY_STEP_Y + 8.0f;
}

/* Incrementally push each newly introduced stack slot out of earlier boxes. */
static void memory_relax_new_box(Rectangle *box, const MemoryNode *nodes, size_t count) {
    const float gap = 12.0f;
    for (int pass = 0; pass < 64; ++pass) {
        int moved = 0;
        for (size_t i = 0; i < count; ++i) {
            Rectangle obstacle = nodes[i].box;
            obstacle.x -= gap * 0.5f; obstacle.y -= gap * 0.5f;
            obstacle.width += gap; obstacle.height += gap;
            if (!CheckCollisionRecs(*box, obstacle)) continue;
            float overlap_x = fminf(box->x + box->width, obstacle.x + obstacle.width) -
                              fmaxf(box->x, obstacle.x);
            float overlap_y = fminf(box->y + box->height, obstacle.y + obstacle.height) -
                              fmaxf(box->y, obstacle.y);
            if (overlap_x <= 0 || overlap_y <= 0) continue;
            if (overlap_x < overlap_y) {
                float sign = box->x + box->width * 0.5f < obstacle.x + obstacle.width * 0.5f ? -1.0f : 1.0f;
                box->x += sign * overlap_x;
            } else {
                float sign = box->y + box->height * 0.5f < obstacle.y + obstacle.height * 0.5f ? -1.0f : 1.0f;
                box->y += sign * overlap_y;
            }
            moved = 1;
        }
        if (!moved) break;
    }

    /* The local nudges improve compactness; this fallback makes separation a hard invariant. */
    for (size_t i = 0; i < count; ++i) {
        if (CheckCollisionRecs(*box, nodes[i].box)) {
            float bottom = nodes[i].box.y + nodes[i].box.height;
            for (size_t j = 0; j < count; ++j)
                bottom = fmaxf(bottom, nodes[j].box.y + nodes[j].box.height);
            box->y = bottom + gap;
            break;
        }
    }
}

/* Treat each function frame as a container: its variables move together, and sibling
   frames are kept clear of heap allocations and of one another. */
static void memory_align_function_variables(Rectangle *boxes, size_t heap_count,
                                            const OrionTraceStep *step) {
    size_t pos=0;
    for(size_t fi=step->frame_count;fi>0;--fi) {
        size_t frame_vars=step->frames[fi-1].var_count;
        if(frame_vars>1) {
            size_t first=heap_count+pos;
            float xs[ORION_TRACE_MAX_VARS],top=boxes[first].y;
            for(size_t i=0;i<frame_vars;++i) { xs[i]=boxes[first+i].x; top=fminf(top,boxes[first+i].y); }
            for(size_t i=1;i<frame_vars;++i) {
                float value=xs[i]; size_t j=i;
                while(j && xs[j-1]>value) { xs[j]=xs[j-1]; --j; }
                xs[j]=value;
            }
            float left=xs[(frame_vars-1)/2];
            left=roundf(left/8.0f)*8.0f;
            for(size_t i=0;i<frame_vars;++i) {
                boxes[first+i].x=left;
                boxes[first+i].y=top+(float)i*74.0f;
            }
        }
        pos+=frame_vars;
    }
}

static void memory_pack_function_boxes(Rectangle *boxes, size_t heap_count,
                                       const OrionTraceStep *step,
                                       Rectangle *frame_bounds) {
    MemoryNode obstacles[ORION_TRACE_MAX_FRAMES * ORION_TRACE_MAX_VARS + ORION_TRACE_MAX_OBJECTS];
    Rectangle prior_frames[ORION_TRACE_MAX_FRAMES];
    size_t pos=0, prior_count=0;
    for(size_t fi=step->frame_count;fi>0;--fi) {
        size_t frame_index=fi-1, frame_vars=step->frames[frame_index].var_count;
        if(frame_vars) {
            size_t first=heap_count+pos;
            Rectangle bounds=boxes[first];
            for(size_t j=first+1;j<first+frame_vars;++j) {
                float x=fminf(bounds.x,boxes[j].x),y=fminf(bounds.y,boxes[j].y);
                float right=fmaxf(bounds.x+bounds.width,boxes[j].x+boxes[j].width);
                float bottom=fmaxf(bounds.y+bounds.height,boxes[j].y+boxes[j].height);
                bounds=(Rectangle){x,y,right-x,bottom-y};
            }
            bounds=(Rectangle){bounds.x-12.0f,bounds.y-26.0f,bounds.width+24.0f,bounds.height+38.0f};
            size_t obstacle_count=0;
            for(size_t j=0;j<heap_count;++j) obstacles[obstacle_count++].box=boxes[j];
            for(size_t j=0;j<prior_count;++j) obstacles[obstacle_count++].box=prior_frames[j];
            Rectangle original=bounds;
            memory_relax_new_box(&bounds,obstacles,obstacle_count);
            float dx=bounds.x-original.x,dy=bounds.y-original.y;
            for(size_t j=first;j<first+frame_vars;++j) { boxes[j].x+=dx; boxes[j].y+=dy; }
            frame_bounds[frame_index]=bounds;
            prior_frames[prior_count++]=bounds;
        }
        pos+=frame_vars;
    }
}

static void draw_memory_arrow_head(Vector2 from, Vector2 to, Color color) {
    Vector2 d = {to.x-from.x, to.y-from.y};
    float length = sqrtf(d.x*d.x+d.y*d.y);
    if (length < 2) return;
    d.x /= length; d.y /= length;
    Vector2 end = {to.x-d.x*1.0f, to.y-d.y*1.0f};
    float a = atan2f(d.y,d.x), size = 12.0f;
    Vector2 l = {end.x-size*cosf(a-0.55f),end.y-size*sinf(a-0.55f)};
    Vector2 r = {end.x-size*cosf(a+0.55f),end.y-size*sinf(a+0.55f)};
    DrawTriangle(end,l,r,color);
    DrawLineEx(end,l,2.5f,color);
    DrawLineEx(end,r,2.5f,color);
}

static void draw_memory_route_segment(Vector2 a, Vector2 b, Color color);

static void draw_memory_arrow(Vector2 from, Vector2 to, Color color) {
    draw_memory_route_segment(from,to,color);
    DrawCircleV(from,4.0f,color);
    draw_memory_arrow_head(from,to,color);
}

typedef struct { Vector2 a,b; } MemoryRouteSegment;
static MemoryRouteSegment memory_route_segments[512];
static size_t memory_route_segment_count;

/* Keep orthogonal connectors distinct; at a crossing the newer route yields a short gap. */
static void draw_memory_route_segment(Vector2 a, Vector2 b, Color color) {
    const float eps=0.5f, gap=7.0f;
    if(fabsf(a.x-b.x)>eps && fabsf(a.y-b.y)>eps) {
        DrawLineEx(a,b,2.0f,color);
        return;
    }
    float start,end,fixed;
    int horizontal=fabsf(a.y-b.y)<=eps;
    if(horizontal) { start=fminf(a.x,b.x); end=fmaxf(a.x,b.x); fixed=a.y; }
    else { start=fminf(a.y,b.y); end=fmaxf(a.y,b.y); fixed=a.x; }
    float crossings[64]; size_t crossing_count=0;
    for(size_t i=0;i<memory_route_segment_count && crossing_count<64;++i) {
        Vector2 p=memory_route_segments[i].a,q=memory_route_segments[i].b;
        int old_horizontal=fabsf(p.y-q.y)<=eps;
        if(old_horizontal==horizontal) continue;
        float cross=horizontal?p.x:p.y;
        float old_fixed=old_horizontal?p.y:p.x;
        float old_lo=old_horizontal?fminf(p.x,q.x):fminf(p.y,q.y);
        float old_hi=old_horizontal?fmaxf(p.x,q.x):fmaxf(p.y,q.y);
        if(cross>start+gap && cross<end-gap && fixed>=old_lo-eps && fixed<=old_hi+eps && old_fixed>=start-eps && old_fixed<=end+eps)
            crossings[crossing_count++]=cross;
    }
    for(size_t i=0;i<crossing_count;++i) for(size_t j=i+1;j<crossing_count;++j)
        if(crossings[j]<crossings[i]) {float t=crossings[i];crossings[i]=crossings[j];crossings[j]=t;}
    float cursor=start;
    for(size_t i=0;i<crossing_count;++i) {
        float cross=crossings[i];
        if(cross-gap>cursor) {
            Vector2 from=horizontal?(Vector2){cursor,fixed}:(Vector2){fixed,cursor};
            Vector2 to=horizontal?(Vector2){cross-gap,fixed}:(Vector2){fixed,cross-gap};
            DrawLineEx(from,to,2.0f,color);
        }
        cursor=fmaxf(cursor,cross+gap);
    }
    if(end>cursor) {
        Vector2 from=horizontal?(Vector2){cursor,fixed}:(Vector2){fixed,cursor};
        Vector2 to=horizontal?(Vector2){end,fixed}:(Vector2){fixed,end};
        DrawLineEx(from,to,2.0f,color);
    }
    if(memory_route_segment_count<sizeof(memory_route_segments)/sizeof(memory_route_segments[0]))
        memory_route_segments[memory_route_segment_count++]=(MemoryRouteSegment){a,b};
}

static void draw_memory_link(const MemoryNode *from_node, const MemoryNode *to_node,
                             Vector2 source_port, Color color, float lane_offset) {
    Rectangle a=from_node->box, b=to_node->box;
    Vector2 ac={a.x+a.width/2,a.y+a.height/2};
    Vector2 bc={b.x+b.width/2,b.y+b.height/2};
    if(from_node->is_heap && to_node->is_heap) {
        float rail=fmaxf(a.x+a.width,b.x+b.width)+24.0f;
        Vector2 first=source_port;
        Vector2 bend1={rail,source_port.y};
        Vector2 bend2={rail,bc.y};
        Vector2 target={b.x+b.width,bc.y};
        draw_memory_route_segment(first,bend1,color);
        draw_memory_route_segment(bend1,bend2,color);
        draw_memory_route_segment(bend2,target,color);
        DrawCircleV(first,4.0f,color);
        draw_memory_arrow_head(bend2,target,color);
        return;
    }
    int rightward=bc.x>=ac.x;
    Vector2 start={from_node->is_heap?(rightward?source_port.x:a.x+8.0f):(rightward?a.x+a.width:a.x),
                   from_node->is_heap?source_port.y:ac.y};
    Vector2 end={rightward?b.x:b.x+b.width,bc.y};
    if(fabsf(start.y-end.y)<0.1f) {
        draw_memory_arrow(start,end,color);
        return;
    }
    float mid_x=(start.x+end.x)*0.5f + lane_offset;
    Vector2 corner1={mid_x,start.y}, corner2={mid_x,end.y};
    draw_memory_route_segment(start,corner1,color);
    draw_memory_route_segment(corner1,corner2,color);
    draw_memory_route_segment(corner2,end,color);
    DrawCircleV(start,4.0f,color);
    draw_memory_arrow_head(corner2,end,color);
}

static Vector2 memory_field_port(const OrionTraceObject *obj,Rectangle box,size_t field) {
    if(obj->is_array) {
        size_t col=field%MEMORY_ARRAY_COLUMNS, row=field/MEMORY_ARRAY_COLUMNS;
        float row_lane=(float)col*4.0f-14.0f;
        return (Vector2){box.x+8.0f+(float)col*MEMORY_ARRAY_STEP_X+MEMORY_ARRAY_CELL_W,
                         box.y+51.0f+(float)row*MEMORY_ARRAY_STEP_Y+MEMORY_ARRAY_CELL_H*0.5f+row_lane};
    }
    return (Vector2){box.x+box.width-8.0f,box.y+58.0f+(float)field*34.0f};
}

static void draw_memory_edges(MemoryNode *nodes,size_t count,size_t heap_count) {
    memory_route_segment_count=0;
    unsigned char handled[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS*2+ORION_TRACE_MAX_OBJECTS]={0};
    Color normal=ORI_COLOR_MEMORY_EDGE;
    for(size_t i=heap_count;i<count;++i) {
        if(handled[i] || !nodes[i].target) continue;
        size_t target_index=count;
        for(size_t j=0;j<count;++j) if(nodes[j].address==nodes[i].target) {target_index=j;break;}
        if(target_index==count) continue;
        size_t group[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS*2+ORION_TRACE_MAX_OBJECTS];
        size_t group_count=0;
        float min_y=nodes[i].box.y+nodes[i].box.height/2;
        float max_y=min_y;
        for(size_t j=i;j<count;++j) if(!nodes[j].is_heap && !nodes[j].stale && nodes[j].target==nodes[i].target && !handled[j]) {
            float cy=nodes[j].box.y+nodes[j].box.height/2;
            float next_min=fminf(min_y,cy),next_max=fmaxf(max_y,cy);
            if(next_max-next_min<=160.0f) {
                group[group_count++]=j;
                min_y=next_min; max_y=next_max;
            }
        }
        if(group_count>1 && nodes[target_index].box.x>nodes[i].box.x) {
            float target_x=nodes[target_index].box.x-18.0f;
            float mid_y=(min_y+max_y)*0.5f;
            for(size_t g=0;g<group_count;++g) {
                size_t j=group[g];
                Vector2 start={nodes[j].box.x+nodes[j].box.width,nodes[j].box.y+nodes[j].box.height/2};
                draw_memory_route_segment(start,(Vector2){target_x,start.y},nodes[j].stale?ORI_COLOR_MEMORY_EDGE_FADED:normal);
                DrawCircleV(start,4.0f,nodes[j].stale?ORI_COLOR_MEMORY_EDGE_FADED:normal);
                handled[j]=1;
            }
            draw_memory_route_segment((Vector2){target_x,min_y},(Vector2){target_x,max_y},normal);
            Vector2 branch={target_x,mid_y}, turn={target_x, nodes[target_index].box.y+nodes[target_index].box.height/2};
            draw_memory_route_segment(branch,turn,normal);
            Vector2 arrow_target={nodes[target_index].box.x,turn.y};
            draw_memory_route_segment(turn,arrow_target,normal);
            draw_memory_arrow_head(turn,arrow_target,normal);
        } else {
            for(size_t g=0;g<group_count;++g) {
                size_t j=group[g];
                Color color=nodes[j].stale?ORI_COLOR_MEMORY_EDGE_FADED:normal;
                Vector2 port={0,nodes[j].box.y+nodes[j].box.height/2};
                draw_memory_link(&nodes[j],&nodes[target_index],port,color,(float)(j-i)*8.0f);
                handled[j]=1;
            }
        }
    }

    for(size_t i=0;i<heap_count;++i) {
        OrionTraceObject *obj=nodes[i].object;
        unsigned char field_done[ORION_TRACE_MAX_FIELDS]={0};
        for(size_t f=0;f<obj->field_count;++f) {
            uint64_t target=obj->fields[f].pointer_target;
            if(!target || field_done[f]) continue;
            size_t target_index=count;
            for(size_t j=0;j<count;++j) if(nodes[j].address==target) {target_index=j;break;}
            if(target_index==count) continue;
            size_t group[ORION_TRACE_MAX_FIELDS],group_count=0;
            float min_y=0,max_y=0;
            for(size_t k=f;k<obj->field_count;++k) if(!field_done[k] && obj->fields[k].pointer_target==target) {
                float y=memory_field_port(obj,nodes[i].box,k).y;
                float next_min=group_count?fminf(min_y,y):y;
                float next_max=group_count?fmaxf(max_y,y):y;
                if(group_count && next_max-next_min>110.0f) continue;
                group[group_count++]=k;
                min_y=next_min; max_y=next_max;
            }
            float lane=nodes[i].box.x+nodes[i].box.width+24.0f;
            if(group_count>1 && nodes[target_index].is_heap) {
                float mid=(min_y+max_y)*0.5f;
                for(size_t g=0;g<group_count;++g) {
                    size_t k=group[g]; Vector2 start=memory_field_port(obj,nodes[i].box,k);
                    draw_memory_route_segment(start,(Vector2){lane,start.y},normal); DrawCircleV(start,4.0f,normal); field_done[k]=1;
                }
                draw_memory_route_segment((Vector2){lane,min_y},(Vector2){lane,max_y},normal);
                Vector2 branch={lane,mid}, turn={lane,nodes[target_index].box.y+nodes[target_index].box.height/2};
                draw_memory_route_segment(branch,turn,normal);
                Vector2 arrow_target={nodes[target_index].box.x+nodes[target_index].box.width,turn.y};
                draw_memory_route_segment(turn,arrow_target,normal);
                draw_memory_arrow_head(turn,arrow_target,normal);
            } else for(size_t g=0;g<group_count;++g) {
                size_t k=group[g]; Vector2 port=memory_field_port(obj,nodes[i].box,k);
                draw_memory_link(&nodes[i],&nodes[target_index],port,normal,(float)(k-f)*8.0f); field_done[k]=1;
            }
        }
    }
}

static const OrionTrace *owner_cache_trace;
static size_t owner_cache_step=SIZE_MAX, owner_cache_count;
static float owner_cache_width=-1.0f;
static uint64_t owner_cache_addresses[ORION_TRACE_MAX_OBJECTS];
static char owner_cache_history[ORION_TRACE_MAX_OBJECTS][192];

static void memory_owner_history(const OrionTrace *trace, size_t through_step,
                                 uint64_t address, char *out, size_t out_size, float max_width) {
    if(owner_cache_trace!=trace || owner_cache_step!=through_step || fabsf(owner_cache_width-max_width)>0.5f) {
        owner_cache_trace=trace;
        owner_cache_step=through_step;
        owner_cache_width=max_width;
        owner_cache_count=0;
        if(through_step<trace->count) {
            const OrionTraceStep *current=&trace->steps[through_step];
            owner_cache_count=current->object_count<ORION_TRACE_MAX_OBJECTS?current->object_count:ORION_TRACE_MAX_OBJECTS;
            char last_owner[ORION_TRACE_MAX_OBJECTS][ORION_TRACE_NAME]={{0}};
            for(size_t o=0;o<owner_cache_count;++o) {
                owner_cache_addresses[o]=current->objects[o].address;
                owner_cache_history[o][0]='\0';
            }
            for(size_t s=0;s<=through_step;++s) {
                const OrionTraceStep *step=&trace->steps[s];
                const OrionTraceStep *prior=s?&trace->steps[s-1]:NULL;
                char candidates[ORION_TRACE_MAX_OBJECTS][ORION_TRACE_NAME]={{0}};
                for(size_t fi=0;fi<step->frame_count;++fi)
                    for(size_t vi=0;vi<step->frames[fi].var_count;++vi) {
                        const OrionTraceVariable *v=&step->frames[fi].vars[vi];
                        if(!v->is_pointer || !v->pointer_target) continue;
                        for(size_t o=0;o<owner_cache_count;++o) if(owner_cache_addresses[o]==v->pointer_target) {
                            const OrionTraceVariable *old=find_previous_variable(prior,v->address);
                            if(!old || old->pointer_target!=v->pointer_target)
                                snprintf(candidates[o],sizeof(candidates[o]),"%s",v->name);
                        }
                    }
                if(s==0)
                    for(size_t fi=0;fi<step->frame_count;++fi)
                        for(size_t vi=0;vi<step->frames[fi].var_count;++vi) {
                            const OrionTraceVariable *v=&step->frames[fi].vars[vi];
                            if(!v->is_pointer || !v->pointer_target) continue;
                            for(size_t o=0;o<owner_cache_count;++o)
                                if(!candidates[o][0] && owner_cache_addresses[o]==v->pointer_target)
                                    snprintf(candidates[o],sizeof(candidates[o]),"%s",v->name);
                        }
                for(size_t o=0;o<owner_cache_count;++o) {
                    const char *owner=candidates[o];
                    if(!owner[0] || !strcmp(owner,last_owner[o])) continue;
                    char shifted[192];
                    snprintf(shifted,sizeof(shifted),"%s%s%s",owner,owner_cache_history[o][0]?" <- ":"",owner_cache_history[o]);
                    if(ui_measure_text(shifted,15,0.25f).x<=max_width)
                        snprintf(owner_cache_history[o],sizeof(owner_cache_history[o]),"%s",shifted);
                    else snprintf(owner_cache_history[o],sizeof(owner_cache_history[o]),"%s",owner);
                    snprintf(last_owner[o],sizeof(last_owner[o]),"%s",owner);
                }
            }
        }
    }
    out[0]='\0';
    for(size_t o=0;o<owner_cache_count;++o) if(owner_cache_addresses[o]==address) {
        snprintf(out,out_size,"%s",owner_cache_history[o]);
        break;
    }
}

static void draw_memory_view(const OrionTrace *trace, size_t trace_index,
                             const OrionTraceStep *step, const OrionTraceStep *previous,
                             Rectangle panel, Vector2 pan, float zoom) {
    if (!step) return;
    MemoryNode nodes[ORION_TRACE_MAX_FRAMES * ORION_TRACE_MAX_VARS * 2 + ORION_TRACE_MAX_OBJECTS];
    size_t count = 0, stack_count = 0;
    (void)pan; (void)zoom;
    float stack_x = panel.x + 44.0f;
    float heap_right = panel.x + panel.width - 20.0f;
    float stack_y = panel.y + 110.0f;
    float heap_y = panel.y + 110.0f;
    float heap_offset = 0.0f;

    /* Heap objects keep their allocation-order slots so positions stay stable as the trace advances. */
    for (size_t o = 0; o < step->object_count && count < sizeof(nodes)/sizeof(nodes[0]); ++o) {
        OrionTraceObject *obj = &step->objects[o];
        float box_width = memory_object_width(obj);
        Rectangle box = {heap_right - box_width, heap_y + heap_offset, box_width,
                         memory_object_height(obj)};
        heap_offset += box.height + 24.0f;
        const OrionTraceObject *old = find_previous_object(previous, obj->address);
        int object_changed = !old || old->freed != obj->freed;
        int object_read = 0;
        for (size_t f = 0; f < obj->field_count; ++f) {
            object_changed |= field_changed(previous, obj->address, &obj->fields[f]);
            object_read |= obj->fields[f].read;
        }
        MemoryNode *node=&nodes[count];
        *node=(MemoryNode){.box=box,.address=obj->address,.name=obj->type,.type="heap",
            .value="",.changed=object_changed,.read=object_read,.is_heap=1,.freed=obj->freed,.object=obj};
        memory_relax_new_box(&box,nodes,count);
        node->box=box;
        ++count;
        memory_owner_history(trace,trace_index,obj->address,node->owner_history,sizeof(node->owner_history),box.width-16.0f);
    }
    size_t heap_node_count = count;

    /* Place stack slots newest-first, building their bounding volumes from the leaves upward. */
    for (size_t fi = step->frame_count; fi > 0; --fi) {
        const OrionTraceFrame *frame = &step->frames[fi-1];
        for (size_t vi = frame->var_count; vi > 0; --vi) {
            OrionTraceVariable *v = (OrionTraceVariable *)&frame->vars[vi-1];
            float x = stack_x, y = stack_y + stack_count * 96.0f;
            if (v->is_pointer && v->pointer_target) {
                for (size_t t = 0; t < count; ++t) if (nodes[t].address == v->pointer_target) {
                    x = nodes[t].box.x - 300.0f;
                    y = nodes[t].box.y + nodes[t].box.height * 0.5f - 31.0f;
                    break;
                }
            }
            Rectangle placed={x,y,218,62};
            memory_relax_new_box(&placed,nodes,count);
            x=placed.x; y=placed.y;
            const OrionTraceVariable *old = find_previous_variable(previous, v->address);
            nodes[count++] = (MemoryNode){.box={x,y,218,62},.address=v->address,
                .target=v->pointer_target,.name=v->name,.type=v->type,.value=v->value,
                .changed=!old || old->pointer_target != v->pointer_target || strcmp(old->value,v->value),
                .read=v->read,.var=v};
            ++stack_count;
        }
    }
    /* Pack function containers as units so sibling function boxes cannot overlap. */
    size_t live_node_count=count;
    Rectangle layout_boxes[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS+ORION_TRACE_MAX_OBJECTS];
    Rectangle frame_bounds[ORION_TRACE_MAX_FRAMES]={{0}};
    for(size_t i=0;i<live_node_count;++i) layout_boxes[i]=nodes[i].box;
    memory_align_function_variables(layout_boxes,heap_node_count,step);
    memory_pack_function_boxes(layout_boxes,heap_node_count,step,frame_bounds);
    for(size_t fi=0;fi<step->frame_count;++fi) {
        Vector2 offset=function_offset_for(memory_frame_key(&step->frames[fi]));
        frame_bounds[fi].x+=offset.x; frame_bounds[fi].y+=offset.y;
    }
    for(size_t i=0;i<live_node_count;++i) {
        nodes[i].box=layout_boxes[i];
        for(size_t fi=0;fi<step->frame_count;++fi)
            if(memory_frame_has_address(&step->frames[fi],nodes[i].address)) {
                Vector2 offset=function_offset_for(memory_frame_key(&step->frames[fi]));
                nodes[i].box.x+=offset.x; nodes[i].box.y+=offset.y;
                break;
            }
        Vector2 offset=memory_offset_for(nodes[i].address);
        nodes[i].box.x+=offset.x; nodes[i].box.y+=offset.y;
    }
    for(size_t fi=0;fi<step->frame_count;++fi) {
        for(size_t vi=0;vi<step->frames[fi].var_count;++vi) {
            uint64_t address=step->frames[fi].vars[vi].address;
            for(size_t ni=heap_node_count;ni<live_node_count;++ni) {
                if(nodes[ni].address!=address) continue;
                Rectangle *bounds=&frame_bounds[fi], box=nodes[ni].box;
                float right=fmaxf(bounds->x+bounds->width,box.x+box.width);
                float bottom=fmaxf(bounds->y+bounds->height,box.y+box.height);
                bounds->x=fminf(bounds->x,box.x);
                bounds->y=fminf(bounds->y,box.y);
                bounds->width=right-bounds->x;
                bounds->height=bottom-bounds->y;
                break;
            }
        }
    }

    /* Keep recently popped stack slots as small, dim ghosts for context. */
    MemoryNode ghost_obstacles[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS*2+
                               ORION_TRACE_MAX_OBJECTS+ORION_TRACE_MAX_FRAMES];
    size_t ghost_obstacle_count=live_node_count;
    for(size_t i=0;i<live_node_count;++i) ghost_obstacles[i].box=nodes[i].box;
    for(size_t fi=0;fi<step->frame_count;++fi) if(step->frames[fi].var_count)
        ghost_obstacles[ghost_obstacle_count++].box=frame_bounds[fi];
    for(size_t s=trace_index;s>0 && count<sizeof(nodes)/sizeof(nodes[0]);--s) {
        const OrionTraceStep *old_step=&trace->steps[s-1];
        for(size_t fi=old_step->frame_count;fi>0 && count<sizeof(nodes)/sizeof(nodes[0]);--fi)
            for(size_t vi=old_step->frames[fi-1].var_count;vi>0 && count<sizeof(nodes)/sizeof(nodes[0]);--vi) {
                const OrionTraceVariable *v=&old_step->frames[fi-1].vars[vi-1];
                int already_present=0;
                for(size_t j=0;j<count;++j) if(nodes[j].address==v->address) {already_present=1;break;}
                if(already_present) continue;
                float x=stack_x,y=stack_y+stack_count*96.0f;
                if(v->is_pointer && v->pointer_target)
                for(size_t j=0;j<count;++j) if(nodes[j].address==v->pointer_target) {
                        x=nodes[j].box.x-300.0f;
                        y=nodes[j].box.y+nodes[j].box.height*0.5f-31.0f;
                        break;
                    }
                Rectangle placed={x,y,218,62};
                memory_relax_new_box(&placed,ghost_obstacles,ghost_obstacle_count);
                x=placed.x; y=placed.y;
                nodes[count]=(MemoryNode){.box={x,y,218,62},.address=v->address,
                    .target=v->pointer_target,.name=v->name,.type=v->type,.value=v->value,.stale=1};
                Vector2 offset=memory_offset_for(v->address);
                nodes[count].box.x+=offset.x; nodes[count].box.y+=offset.y;
                ghost_obstacles[ghost_obstacle_count++].box=placed;
                ++count;
                ++stack_count;
            }
    }

    Vector2 mouse=GetMousePosition();
    if(IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse,panel)) {
        for(size_t i=count;i>0;--i) {
            MemoryNode *node=&nodes[i-1];
            Rectangle box=node->box;
            if(node->stale) {
                box.x+=box.width*0.06f; box.y+=box.height*0.06f;
                box.width*=0.88f; box.height*=0.88f;
            }
            Rectangle screen_box={pan.x+box.x*zoom,pan.y+box.y*zoom,
                                  box.width*zoom,box.height*zoom};
            if(CheckCollisionPointRec(mouse,screen_box)) {
                memory_box_dragging=1;
                memory_dragging_frame=0;
                memory_drag_address=node->address;
                memory_drag_last_mouse=mouse;
                break;
            }
        }
        if(!memory_box_dragging) {
            for(size_t fi=step->frame_count;fi>0;--fi) {
                size_t index=fi-1;
                if(!step->frames[index].var_count) continue;
                Rectangle b=frame_bounds[index];
                Rectangle screen_box={pan.x+b.x*zoom,pan.y+b.y*zoom,b.width*zoom,b.height*zoom};
                if(CheckCollisionPointRec(mouse,screen_box)) {
                    memory_box_dragging=1;
                    memory_dragging_frame=1;
                    memory_drag_frame_key=memory_frame_key(&step->frames[index]);
                    memory_drag_last_mouse=mouse;
                    break;
                }
            }
        }
    }
    if(memory_box_dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 delta={(mouse.x-memory_drag_last_mouse.x)/zoom,
                       (mouse.y-memory_drag_last_mouse.y)/zoom};
        if(delta.x!=0.0f || delta.y!=0.0f) {
            if(memory_dragging_frame) {
                function_add_offset(memory_drag_frame_key,delta);
                for(size_t fi=0;fi<step->frame_count;++fi) {
                    if(memory_frame_key(&step->frames[fi])!=memory_drag_frame_key) continue;
                    frame_bounds[fi].x+=delta.x; frame_bounds[fi].y+=delta.y;
                    for(size_t vi=0;vi<step->frames[fi].var_count;++vi)
                        for(size_t i=heap_node_count;i<live_node_count;++i)
                            if(nodes[i].address==step->frames[fi].vars[vi].address) {
                                nodes[i].box.x+=delta.x; nodes[i].box.y+=delta.y;
                                break;
                            }
                }
            } else {
                memory_add_offset(memory_drag_address,delta);
                for(size_t i=0;i<count;++i) if(nodes[i].address==memory_drag_address) {
                    nodes[i].box.x+=delta.x; nodes[i].box.y+=delta.y;
                }
            }
        }
        memory_drag_last_mouse=mouse;
    }

    /* Draw routes first so unrelated heap, stack, and function boxes occlude them. */
    draw_memory_edges(nodes,count,heap_node_count);

    /* Draw frame bounding volumes around their stack allocations. */
    for(size_t fi=0;fi<step->frame_count;++fi) {
        if(!step->frames[fi].var_count) continue;
        Rectangle bounds=frame_bounds[fi];
        DrawRectangleRoundedLines(bounds,0.05f,6,ORI_COLOR_FRAME_OUTLINE);
        ui_draw_text(step->frames[fi].function,(int)bounds.x+8,(int)bounds.y+5,14,ORI_COLOR_FRAME_TITLE);
    }

    for (size_t i=0;i<count;++i) {
        MemoryNode *n=&nodes[i];
        Color fill=n->stale?ORI_COLOR_STALE_BOX:(n->freed?ORI_COLOR_FREED_BOX:
            (n->changed?ORI_COLOR_CHANGED_FILL:(n->read?ORI_COLOR_READ_FILL:
            (n->is_heap?ORI_COLOR_HEAP_BOX:ORI_COLOR_STACK_BOX))));
        Rectangle b=n->box;
        if(n->stale) {
            float shrink=0.88f;
            b.x+=b.width*(1.0f-shrink)*0.5f; b.y+=b.height*(1.0f-shrink)*0.5f;
            b.width*=shrink; b.height*=shrink;
        }
        DrawRectangleRounded(b,0.08f,6,fill);
        DrawRectangleRoundedLines(b,0.08f,6,n->stale?ORI_COLOR_STALE_OUTLINE:(n->changed?ORI_COLOR_CHANGED_BORDER:ORI_COLOR_BOX_OUTLINE));
        if(n->is_heap && n->owner_history[0])
            ui_draw_text(n->owner_history,(int)b.x+8,(int)b.y+4,15,n->stale?ORI_COLOR_STALE_TEXT:ORI_COLOR_BRIGHT_TEXT);
        else ui_draw_fitted(n->name,(int)b.x+8,(int)b.y+4,15,b.width-16,n->stale?ORI_COLOR_STALE_TEXT:ORI_COLOR_BRIGHT_TEXT);
        if(n->is_heap) {
            ui_draw_fitted(TextFormat("%s  @%08" PRIx64,n->name,n->address),
                (int)b.x+8,(int)b.y+22,12,b.width-16,ORI_COLOR_ADDRESS);
        } else {
            ui_draw_text(TextFormat("@%08" PRIx64,n->address),(int)b.x+8,(int)b.y+b.height-21,12,n->stale?ORI_COLOR_STALE_TEXT:ORI_COLOR_ADDRESS);
        }
        if (n->is_heap) {
            OrionTraceObject *obj=n->object;
            if(obj->is_array) {
                size_t shown=obj->field_count;
                for(size_t f=0;f<shown;++f) {
                    size_t col=f%MEMORY_ARRAY_COLUMNS,row=f/MEMORY_ARRAY_COLUMNS;
                    int cell_x=(int)b.x+8+(int)col*(int)MEMORY_ARRAY_STEP_X;
                    int cell_y=(int)b.y+51+(int)row*(int)MEMORY_ARRAY_STEP_Y;
                    Color cell_fill=obj->fields[f].read?ORI_COLOR_ARRAY_READ:
                        (field_changed(previous,obj->address,&obj->fields[f])?ORI_COLOR_CHANGED_FILL:ORI_COLOR_ARRAY_IDLE);
                    DrawRectangleRounded((Rectangle){(float)cell_x,(float)cell_y,MEMORY_ARRAY_CELL_W,MEMORY_ARRAY_CELL_H},0.08f,4,cell_fill);
                    ui_draw_text(obj->fields[f].name,cell_x+4,cell_y+3,11,ORI_COLOR_ADDRESS);
                    const char *value=obj->fields[f].pointer_target?TextFormat("->%04" PRIx64,obj->fields[f].pointer_target&0xffff):obj->fields[f].value;
                    ui_draw_fitted(value,cell_x+3,cell_y+22,12,46,
                        obj->fields[f].pointer_target?ORI_COLOR_POINTER_TEXT:ORI_COLOR_BRIGHT_TEXT);
                }
                if(obj->has_more)
                    ui_draw_text("...",(int)b.x+(int)b.width-22,(int)b.y+(int)b.height-18,14,ORI_COLOR_ADDRESS);
            } else for(size_t f=0;f<obj->field_count;++f) {
                int fy=(int)b.y+44+(int)f*34;
                Rectangle field_box={(float)b.x+8.0f,(float)fy-2.0f,b.width-16.0f,29.0f};
                DrawRectangleRounded(field_box,0.08f,4,n->stale?ORI_COLOR_FIELD_FILL_STALE:ORI_COLOR_FIELD_FILL);
                DrawRectangleRoundedLines(field_box,0.08f,4,n->stale?ORI_COLOR_FIELD_BORDER_STALE:ORI_COLOR_FIELD_OUTLINE);
                const char *value=obj->fields[f].pointer_target?TextFormat("-> %08" PRIx64,obj->fields[f].pointer_target):obj->fields[f].value;
                Color field_color=obj->fields[f].pointer_target?ORI_COLOR_POINTER_TEXT:
                    (obj->fields[f].read?ORI_COLOR_READ_BORDER:ORI_COLOR_BRIGHT_TEXT);
                ui_draw_fitted(TextFormat("%s: %s",obj->fields[f].name,value),(int)field_box.x+6,fy+4,14,field_box.width-12,field_color);
            }
        } else {
            ui_draw_fitted(TextFormat("%s  %s",n->type,n->value),(int)b.x+8,(int)b.y+28,14,b.width-16,
                n->stale?ORI_COLOR_FAINT_TEXT:(n->target?ORI_COLOR_POINTER_TEXT:ORI_COLOR_BRIGHT_TEXT));
        }
    }
}

static uint64_t memory_focus_address(const OrionTraceStep *step, const OrionTraceStep *previous) {
    if (!step) return 0;
    for (size_t fi=step->frame_count; fi>0; --fi)
        for (size_t vi=step->frames[fi-1].var_count; vi>0; --vi) {
            const OrionTraceVariable *v=&step->frames[fi-1].vars[vi-1];
            const OrionTraceVariable *old=find_previous_variable(previous,v->address);
            if (!old || old->pointer_target!=v->pointer_target || strcmp(old->value,v->value)) return v->address;
        }
    for (size_t i=0;i<step->frame_count;++i)
        for(size_t j=0;j<step->frames[i].var_count;++j)
            if(step->frames[i].vars[j].read) return step->frames[i].vars[j].address;
    for (size_t o=0;o<step->object_count;++o)
        for(size_t f=0;f<step->objects[o].field_count;++f) {
            const OrionTraceField *field=&step->objects[o].fields[f];
            const OrionTraceObject *old=find_previous_object(previous,step->objects[o].address);
            int changed=!old;
            if(old) {
                changed=1;
                for(size_t k=0;k<old->field_count;++k) if(!strcmp(old->fields[k].name,field->name)) {
                    changed=old->fields[k].pointer_target!=field->pointer_target || strcmp(old->fields[k].value,field->value);
                    break;
                }
            }
            if(changed || field->read) return step->objects[o].address;
        }
    return 0;
}

static Vector2 memory_focus_position(const OrionTraceStep *step, uint64_t address, Rectangle panel) {
    float stack_x=panel.x+44.0f, heap_right=panel.x+panel.width-20.0f;
    float stack_y=panel.y+110.0f, heap_y=panel.y+110.0f;
    Rectangle placed[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS+ORION_TRACE_MAX_OBJECTS];
    uint64_t addresses[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS+ORION_TRACE_MAX_OBJECTS];
    size_t n=0, stack_count=0;
    float heap_offset=0.0f;
    for(size_t o=0;o<step->object_count && n<sizeof(placed)/sizeof(placed[0]);++o) {
        OrionTraceObject *obj=&step->objects[o];
        float w=memory_object_width(obj),h=memory_object_height(obj);
        placed[n]=(Rectangle){heap_right-w,heap_y+heap_offset,w,h}; addresses[n++]=obj->address;
        heap_offset+=h+24.0f;
    }
    for(size_t fi=step->frame_count;fi>0;--fi)
        for(size_t vi=step->frames[fi-1].var_count;vi>0;--vi) {
            OrionTraceVariable *v=&step->frames[fi-1].vars[vi-1];
            float x=stack_x,y=stack_y+stack_count*96.0f;
            if(v->is_pointer && v->pointer_target)
                for(size_t t=0;t<n;++t) if(addresses[t]==v->pointer_target) {
                    x=placed[t].x-300.0f; y=placed[t].y+placed[t].height*0.5f-31.0f; break;
                }
            Rectangle r={x,y,218,62};
            /* Use the same incremental separation rule as the rendered layout. */
            MemoryNode obstacles[ORION_TRACE_MAX_FRAMES*ORION_TRACE_MAX_VARS+ORION_TRACE_MAX_OBJECTS];
            for(size_t t=0;t<n;++t) obstacles[t].box=placed[t];
            memory_relax_new_box(&r,obstacles,n); x=r.x; y=r.y;
            placed[n]=(Rectangle){x,y,218,62}; addresses[n++]=v->address; ++stack_count;
        }
    Rectangle frame_bounds[ORION_TRACE_MAX_FRAMES]={{0}};
    memory_align_function_variables(placed,step->object_count,step);
    memory_pack_function_boxes(placed,step->object_count,step,frame_bounds);
    for(size_t i=step->object_count;i<n;++i)
        if(addresses[i]==address)
            {
                Vector2 offset=memory_offset_for(address);
                Vector2 frame_offset={0};
                for(size_t fi=0;fi<step->frame_count;++fi)
                    if(memory_frame_has_address(&step->frames[fi],address)) {
                        frame_offset=function_offset_for(memory_frame_key(&step->frames[fi]));
                        break;
                    }
                return (Vector2){placed[i].x+offset.x+frame_offset.x+placed[i].width*0.5f,
                                 placed[i].y+offset.y+frame_offset.y+placed[i].height*0.5f};
            }
    heap_offset=0.0f;
    for(size_t i=0;i<step->object_count;++i) {
        OrionTraceObject *obj=&step->objects[i];
        float width=memory_object_width(obj),height=memory_object_height(obj);
        if(obj->address==address) {
            Vector2 offset=memory_offset_for(address);
            return (Vector2){heap_right-width*0.5f+offset.x,heap_y+heap_offset+height*0.5f+offset.y};
        }
        heap_offset+=height+24.0f;
    }
    return (Vector2){panel.x+panel.width/2.0f,panel.y+panel.height/2.0f};
}

static Vector2 memory_focus_size(const OrionTraceStep *step, uint64_t address) {
    if(!step) return (Vector2){0};
    for(size_t fi=0;fi<step->frame_count;++fi)
        for(size_t vi=0;vi<step->frames[fi].var_count;++vi)
            if(step->frames[fi].vars[vi].address==address) return (Vector2){218.0f,62.0f};
    for(size_t oi=0;oi<step->object_count;++oi)
        if(step->objects[oi].address==address)
            return (Vector2){memory_object_width(&step->objects[oi]),memory_object_height(&step->objects[oi])};
    return (Vector2){0};
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s path/to/program.ori\n", argv[0]);
        return 2;
    }
    char *contents = read_file(argv[1]);
    if (!contents) return 1;
    SourceFile source = split_lines(contents);
    TokenStream tokens = lex_source(contents);
    Parser parser = {.tokens = tokens.data, .current = 0, .token_count = tokens.count};
    ASTNode *program = parse_program(&parser);
    char *bytecode_text = generate_bytecode_listing(program);
    SourceFile bytecode = split_lines(bytecode_text ? bytecode_text : "; unable to generate instruction listing");
    free(bytecode_text);
    OrionTrace trace = {0};
    int result = run_program_traced(program, &trace);
    fprintf(stderr, "Program finished with exit value %d; captured %zu execution steps.\n", result, trace.count);
    size_t bytecode_instruction_count = 0;
    BytecodeInstruction *bytecode_instructions = parse_bytecode_instructions(&bytecode, &bytecode_instruction_count);
    size_t *bytecode_event_indexes = NULL;
    size_t *bytecode_step_offsets = build_bytecode_timeline(&trace, bytecode_instructions,
        bytecode_instruction_count, &bytecode_event_indexes);

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_W, WINDOW_H, TextFormat("Orion Rigel | %s", argv[1]));
    SetWindowMinSize(900, 600);
    ui_font_path = "assets/NotoSans-VariableFont.ttf";
    (void)ui_font_for_size(16);
    if (ui_font_error) {
        fprintf(stderr, "Could not load %s (run from the project directory).\n", ui_font_path);
        CloseWindow();
        return 1;
    }
    SetTargetFPS(60);

    int trace_index = 0;
    size_t bytecode_cursor = 0;
    size_t bytecode_animated_event = SIZE_MAX;
    int bytecode_animation_direction = 0;
    float bytecode_animation_progress = 1.0f;
    int previous_trace = -1;
    int playback = 0;
    float steps_per_second = 5.0f;
    float playback_accumulator = 0.0f;
    float next_repeat_timer = 0.0f;
    float back_repeat_timer = 0.0f;
    int source_top = 1;
    int bytecode_top = 1;
    int view_mode = 0; /* 0 source, 1 bytecode, 2 flow */
    Vector2 flow_pan = {0};
    float flow_zoom = 1.0f;
    int memory_view = 0;
    Vector2 memory_pan = {0};
    float memory_zoom = 1.0f;
    int previous_memory_trace = -1;
    int memory_following = 0;
    Vector2 memory_follow_target = {0};
    Vector2 flow_follow_target = {0};
    int flow_following = 0;
    int previous_flow_trace = -1;
    Vector2 drag_origin = {0};
    int panning = 0;
    Vector2 memory_drag_origin = {0};
    int memory_panning = 0;
    float memory_scroll = 0.0f;
    FlowLayout *cached_flow_layout = NULL;
    ASTNode *cached_flow_body = NULL;
    while (!WindowShouldClose()) {
        float frame_time = GetFrameTime();
        int next_down = IsKeyDown(KEY_N) || IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_SPACE);
        int back_down = IsKeyDown(KEY_B) || IsKeyDown(KEY_LEFT);
        if (next_down && (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_SPACE) || next_repeat_timer <= 0.0f)) {
            navigate_step(&trace_index, 1, trace.count, view_mode == 1, &bytecode_cursor,
                          bytecode_step_offsets, bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0,
                          &bytecode_animated_event, &bytecode_animation_direction, &bytecode_animation_progress);
            next_repeat_timer = (IsKeyPressed(KEY_N) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_SPACE)) ? 0.35f : (view_mode == 1 ? 0.30f : 0.08f);
        } else if (next_down) next_repeat_timer -= frame_time;
        else next_repeat_timer = 0.0f;
        if (back_down && (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_LEFT) || back_repeat_timer <= 0.0f)) {
            navigate_step(&trace_index, -1, trace.count, view_mode == 1, &bytecode_cursor,
                          bytecode_step_offsets, bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0,
                          &bytecode_animated_event, &bytecode_animation_direction, &bytecode_animation_progress);
            back_repeat_timer = (IsKeyPressed(KEY_B) || IsKeyPressed(KEY_LEFT)) ? 0.35f : (view_mode == 1 ? 0.30f : 0.08f);
        } else if (back_down) back_repeat_timer -= frame_time;
        else back_repeat_timer = 0.0f;
        if (IsKeyPressed(KEY_P)) { playback = !playback; playback_accumulator = 0.0f; }
        if (IsKeyPressed(KEY_LEFT_BRACKET)) steps_per_second = fmaxf(0.5f, steps_per_second - 0.5f);
        if (IsKeyPressed(KEY_RIGHT_BRACKET)) steps_per_second = fminf(60.0f, steps_per_second + 0.5f);
        if (playback) {
            playback_accumulator += frame_time;
            float step_period = 1.0f / steps_per_second;
            while (playback_accumulator >= step_period && playback &&
                   !(view_mode == 1 && bytecode_animation_direction && bytecode_animation_progress < 1.0f)) {
                int old_index = trace_index;
                size_t old_bytecode_cursor = bytecode_cursor;
                navigate_step(&trace_index, 1, trace.count, view_mode == 1, &bytecode_cursor,
                              bytecode_step_offsets, bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0,
                              &bytecode_animated_event, &bytecode_animation_direction, &bytecode_animation_progress);
                playback_accumulator = view_mode == 1 ? 0.0f : playback_accumulator - step_period;
                if (view_mode == 1 ? bytecode_cursor == old_bytecode_cursor : trace_index == old_index || (size_t)(trace_index + 1) >= trace.count) playback = 0;
                if (view_mode == 1) break;
            }
        }
        if (IsKeyPressed(KEY_R)) { trace_index = 0; bytecode_cursor = 0; bytecode_animation_direction = 0; bytecode_animation_progress = 1.0f; }
        int w = GetScreenWidth(), h = GetScreenHeight();
        float window_scale=fminf((float)w/(float)WINDOW_W,(float)h/(float)WINDOW_H);
        ui_window_scale=fmaxf(0.80f,fminf(1.15f,window_scale));
        Rectangle back = {28, h - 66, 112, 42};
        Rectangle reset = {150, h - 66, 112, 42};
        Rectangle next = {272, h - 66, 112, 42};
        Rectangle play_button = {394, h - 66, 104, 42};
        Rectangle slower_button = {508, h - 66, 36, 42};
        Rectangle faster_button = {650, h - 66, 36, 42};
        Rectangle source_panel = {24, 94, (float)(w * 48 / 100) - 34, (float)h - 270};
        Rectangle stack_panel = {(float)(w * 48 / 100) + 16, 94, (float)(w - w * 48 / 100) - 40, (float)h - 270};
        Rectangle source_tab = {source_panel.x + 12, 104, 76, 30};
        Rectangle bytecode_tab = {source_panel.x + 94, 104, 100, 30};
        Rectangle flow_tab = {source_panel.x + 200, 104, 76, 30};
        Rectangle zoom_out = {source_panel.x + 284, 104, 34, 30};
        Rectangle zoom_in = {source_panel.x + 322, 104, 34, 30};
        Rectangle values_tab = {stack_panel.x + 12, 104, 94, 30};
        Rectangle memory_tab = {stack_panel.x + 112, 104, 100, 30};
        Rectangle mem_zoom_out = {stack_panel.x + 220, 104, 34, 30};
        Rectangle mem_zoom_in = {stack_panel.x + 258, 104, 34, 30};
        if (IsKeyPressed(KEY_F)) view_mode = view_mode == 2 ? 0 : 2;
        if (IsKeyPressed(KEY_C)) view_mode = view_mode == 1 ? 0 : 1;
        if (IsKeyPressed(KEY_M)) memory_view = !memory_view;
        if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) {
            if(memory_view) memory_zoom=fminf(2.5f,memory_zoom*1.15f);
            else flow_zoom=fminf(2.5f,flow_zoom*1.15f);
        }
        if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) {
            if(memory_view) memory_zoom=fmaxf(0.35f,memory_zoom/1.15f);
            else flow_zoom=fmaxf(0.35f,flow_zoom/1.15f);
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, source_tab)) view_mode = 0;
            if (CheckCollisionPointRec(mouse, bytecode_tab)) view_mode = 1;
            if (CheckCollisionPointRec(mouse, flow_tab)) view_mode = 2;
            if (CheckCollisionPointRec(mouse, values_tab)) memory_view = 0;
            if (CheckCollisionPointRec(mouse, memory_tab)) memory_view = 1;
            if (CheckCollisionPointRec(mouse, back))
                navigate_step(&trace_index, -1, trace.count, view_mode == 1, &bytecode_cursor,
                              bytecode_step_offsets, bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0,
                              &bytecode_animated_event, &bytecode_animation_direction, &bytecode_animation_progress);
            if (CheckCollisionPointRec(mouse, reset)) { trace_index = 0; bytecode_cursor = 0; bytecode_animation_direction = 0; bytecode_animation_progress = 1.0f; }
            if (CheckCollisionPointRec(mouse, next))
                navigate_step(&trace_index, 1, trace.count, view_mode == 1, &bytecode_cursor,
                              bytecode_step_offsets, bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0,
                              &bytecode_animated_event, &bytecode_animation_direction, &bytecode_animation_progress);
            if (CheckCollisionPointRec(mouse, play_button)) { playback = !playback; playback_accumulator = 0.0f; }
            if (CheckCollisionPointRec(mouse, slower_button)) steps_per_second = fmaxf(0.5f, steps_per_second - 0.5f);
            if (CheckCollisionPointRec(mouse, faster_button)) steps_per_second = fminf(60.0f, steps_per_second + 0.5f);
            if (view_mode == 2 && CheckCollisionPointRec(mouse, zoom_in)) flow_zoom = fminf(2.5f, flow_zoom * 1.15f);
            if (view_mode == 2 && CheckCollisionPointRec(mouse, zoom_out)) flow_zoom = fmaxf(0.35f, flow_zoom / 1.15f);
            panning = view_mode == 2 && CheckCollisionPointRec(mouse, source_panel) &&
                      !CheckCollisionPointRec(mouse, source_tab) && !CheckCollisionPointRec(mouse, bytecode_tab) && !CheckCollisionPointRec(mouse, flow_tab) &&
                      !CheckCollisionPointRec(mouse, zoom_in) && !CheckCollisionPointRec(mouse, zoom_out);
            drag_origin = mouse;
            memory_panning = memory_view && CheckCollisionPointRec(mouse, stack_panel) &&
                !CheckCollisionPointRec(mouse, values_tab) && !CheckCollisionPointRec(mouse, memory_tab) &&
                !CheckCollisionPointRec(mouse, mem_zoom_in) && !CheckCollisionPointRec(mouse, mem_zoom_out);
            memory_drag_origin = mouse;
            if(memory_view && CheckCollisionPointRec(mouse,mem_zoom_in)) memory_zoom=fminf(2.5f,memory_zoom*1.15f);
            if(memory_view && CheckCollisionPointRec(mouse,mem_zoom_out)) memory_zoom=fmaxf(0.35f,memory_zoom/1.15f);
        }
        if (view_mode != 1 && bytecode_step_offsets)
            bytecode_cursor = bytecode_step_offsets[trace_index];
        if (bytecode_animation_direction && bytecode_animation_progress < 1.0f) {
            bytecode_animation_progress = fminf(1.0f, bytecode_animation_progress + frame_time * 4.0f);
            if (bytecode_animation_progress >= 1.0f) bytecode_animation_direction = 0;
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && panning) {
            flow_following = 0;
            memory_following = 0;
            Vector2 mouse_now = GetMousePosition();
            flow_pan.x += mouse_now.x - drag_origin.x;
            flow_pan.y += mouse_now.y - drag_origin.y;
            drag_origin = mouse_now;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { panning = 0; flow_drag_node = NULL; memory_box_dragging = 0; memory_dragging_frame = 0; }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && memory_panning && !memory_box_dragging) {
            memory_following=0;
            Vector2 now=GetMousePosition();
            memory_pan.x+=now.x-memory_drag_origin.x;
            memory_pan.y+=now.y-memory_drag_origin.y;
            memory_drag_origin=now;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) memory_panning=0;

        float wheel = GetMouseWheelMove();
        Vector2 mouse = GetMousePosition();
        if (wheel != 0 && CheckCollisionPointRec(mouse, stack_panel) && memory_view) {
            float old_zoom=memory_zoom;
            memory_zoom=fmaxf(0.35f,fminf(2.5f,memory_zoom*powf(1.12f,wheel)));
            memory_pan.x=mouse.x-(mouse.x-memory_pan.x)*(memory_zoom/old_zoom);
            memory_pan.y=mouse.y-(mouse.y-memory_pan.y)*(memory_zoom/old_zoom);
        } else if (wheel != 0 && CheckCollisionPointRec(mouse, source_panel) && view_mode == 2) {
            float old_zoom = flow_zoom;
            flow_zoom = fmaxf(0.35f, fminf(2.5f, flow_zoom * powf(1.12f, wheel)));
            flow_pan.x = mouse.x - (mouse.x - flow_pan.x) * (flow_zoom / old_zoom);
            flow_pan.y = mouse.y - (mouse.y - flow_pan.y) * (flow_zoom / old_zoom);
        } else if (wheel != 0 && CheckCollisionPointRec(mouse, source_panel)) {
            if (view_mode == 1) bytecode_top -= (int)(wheel * 3.0f);
            else source_top -= (int)(wheel * 3.0f);
        } else if (wheel != 0 && CheckCollisionPointRec(mouse, stack_panel)) {
            memory_scroll -= wheel * 56.0f;
        }
        if (IsKeyPressed(KEY_UP)) { if (view_mode == 1) --bytecode_top; else --source_top; }
        if (IsKeyPressed(KEY_DOWN)) { if (view_mode == 1) ++bytecode_top; else ++source_top; }

        OrionTraceStep *step = trace.count ? &trace.steps[trace_index] : NULL;
        int current_line = step ? step->source_line : 1;
        int left_w = w * 48 / 100;
        int first_code_y = 140;
        int bottom_code_y = h - 190;
        int visible_source_lines = (bottom_code_y - first_code_y) / SOURCE_LINE_HEIGHT;
        if (visible_source_lines < 1) visible_source_lines = 1;
        SourceFile *listing_view = view_mode == 1 ? &bytecode : &source;
        int *listing_top = view_mode == 1 ? &bytecode_top : &source_top;
        int max_source_top = listing_view->count > (size_t)visible_source_lines ? (int)listing_view->count - visible_source_lines + 1 : 1;
        if (view_mode == 0 && trace_index != previous_trace) {
            if (current_line < source_top) source_top = current_line;
            if (current_line >= source_top + visible_source_lines) source_top = current_line - visible_source_lines + 1;
            previous_trace = trace_index;
        }
        if (*listing_top < 1) *listing_top = 1;
        if (*listing_top > max_source_top) *listing_top = max_source_top;

        BeginDrawing();
        ClearBackground(ORI_COLOR_BACKGROUND);
        ui_draw_text("ORION RIGEL", 30, 20, 28, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_fitted(argv[1], 30, 56, 17, (float)w - 60, ORI_COLOR_MUTED_TEXT);
        DrawRectangleRounded(source_panel, 0.04f, 8, ORI_COLOR_PANEL);
        DrawRectangleRounded(stack_panel, 0.04f, 8, ORI_COLOR_PANEL);
        DrawRectangleRounded(source_tab, 0.15f, 6, view_mode == 0 ? ORI_COLOR_TAB_ACTIVE : ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(bytecode_tab, 0.15f, 6, view_mode == 1 ? ORI_COLOR_TAB_ACTIVE : ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(flow_tab, 0.15f, 6, view_mode == 2 ? ORI_COLOR_TAB_ACTIVE : ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(values_tab, 0.15f, 6, !memory_view ? ORI_COLOR_TAB_ACTIVE : ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(memory_tab, 0.15f, 6, memory_view ? ORI_COLOR_TAB_ACTIVE : ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(mem_zoom_out, 0.15f, 6, ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(mem_zoom_in, 0.15f, 6, ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(zoom_out, 0.15f, 6, ORI_COLOR_TAB_INACTIVE);
        DrawRectangleRounded(zoom_in, 0.15f, 6, ORI_COLOR_TAB_INACTIVE);
        ui_draw_text("Source", (int)source_tab.x + 12, 110, 14, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("Bytecode", (int)bytecode_tab.x + 10, 110, 14, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("Flow [F]", (int)flow_tab.x + 7, 110, 14, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("Values", (int)values_tab.x + 18, 110, 14, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("Memory [M]", (int)memory_tab.x + 5, 110, 13, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("-", (int)zoom_out.x + 12, 108, 20, ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("+", (int)zoom_in.x + 10, 108, 20, ORI_COLOR_BRIGHT_TEXT);
        if (view_mode == 2) ui_draw_text(TextFormat("%d%%", (int)(flow_zoom * 100)), (int)zoom_in.x + 42, 111, 13, ORI_COLOR_ADDRESS);
        ui_draw_text("-",(int)mem_zoom_out.x+12,108,20,ORI_COLOR_BRIGHT_TEXT);
        ui_draw_text("+",(int)mem_zoom_in.x+10,108,20,ORI_COLOR_BRIGHT_TEXT);
        if(memory_view) ui_draw_text(TextFormat("%d%%",(int)(memory_zoom*100)),(int)mem_zoom_in.x+42,111,13,ORI_COLOR_ADDRESS);

        BeginScissorMode((int)source_panel.x + 8, first_code_y - 4, (int)source_panel.width - 16, bottom_code_y - first_code_y + 8);
        if (view_mode == 2) {
            const char *active_function = step && step->frame_count ? step->frames[step->frame_count - 1].function : "main";
            ASTNode *function = NULL;
            for (ASTNode *fn = program->extra; fn; fn = fn->next)
                if (strcmp(fn->opToken.lexeme, active_function) == 0) { function = fn; break; }
            char title[ORION_TRACE_NAME + 32];
            snprintf(title, sizeof(title), "Execution flow: %s", active_function);
            ui_draw_fitted(title, (int)source_panel.x + 16, 137, 18, source_panel.width - 32, ORI_COLOR_MAIN_TEXT);
            ui_font_scale = flow_zoom;
            ASTNode *body = function ? function->left : program->left;
            int layout_changed = body != cached_flow_body;
            if (layout_changed) {
                flow_layout_free(cached_flow_layout);
                flow_drag_node = NULL;
                cached_flow_body = body;
                cached_flow_layout = flow_layout_build(body, &source);
                flow_layout_place(cached_flow_layout,
                    source_panel.x + (source_panel.width - (cached_flow_layout ? cached_flow_layout->total_w : 0.0f)) / 2.0f,
                    218.0f, cached_flow_layout ? cached_flow_layout->total_w : 0.0f);
                flow_apply_offsets(cached_flow_layout);
                flow_layout_region(cached_flow_layout);
            }
            if (trace_index != previous_flow_trace || layout_changed) {
                FlowLayout *active_node = flow_layout_find_line(cached_flow_layout, current_line);
                if (active_node) {
                    float left = source_panel.x + 8.0f + 45.0f;
                    float right = source_panel.x + source_panel.width - 8.0f - 45.0f;
                    float top = first_code_y + 45.0f;
                    float bottom = bottom_code_y - 45.0f;
                    Rectangle screen = {active_node->x * flow_zoom + flow_pan.x,
                        active_node->y * flow_zoom + flow_pan.y,
                        active_node->w * flow_zoom, active_node->h * flow_zoom};
                    if (screen.x < left || screen.y < top ||
                        screen.x + screen.width > right || screen.y + screen.height > bottom) {
                        float cx = active_node->x + active_node->w / 2.0f;
                        float cy = active_node->y + active_node->h / 2.0f;
                        flow_follow_target = (Vector2){
                            (source_panel.x + source_panel.width / 2.0f) - cx * flow_zoom,
                            ((float)first_code_y + bottom_code_y) / 2.0f - cy * flow_zoom};
                        flow_following = 1;
                    }
                }
                previous_flow_trace = trace_index;
            }
            if (flow_following) {
                float alpha = 1.0f - expf(-8.0f * GetFrameTime());
                flow_pan.x += (flow_follow_target.x - flow_pan.x) * alpha;
                flow_pan.y += (flow_follow_target.y - flow_pan.y) * alpha;
                if (fabsf(flow_follow_target.x - flow_pan.x) < 0.5f &&
                    fabsf(flow_follow_target.y - flow_pan.y) < 0.5f) {
                    flow_pan = flow_follow_target;
                    flow_following = 0;
                }
            }
            Camera2D flow_camera = {0};
            flow_camera.offset = flow_pan;
            flow_camera.zoom = flow_zoom;
            BeginMode2D(flow_camera);
            Vector2 flow_mouse = GetMousePosition();
            Vector2 flow_world_mouse = {(flow_mouse.x - flow_pan.x) / flow_zoom,
                                        (flow_mouse.y - flow_pan.y) / flow_zoom};
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(flow_mouse, source_panel) &&
                !CheckCollisionPointRec(flow_mouse, source_tab) && !CheckCollisionPointRec(flow_mouse, bytecode_tab) && !CheckCollisionPointRec(flow_mouse, flow_tab) &&
                !CheckCollisionPointRec(flow_mouse, zoom_in) && !CheckCollisionPointRec(flow_mouse, zoom_out)) {
                flow_drag_node = flow_pick_node(cached_flow_layout, flow_world_mouse);
                if (flow_drag_node) { panning = 0; flow_following = 0; }
                flow_drag_last_mouse = flow_mouse;
            }
            if (flow_drag_node && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                Vector2 delta = {(flow_mouse.x - flow_drag_last_mouse.x) / flow_zoom,
                                 (flow_mouse.y - flow_drag_last_mouse.y) / flow_zoom};
                flow_add_offset(flow_drag_node->ast, delta);
                flow_drag_node->x += delta.x; flow_drag_node->y += delta.y;
                flow_drag_node->body_x += delta.x; flow_drag_node->body_y += delta.y;
                flow_drag_node->else_x += delta.x; flow_drag_node->else_y += delta.y;
                flow_drag_last_mouse = flow_mouse;
                flow_layout_region(cached_flow_layout);
            }
            int path_line = -1;
            for (size_t i = (size_t)trace_index + 1; i < trace.count; ++i) {
                if (trace.steps[i].source_line != current_line && line_in_subtree(body, trace.steps[i].source_line)) {
                    path_line = trace.steps[i].source_line;
                    break;
                }
            }
            if (cached_flow_layout) {
                FlowLayout *entry = cached_flow_layout;
                Rectangle start = {entry->x + entry->w/2 - 36, 174, 72, 30};
                DrawEllipse((int)(start.x + start.width/2), (int)(start.y + start.height/2),
                            start.width/2, start.height/2, ORI_COLOR_START_FILL);
                DrawEllipseLines((int)(start.x + start.width/2), (int)(start.y + start.height/2),
                                 start.width/2, start.height/2, ORI_COLOR_START_OUTLINE);
                ui_draw_text("Start", (int)start.x + 16, (int)start.y + 7, 13, ORI_COLOR_BRIGHT_TEXT);
                Vector2 start_path[] = {{entry->x + entry->w/2, start.y + start.height},
                                        {entry->x + entry->w/2, entry->y}};
                draw_flow_layout_nodes(cached_flow_layout, &source, current_line);
                draw_flow_layout_edges(cached_flow_layout, path_line);
                draw_flow_path(start_path, 2, current_line == entry->ast->opToken.line);
            }
            EndMode2D();
            ui_font_scale = 1.0f;
        } else if (view_mode == 1) {
            draw_bytecode_queue(source_panel, bytecode_instructions, bytecode_step_offsets,
                                bytecode_event_indexes, trace.count, bytecode_cursor,
                                bytecode_animated_event, bytecode_animation_direction,
                                bytecode_animation_progress);
        } else {
            for (int row = 0; row < visible_source_lines; ++row) {
                int line = *listing_top + row;
                if ((size_t)line > listing_view->count) break;
                int y = first_code_y + row * SOURCE_LINE_HEIGHT;
                if (view_mode == 0 && line == current_line) DrawRectangle((int)source_panel.x + 12, y - 2, (int)source_panel.width - 24, SOURCE_LINE_HEIGHT, ORI_COLOR_LINE_CURRENT);
                char numbered[16]; snprintf(numbered, sizeof(numbered), "%3d", line);
                Color source_ink=line==current_line?ORI_COLOR_LINE_NUMBER_ACTIVE:ORI_COLOR_MUTED_TEXT;
                Color code_ink=line==current_line?ORI_COLOR_LINE_TEXT_ACTIVE:ORI_COLOR_TEXT;
                ui_draw_text(numbered, (int)source_panel.x + 18, y, SOURCE_TEXT_SIZE, source_ink);
                ui_draw_fitted(listing_view->lines[line - 1], (int)source_panel.x + 68, y, SOURCE_TEXT_SIZE, source_panel.width - 84, code_ink);
            }
        }
        EndScissorMode();
        if (!listing_view->count) ui_draw_text(view_mode == 1 ? "(no bytecode listing)" : "(empty source file)", (int)source_panel.x + 44, first_code_y, 17, ORI_COLOR_MUTED_TEXT);

        char step_label[160];
        if (view_mode == 1) {
            size_t total_bytecode_events = bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0;
            if (bytecode_cursor < total_bytecode_events)
                snprintf(step_label, sizeof(step_label), "NEXT INSTRUCTION %zu / %zu   |   SOURCE STEP %d / %zu", bytecode_cursor + 1, total_bytecode_events, trace_index + 1, trace.count);
            else snprintf(step_label, sizeof(step_label), "INSTRUCTION QUEUE COMPLETE   |   SOURCE STEP %d / %zu", trace_index + 1, trace.count);
        }
        else if (step) snprintf(step_label, sizeof(step_label), "STEP %d / %zu   |   LINE %d", trace_index + 1, trace.count, step->source_line);
        else snprintf(step_label, sizeof(step_label), "No executable statements were captured.");
        ui_draw_text(step_label, 42, h - 157, 18, ORI_COLOR_STEP_TEXT);
        if (step) ui_draw_fitted(TextFormat("Statement: %s", step->instruction), 42, h - 130, 20, source_panel.width - 34, ORI_COLOR_INSTRUCTION_TEXT);

        BeginScissorMode((int)stack_panel.x + 4, 136, (int)stack_panel.width - 8, (int)stack_panel.height - 48);
        int stack_top = 140;
        float content_height = 0;
        if (memory_view) {
            OrionTraceStep *prior=trace_index>0?&trace.steps[trace_index-1]:NULL;
            Rectangle graph_panel={stack_panel.x+8.0f,142.0f,stack_panel.width-16.0f,stack_panel.height-54.0f};
            if(previous_memory_trace!=trace_index) {
                uint64_t focus=memory_focus_address(step,prior);
                if(focus) {
                    Vector2 p=memory_focus_position(step,focus,graph_panel);
                    Vector2 size=memory_focus_size(step,focus);
                    Rectangle on_screen={memory_pan.x+(p.x-size.x*0.5f)*memory_zoom,
                        memory_pan.y+(p.y-size.y*0.5f)*memory_zoom,
                        size.x*memory_zoom,size.y*memory_zoom};
                    float visible_width=fmaxf(0.0f,fminf(on_screen.x+on_screen.width,graph_panel.x+graph_panel.width)-
                                                       fmaxf(on_screen.x,graph_panel.x));
                    float visible_height=fmaxf(0.0f,fminf(on_screen.y+on_screen.height,graph_panel.y+graph_panel.height)-
                                                        fmaxf(on_screen.y,graph_panel.y));
                    float area=fmaxf(1.0f,on_screen.width*on_screen.height);
                    float visible_fraction=(visible_width*visible_height)/area;
                    if(visible_fraction<0.70f) {
                        memory_follow_target=(Vector2){graph_panel.x+graph_panel.width/2.0f-p.x*memory_zoom,
                            graph_panel.y+graph_panel.height/2.0f-p.y*memory_zoom};
                        memory_following=1;
                    } else memory_following=0;
                }
                previous_memory_trace=trace_index;
            }
            if(memory_box_dragging) memory_following=0;
            if(memory_following) {
                float alpha=1.0f-expf(-7.0f*GetFrameTime());
                memory_pan.x+=(memory_follow_target.x-memory_pan.x)*alpha;
                memory_pan.y+=(memory_follow_target.y-memory_pan.y)*alpha;
                if(fabsf(memory_follow_target.x-memory_pan.x)<0.5f && fabsf(memory_follow_target.y-memory_pan.y)<0.5f) {
                    memory_pan=memory_follow_target; memory_following=0;
                }
            }
            Camera2D camera={0}; camera.offset=memory_pan; camera.zoom=memory_zoom;
            BeginMode2D(camera);
            ui_font_scale=memory_zoom;
            draw_memory_view(&trace,(size_t)trace_index,step,prior,graph_panel,memory_pan,memory_zoom);
            EndMode2D();
            ui_font_scale=1.0f;
        } else if (step) {
            for (size_t i = 0; i < step->frame_count; ++i) {
                OrionTraceFrame *frame = &step->frames[i];
                int rows = (int)((frame->var_count + 1) / 2);
                int card_h = 45 + rows * 52;
                DrawRectangleRounded((Rectangle){(float)left_w + 32, (float)stack_top - memory_scroll, stack_panel.width - 62, (float)card_h}, 0.06f, 8, i + 1 == step->frame_count ? ORI_COLOR_FRAME_ACTIVE : ORI_COLOR_FRAME_INACTIVE);
                DrawRectangleRoundedLines((Rectangle){(float)left_w + 32, (float)stack_top - memory_scroll, stack_panel.width - 62, (float)card_h}, 0.06f, 8, i + 1 == step->frame_count ? ORI_COLOR_FRAME_ACTIVE_BORDER : ORI_COLOR_FRAME_INACTIVE_BORDER);
                ui_draw_text(frame->function, left_w + 48, stack_top + 7 - (int)memory_scroll, 19, ORI_COLOR_BRIGHT_TEXT);
                for (size_t v = 0; v < frame->var_count; ++v) {
                    int col = (int)(v % 2), row = (int)(v / 2);
                    int chip_x = left_w + 46 + col * 254;
                    int chip_y = stack_top + 34 + row * 52;
                    Rectangle chip = {(float)chip_x, (float)chip_y - memory_scroll, 244, 44};
                    int changed = trace_index > 0 && variable_changed(&trace.steps[trace_index - 1], &frame->vars[v]);
                    int read = frame->vars[v].read;
                    Color chip_fill = changed ? ORI_COLOR_CHANGED_FILL : (read ? ORI_COLOR_READ_FILL : ORI_COLOR_PANEL);
                    DrawRectangleRounded(chip, 0.11f, 6, chip_fill);
                    if (changed || read) DrawRectangleRoundedLines(chip, 0.11f, 6, changed ? ORI_COLOR_CHANGED_BORDER : ORI_COLOR_READ_BORDER);
                    if (changed && read) DrawRectangleRec((Rectangle){chip.x, chip.y + 5, 4, chip.height - 10}, ORI_COLOR_READ_CHANGED_STRIPE);
                    char address[32]; snprintf(address, sizeof(address), "@ 0x%08" PRIx64, frame->vars[v].address);
                    Vector2 address_size = ui_measure_text(address, 13, 0.25f);
                    int address_x = chip_x + 236 - (int)address_size.x;
                    char variable_label[ORION_TRACE_NAME * 2];
                    snprintf(variable_label, sizeof(variable_label), "%s: %s", frame->vars[v].name, frame->vars[v].type[0] ? frame->vars[v].type : "?");
                    ui_draw_fitted(variable_label, chip_x + 8, chip_y + 3 - (int)memory_scroll, 17, address_x - chip_x - 14, ORI_COLOR_MAIN_TEXT);
                    ui_draw_text(address, address_x, chip_y + 3 - (int)memory_scroll, 13, ORI_COLOR_ADDRESS);
                    ui_draw_fitted(frame->vars[v].pointer_target ? TextFormat("-> %s", frame->vars[v].value) : frame->vars[v].value,
                                   chip_x + 8, chip_y + 23 - (int)memory_scroll, 16, 224,
                                   frame->vars[v].pointer_target ? ORI_COLOR_POINTER_TEXT : ORI_COLOR_BRIGHT_TEXT);
                }
                stack_top += card_h + 10;
            }
            if (step->object_count) {
                ui_draw_text("HEAP OBJECTS", left_w + 44, stack_top + 2 - (int)memory_scroll, 16, ORI_COLOR_STEP_TEXT);
                stack_top += 26;
                for (size_t o = 0; o < step->object_count; ++o) {
                    OrionTraceObject *object = &step->objects[o];
                    int array_cells = (int)object->field_count + (object->has_more ? 1 : 0);
                    int array_columns = (int)((stack_panel.width - 112.0f + 6.0f) / 58.0f);
                    if (array_columns < 1) array_columns = 1;
                    int array_rows = array_cells ? (array_cells + array_columns - 1) / array_columns : 0;
                    int card_h = object->is_array ? (array_rows ? 36 + array_rows * 58 : 42) : 34 + (int)object->field_count * 27;
                    Rectangle object_box = {(float)left_w + 46, (float)stack_top - memory_scroll, stack_panel.width - 88, (float)card_h};
                    const OrionTraceObject *old_object = trace_index > 0 ? find_previous_object(&trace.steps[trace_index - 1], object->address) : NULL;
                    int allocation_changed = trace_index > 0 && (!old_object || old_object->freed != object->freed);
                    DrawRectangleRounded(object_box, 0.06f, 8, allocation_changed ? ORI_COLOR_CHANGED_FILL : (object->freed ? ORI_COLOR_FREED_BOX : ORI_COLOR_HEAP_BOX));
                    DrawRectangleRoundedLines(object_box, 0.06f, 8, allocation_changed ? ORI_COLOR_CHANGED_BORDER : (object->freed ? ORI_COLOR_FREED_BORDER : ORI_COLOR_BOX_OUTLINE));
                    char object_label[ORION_TRACE_NAME + 16];
                    snprintf(object_label, sizeof(object_label), "%s%s", object->type, object->freed ? "  (freed)" : "");
                    char object_address[32]; snprintf(object_address, sizeof(object_address), "@ 0x%08" PRIx64, object->address);
                    Vector2 object_address_size = ui_measure_text(object_address, 13, 0.25f);
                    int object_address_x = left_w + (int)stack_panel.width - 48 - (int)object_address_size.x;
                    ui_draw_fitted(object_label, left_w + 58, stack_top + 5 - (int)memory_scroll, 16, object_address_x - (left_w + 70), ORI_COLOR_BRIGHT_TEXT);
                    ui_draw_text(object_address, object_address_x, stack_top + 5 - (int)memory_scroll, 13, ORI_COLOR_ADDRESS);
                    if (object->is_array) {
                        int cell_x = left_w + 58;
                        int cell_y = stack_top + 26;
                        for (size_t f = 0; f < object->field_count + (object->has_more ? 1u : 0u); ++f) {
                            int col = (int)(f % (size_t)array_columns);
                            int row = (int)(f / (size_t)array_columns);
                            int x = cell_x + col * 58;
                            int y = cell_y + row * 58;
                            int is_ellipsis = f == object->field_count;
                            int changed = !is_ellipsis && trace_index > 0 && field_changed(&trace.steps[trace_index - 1], object->address, &object->fields[f]);
                            Rectangle cell = {(float)x, (float)y - memory_scroll, 52, 52};
                            int read = !is_ellipsis && object->fields[f].read;
                            Color cell_fill = changed ? ORI_COLOR_CHANGED_FILL : (read ? ORI_COLOR_READ_FILL : ORI_COLOR_PANEL);
                            DrawRectangleRounded(cell, 0.08f, 5, cell_fill);
                            if (changed || read) DrawRectangleRoundedLines(cell, 0.08f, 5, changed ? ORI_COLOR_CHANGED_BORDER : ORI_COLOR_READ_BORDER);
                            if (changed && read) DrawRectangleRec((Rectangle){cell.x, cell.y + 5, 4, cell.height - 10}, ORI_COLOR_READ_CHANGED_STRIPE);
                            if (is_ellipsis) {
                                ui_draw_text("...", x + 14, y + 18 - (int)memory_scroll, 16, ORI_COLOR_ADDRESS);
                            } else {
                                ui_draw_text(object->fields[f].name, x + 4, y + 3 - (int)memory_scroll, 12, ORI_COLOR_ADDRESS);
                                const char *cell_value = object->fields[f].pointer_target ? TextFormat("->%08" PRIx64, object->fields[f].pointer_target) : object->fields[f].value;
                                ui_draw_fitted(cell_value, x + 3, y + 25 - (int)memory_scroll, 13, 46,
                                               object->fields[f].pointer_target ? ORI_COLOR_POINTER_TEXT : ORI_COLOR_BRIGHT_TEXT);
                            }
                        }
                    } else {
                        for (size_t f = 0; f < object->field_count; ++f) {
                            int fy = stack_top + 29 + (int)f * 27;
                            int changed = trace_index > 0 && field_changed(&trace.steps[trace_index - 1], object->address, &object->fields[f]);
                            Rectangle field_box = {(float)left_w + 58, (float)fy - memory_scroll, stack_panel.width - 112, 23};
                            int read = object->fields[f].read;
                            Color field_fill = changed ? ORI_COLOR_CHANGED_FILL : (read ? ORI_COLOR_READ_FILL : ORI_COLOR_PANEL);
                            DrawRectangleRounded(field_box, 0.08f, 5, field_fill);
                            if (changed || read) DrawRectangleRoundedLines(field_box, 0.08f, 5, changed ? ORI_COLOR_CHANGED_BORDER : ORI_COLOR_READ_BORDER);
                            if (changed && read) DrawRectangleRec((Rectangle){field_box.x, field_box.y + 3, 4, field_box.height - 6}, ORI_COLOR_READ_CHANGED_STRIPE);
                            const char *value = object->fields[f].pointer_target
                                ? TextFormat("-> %s", object->fields[f].value)
                                : object->fields[f].value;
                            float value_limit = stack_panel.width - 150;
                            Vector2 value_size = ui_measure_text(value, 15, 0.25f);
                            if (value_size.x > value_limit) value_size.x = value_limit;
                            int value_x = left_w + (int)stack_panel.width - 62 - (int)value_size.x;
                            char field_label[ORION_TRACE_NAME * 2];
                            snprintf(field_label, sizeof(field_label), "%s: %s", object->fields[f].name, object->fields[f].type[0] ? object->fields[f].type : "?");
                            ui_draw_fitted(field_label, left_w + 65, fy + 2 - (int)memory_scroll, 15,
                                           value_x - (left_w + 76), ORI_COLOR_MAIN_TEXT);
                            ui_draw_fitted(value, value_x, fy + 2 - (int)memory_scroll, 15,
                                           value_limit, object->fields[f].pointer_target ? ORI_COLOR_POINTER_TEXT : ORI_COLOR_BRIGHT_TEXT);
                        }
                    }
                    stack_top += card_h + 10;
                }
            }
        }
        if(!memory_view) {
            content_height = (float)(stack_top - 140);
            float viewport_height = stack_panel.height - 42;
            float max_memory_scroll = content_height > viewport_height ? content_height - viewport_height : 0;
            if (memory_scroll < 0) memory_scroll = 0;
            if (memory_scroll > max_memory_scroll) memory_scroll = max_memory_scroll;
        }
        EndScissorMode();
        size_t total_bytecode_events = bytecode_step_offsets ? bytecode_step_offsets[trace.count] : 0;
        draw_button(back, "Back  [B]", view_mode == 1 ? bytecode_cursor > 0 : trace_index > 0);
        draw_button(reset, "Reset  [R]", 1);
        draw_button(next, "Next  [N]", view_mode == 1 ? bytecode_cursor < total_bytecode_events : (size_t)(trace_index + 1) < trace.count);
        draw_button(play_button, playback ? "Pause  [P]" : "Play  [P]", 1);
        draw_button(slower_button, "-", steps_per_second > 0.5f);
        draw_button(faster_button, "+", steps_per_second < 60.0f);
        ui_draw_text(TextFormat("%.1f steps/s", steps_per_second), 550, h - 54, 15, ORI_COLOR_TEXT);
        ui_draw_text("GOLD: changed   BLUE: read", 42, h - 100, 14, ORI_COLOR_MUTED_TEXT);
        ui_draw_fitted("Hold N/B: repeat   P: play/pause   [ ]: speed   C: bytecode   F: flow   M: memory", 330, h - 100, 13, w - 360, ORI_COLOR_FAINT_TEXT);
        EndDrawing();
    }

    for (size_t i = 0; i < sizeof(ui_fonts) / sizeof(ui_fonts[0]); ++i)
        if (ui_fonts[i].loaded) UnloadFont(ui_fonts[i].font);
    CloseWindow();
    flow_layout_free(cached_flow_layout);
    orion_trace_free(&trace);
    free(bytecode_instructions);
    free(bytecode_event_indexes);
    free(bytecode_step_offsets);
    free_source(&bytecode);
    free_source(&source);
    free(contents);
    free_token_stream(&tokens);
    return 0;
}
