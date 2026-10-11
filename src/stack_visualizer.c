#include "raylib.h"
#include <stdio.h>
#include <string.h>

#define WINDOW_W 1280
#define WINDOW_H 800
#define MAX_FRAMES 4
#define MAX_VARS 3
#define TRACE_COUNT 9

typedef struct { const char *name; const char *value; } VariableView;
typedef struct {
    const char *function;
    const char *return_site;
    VariableView vars[MAX_VARS];
    int var_count;
} FrameView;
typedef struct {
    int source_line;
    const char *instruction;
    const char *explanation;
    FrameView frames[MAX_FRAMES];
    int depth;
} TraceStep;

static const char *source[] = {
    "fn main() -> int {",
    "    int n = 3;",
    "    int answer = factorial(n);",
    "    return answer;",
    "}",
    "fn factorial(int n) -> int {",
    "    if (n <= 1) return 1;",
    "    return n * factorial(n - 1);",
    "}"
};

static TraceStep trace[TRACE_COUNT];
static int trace_index = 0;

static FrameView frame(const char *name, const char *return_site,
                       const char *v0n, const char *v0v,
                       const char *v1n, const char *v1v) {
    FrameView f = {0};
    f.function = name; f.return_site = return_site;
    if (v0n) f.vars[f.var_count++] = (VariableView){v0n, v0v};
    if (v1n) f.vars[f.var_count++] = (VariableView){v1n, v1v};
    return f;
}

static void set_step(int i, int line, const char *instruction, const char *explanation,
                     int depth, FrameView a, FrameView b, FrameView c, FrameView d) {
    trace[i].source_line = line; trace[i].instruction = instruction;
    trace[i].explanation = explanation; trace[i].depth = depth;
    FrameView frames[] = {a, b, c, d};
    for (int j = 0; j < depth; ++j) trace[i].frames[j] = frames[j];
}

static void build_trace(void) {
    FrameView main0 = frame("main", "program entry", "n", "3", "answer", "0");
    FrameView fact3 = frame("factorial", "main: line 3", "n", "3", "result", "pending");
    FrameView fact2 = frame("factorial", "factorial: line 8", "n", "2", "result", "pending");
    FrameView fact1 = frame("factorial", "factorial: line 8", "n", "1", NULL, NULL);
    set_step(0, 1, "ENTER main", "Program starts. The main frame is created.", 1, main0, (FrameView){0}, (FrameView){0}, (FrameView){0});
    set_step(1, 3, "CALL factorial(3)", "A function call pushes a new frame above main.", 2, main0, fact3, (FrameView){0}, (FrameView){0});
    set_step(2, 7, "COMPARE n <= 1  → false", "n is 3, so factorial takes the recursive path.", 2, main0, fact3, (FrameView){0}, (FrameView){0});
    set_step(3, 8, "CALL factorial(2)", "The recursive call pushes another frame. The caller stays below it.", 3, main0, fact3, fact2, (FrameView){0});
    set_step(4, 8, "CALL factorial(1)", "A third factorial frame is pushed for the base case.", 4, main0, fact3, fact2, fact1);
    fact2 = frame("factorial", "factorial: line 8", "n", "2", "result", "2");
    set_step(5, 7, "RETURN 1", "The base case returns 1 and its frame is popped. factorial(2) computes 2 × 1.", 3, main0, fact3, fact2, (FrameView){0});
    fact3 = frame("factorial", "main: line 3", "n", "3", "result", "6");
    set_step(6, 8, "RETURN 2", "factorial(2) returns 2. factorial(3) computes 3 × 2.", 2, main0, fact3, (FrameView){0}, (FrameView){0});
    main0 = frame("main", "program entry", "n", "3", "answer", "6");
    set_step(7, 3, "RETURN 6", "factorial(3) returns 6 to main; its frame is popped.", 1, main0, (FrameView){0}, (FrameView){0}, (FrameView){0});
    set_step(8, 4, "RETURN 6", "main returns. The call stack is empty.", 0, (FrameView){0}, (FrameView){0}, (FrameView){0}, (FrameView){0});
}

static void draw_button(Rectangle r, const char *text, int enabled) {
    Color fill = enabled ? (Color){57, 76, 105, 255} : (Color){42, 48, 60, 255};
    Color border = enabled ? (Color){105, 139, 188, 255} : (Color){65, 70, 80, 255};
    DrawRectangleRounded(r, 0.18f, 8, fill);
    DrawRectangleRoundedLines(r, 0.18f, 8, border);
    int width = MeasureText(text, 18);
    DrawText(text, (int)(r.x + (r.width - width)/2), (int)(r.y + 12), 18, enabled ? RAYWHITE : GRAY);
}

int main(void) {
    build_trace();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(WINDOW_W, WINDOW_H, "OrionLang | Stack Stepper");
    SetWindowMinSize(900, 600);
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_SPACE)) { if (trace_index < TRACE_COUNT - 1) trace_index++; }
        if (IsKeyPressed(KEY_LEFT)) { if (trace_index > 0) trace_index--; }
        if (IsKeyPressed(KEY_R)) trace_index = 0;

        int w = GetScreenWidth(), h = GetScreenHeight();
        Rectangle back = {28, h - 66, 112, 42};
        Rectangle reset = {150, h - 66, 112, 42};
        Rectangle next = {272, h - 66, 112, 42};
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (CheckCollisionPointRec(mouse, back) && trace_index > 0) trace_index--;
            if (CheckCollisionPointRec(mouse, reset)) trace_index = 0;
            if (CheckCollisionPointRec(mouse, next) && trace_index < TRACE_COUNT - 1) trace_index++;
        }

        BeginDrawing();
        ClearBackground((Color){18, 22, 30, 255});
        DrawText("STACK STEPPER", 30, 22, 26, RAYWHITE);
        DrawText("A recursive call pushes a frame; returning pops it.", 30, 56, 17, (Color){165, 178, 198, 255});

        int left_w = w * 45 / 100;
        Rectangle code_panel = {24, 100, (float)left_w - 34, (float)h - 190};
        Rectangle stack_panel = {(float)left_w + 16, 100, (float)(w - left_w) - 40, (float)h - 190};
        DrawRectangleRounded(code_panel, 0.04f, 8, (Color){27, 33, 44, 255});
        DrawRectangleRounded(stack_panel, 0.04f, 8, (Color){27, 33, 44, 255});
        DrawText("SOURCE", 42, 116, 16, (Color){129, 165, 213, 255});
        DrawText("CALL STACK  ·  grows downward", left_w + 34, 116, 16, (Color){129, 165, 213, 255});

        for (int i = 0; i < 9; ++i) {
            int y = 153 + i * 34;
            if (trace[trace_index].source_line == i + 1) DrawRectangle(38, y - 4, left_w - 62, 28, (Color){73, 91, 120, 170});
            char numbered[16]; snprintf(numbered, sizeof(numbered), "%2d", i + 1);
            DrawText(numbered, 44, y, 17, (Color){108, 121, 141, 255});
            DrawText(source[i], 82, y, 17, (Color){222, 226, 233, 255});
        }

        TraceStep *step = &trace[trace_index];
        DrawText(TextFormat("STEP %d / %d", trace_index + 1, TRACE_COUNT), 44, h - 158, 16, (Color){129, 165, 213, 255});
        DrawText(step->instruction, 44, h - 132, 20, (Color){245, 203, 112, 255});
        DrawText(step->explanation, 44, h - 102, 16, (Color){190, 198, 210, 255});

        int card_y = 151;
        for (int i = 0; i < step->depth; ++i) {
            FrameView *f = &step->frames[i];
            int card_h = 112;
            Color frame_color = i == step->depth - 1 ? (Color){51, 74, 92, 255} : (Color){38, 47, 61, 255};
            DrawRectangleRounded((Rectangle){(float)left_w + 36, (float)card_y, (float)(w - left_w) - 80, (float)card_h}, 0.08f, 8, frame_color);
            DrawRectangleRoundedLines((Rectangle){(float)left_w + 36, (float)card_y, (float)(w - left_w) - 80, (float)card_h}, 0.08f, 8,
                                      i == step->depth - 1 ? (Color){112, 174, 202, 255} : (Color){73, 88, 108, 255});
            DrawText(f->function, left_w + 54, card_y + 14, 20, RAYWHITE);
            DrawText(TextFormat("return to %s", f->return_site), left_w + 54, card_y + 39, 14, (Color){151, 164, 184, 255});
            for (int j = 0; j < f->var_count; ++j) {
                int x = left_w + 54 + j * 178;
                DrawRectangleRounded((Rectangle){(float)x, (float)card_y + 66, 158, 32}, 0.16f, 6, (Color){27, 33, 44, 255});
                DrawText(TextFormat("%s = %s", f->vars[j].name, f->vars[j].value), x + 9, card_y + 74, 15, (Color){190, 220, 184, 255});
            }
            card_y += card_h + 12;
        }
        if (step->depth == 0) DrawText("No active frames", left_w + 52, 165, 20, (Color){164, 176, 195, 255});
        DrawTriangle((Vector2){(float)w - 34, (float)card_y - 8}, (Vector2){(float)w - 46, (float)card_y - 28}, (Vector2){(float)w - 22, (float)card_y - 28}, (Color){245, 203, 112, 255});
        DrawText("SP", w - 57, card_y + 1, 13, (Color){245, 203, 112, 255});

        draw_button(back, "Back  [←]", trace_index > 0);
        draw_button(reset, "Reset  [R]", 1);
        draw_button(next, "Next  [→]", trace_index < TRACE_COUNT - 1);
        DrawText("Space: next step", w - 210, h - 51, 14, (Color){151, 164, 184, 255});
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
