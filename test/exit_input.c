#include <X11/Xlib.h>

int exit_test_request_close(void *display, unsigned long window)
{
    Display *d = display;
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(d, "WM_PROTOCOLS", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = XInternAtom(d, "WM_DELETE_WINDOW", False);
    event.xclient.data.l[1] = CurrentTime;
    int sent = XSendEvent(d, window, False, NoEventMask, &event);
    XFlush(d);
    return sent;
}
