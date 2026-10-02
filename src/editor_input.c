#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Only the GLFW ABI used here; the installed raylib archive supplies it.
// No headers or build products from a Til development checkout are needed.
typedef struct GLFWwindow GLFWwindow;
typedef void (*EditorKeyCallback)(GLFWwindow *, int, int, int, int);
extern GLFWwindow *glfwGetCurrentContext(void);
extern EditorKeyCallback glfwSetKeyCallback(GLFWwindow *, EditorKeyCallback);

typedef struct { int key, mods, action; } EditorKeyEvent;
static EditorKeyCallback raylib_key_callback;
static EditorKeyEvent *events;
static size_t count, capacity, cursor;

static void editor_key_callback(GLFWwindow *window, int key, int scancode,
                                int action, int mods)
{
    raylib_key_callback(window, key, scancode, action, mods);
    if (action == 0 || key <= 0) return; // Skip GLFW_RELEASE and unknown keys.
    if (count == capacity) {
        size_t next_capacity = capacity ? capacity * 2 : 32;
        EditorKeyEvent *next = realloc(events, next_capacity * sizeof(*events));
        if (!next) { fputs("Tiled: keyboard queue allocation failed\n", stderr); abort(); }
        events = next;
        capacity = next_capacity;
    }
    events[count++] = (EditorKeyEvent){key, mods, action};
}

void editor_init_input(void)
{
    GLFWwindow *window = glfwGetCurrentContext();
    if (!window) { fputs("Tiled: no GLFW window\n", stderr); abort(); }
    raylib_key_callback = glfwSetKeyCallback(window, editor_key_callback);
    if (!raylib_key_callback) { fputs("Tiled: no raylib key callback\n", stderr); abort(); }
}

int32_t editor_next_key(int32_t *mods, int32_t *action)
{
    if (cursor == count) { cursor = count = 0; *mods = *action = 0; return 0; }
    EditorKeyEvent event = events[cursor++];
    *mods = event.mods;
    *action = event.action;
    return event.key;
}

void editor_close_input(void)
{
    glfwSetKeyCallback(glfwGetCurrentContext(), raylib_key_callback);
    free(events);
    events = NULL;
    cursor = count = capacity = 0;
}
