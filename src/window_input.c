#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Only the GLFW ABI used here; the installed raylib archive supplies it.
// No headers or build products from a Til development checkout are needed.
typedef struct GLFWwindow GLFWwindow;
typedef void (*EditorKeyCallback)(GLFWwindow *, int, int, int, int);
typedef void (*WindowCharModsCallback)(GLFWwindow *, unsigned int, int);
typedef void (*EditorCloseCallback)(GLFWwindow *);
extern GLFWwindow *glfwGetCurrentContext(void);
extern EditorKeyCallback glfwSetKeyCallback(GLFWwindow *, EditorKeyCallback);
extern WindowCharModsCallback glfwSetCharModsCallback(GLFWwindow *, WindowCharModsCallback);
extern EditorCloseCallback glfwSetWindowCloseCallback(GLFWwindow *, EditorCloseCallback);
extern void glfwSetWindowShouldClose(GLFWwindow *, int);

typedef struct { int key, mods, action; } EditorKeyEvent;
static EditorKeyCallback raylib_key_callback;
static WindowCharModsCallback previous_char_mods_callback;
static EditorCloseCallback previous_close_callback;
static EditorKeyEvent *events;
static size_t count, capacity, cursor;

static void editor_push_event(int key, int mods, int action)
{
    if (count == capacity) {
        size_t next_capacity = capacity ? capacity * 2 : 32;
        EditorKeyEvent *next = realloc(events, next_capacity * sizeof(*events));
        if (!next) { fputs("Tiled: keyboard queue allocation failed\n", stderr); abort(); }
        events = next;
        capacity = next_capacity;
    }
    events[count++] = (EditorKeyEvent){key, mods, action};
}

static void editor_key_callback(GLFWwindow *window, int key, int scancode,
                                int action, int mods)
{
    raylib_key_callback(window, key, scancode, action, mods);
    if (action == 0 || key <= 0) return;
    editor_push_event(key, mods, action);
}

static void window_char_callback(GLFWwindow *window, unsigned int codepoint, int mods)
{
    if (previous_char_mods_callback) previous_char_mods_callback(window, codepoint, mods);
    // Action 3 distinguishes text from GLFW press/repeat. Retaining callback
    // order keeps a prompt's opening key, typed text and Enter unambiguous.
    // Unlike the plain char callback, this also delivers Alt-modified text.
    // Raylib's original plain callback remains installed for its own queue.
    editor_push_event((int)codepoint, mods, 3);
}

static void editor_window_close_callback(GLFWwindow *window)
{
    // GLFW allows no close callback. Its close flag is set before this call;
    // defer that decision until earlier queued text has reached the buffers.
    if (previous_close_callback) previous_close_callback(window);
    glfwSetWindowShouldClose(window, 0);
    editor_push_event(1, 0, 4);
}

void window_init_input(void)
{
    GLFWwindow *window = glfwGetCurrentContext();
    if (!window) { fputs("Tiled: no GLFW window\n", stderr); abort(); }
    raylib_key_callback = glfwSetKeyCallback(window, editor_key_callback);
    if (!raylib_key_callback) { fputs("Tiled: no raylib key callback\n", stderr); abort(); }
    previous_char_mods_callback = glfwSetCharModsCallback(window, window_char_callback);
    previous_close_callback = glfwSetWindowCloseCallback(window, editor_window_close_callback);
}

int32_t window_next_event(int32_t *mods, int32_t *action)
{
    if (cursor == count) { cursor = count = 0; *mods = *action = 0; return 0; }
    EditorKeyEvent event = events[cursor++];
    *mods = event.mods;
    *action = event.action;
    return event.key;
}

void window_close_input(void)
{
    glfwSetKeyCallback(glfwGetCurrentContext(), raylib_key_callback);
    glfwSetCharModsCallback(glfwGetCurrentContext(), previous_char_mods_callback);
    glfwSetWindowCloseCallback(glfwGetCurrentContext(), previous_close_callback);
    free(events);
    events = NULL;
    cursor = count = capacity = 0;
}
