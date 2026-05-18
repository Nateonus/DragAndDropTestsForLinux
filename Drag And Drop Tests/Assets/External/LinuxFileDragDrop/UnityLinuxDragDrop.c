#include <gtk/gtk.h>
#include <stdbool.h>
#include <gdk/gdkx.h>
#include <X11/extensions/shape.h>
#include <X11/Xlib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xatom.h>

#define MAX_DROPPED_FILES 256
#define MAX_PATH_LENGTH 4096

static char *g_droppedFiles[MAX_DROPPED_FILES];
static int g_dropReadIndex = 0;
static int g_dropWriteIndex = 0;

static Window g_unityWindow = 0;


static GtkWidget* g_overlayWindow = NULL;
static bool g_initialized = false;

void InitGTK()
{
    if (g_initialized)
        return;

    if (!gtk_init_check(0, NULL))
    {
        return;
    }

    g_initialized = true;
}

static void ApplyOverlayTransientAndRaise()
{
    if (g_overlayWindow == NULL || g_unityWindow == 0)
        return;

    GdkWindow *overlayGdkWindow = gtk_widget_get_window(g_overlayWindow);
    if (overlayGdkWindow == NULL)
        return;

    Display *display = GDK_WINDOW_XDISPLAY(overlayGdkWindow);
    Window overlayXid = GDK_WINDOW_XID(overlayGdkWindow);

    XSetTransientForHint(display, overlayXid, g_unityWindow);

    XRaiseWindow(display, overlayXid);
    XFlush(display);

    fprintf(stderr, "Overlay set transient and raised\n");
    fflush(stderr);
}

static int g_overlayWidth = 1;
static int g_overlayHeight = 1;

static void SetOverlayInputFull()
{
    if (g_overlayWindow == NULL)
        return;

    GdkWindow *gdkWindow = gtk_widget_get_window(g_overlayWindow);
    if (gdkWindow == NULL)
        return;

    Display *display = GDK_WINDOW_XDISPLAY(gdkWindow);
    Window xid = GDK_WINDOW_XID(gdkWindow);

    XShapeCombineMask(
        display,
        xid,
        ShapeInput,
        0,
        0,
        None,
        ShapeSet);

    GdkWindow *gdkWindow = gtk_widget_get_window(g_overlayWindow);
    Display *display = GDK_WINDOW_XDISPLAY(gdkWindow);
    Window xid = GDK_WINDOW_XID(gdkWindow);

    Window overlayXid = GDK_WINDOW_XID(gtk_widget_get_window(g_overlayWindow));
    XSetTransientForHint(display, overlayXid, g_unityWindow);

    XRaiseWindow(display, overlayXid);

    XFlush(display);
}


static void SetOverlayInputNone()
{
    if (g_overlayWindow == NULL)
        return;

    GdkWindow *gdkWindow = gtk_widget_get_window(g_overlayWindow);
    if (gdkWindow == NULL)
        return;

    Display *display = GDK_WINDOW_XDISPLAY(gdkWindow);
    Window xid = GDK_WINDOW_XID(gdkWindow);

    XShapeCombineRectangles(
        display,
        xid,
        ShapeInput,
        0,
        0,
        NULL,
        0,
        ShapeSet,
        YXBanded);

    XFlush(display);
}

__attribute__((visibility("default"))) void SetOverlayInputEnabled(int enabled)
{
    if (enabled)
        SetOverlayInputFull();
    else
        SetOverlayInputNone();
}

static Window GetActiveWindow(Display *display)
{
    Atom prop = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);

    if (prop == None)
        return 0;

    Window root = DefaultRootWindow(display);

    Atom actualType;
    int actualFormat;
    unsigned long itemCount;
    unsigned long bytesAfter;
    unsigned char *data = NULL;

    int result = XGetWindowProperty(
        display,
        root,
        prop,
        0,
        1,
        False,
        AnyPropertyType,
        &actualType,
        &actualFormat,
        &itemCount,
        &bytesAfter,
        &data);

    if (result != Success || data == NULL)
        return 0;

    Window activeWindow = *(Window *)data;

    XFree(data);

    return activeWindow;
}

static Window GetFocusedWindow(Display *display)
{
    Window focusedWindow = 0;
    int revertTo = 0;

    XGetInputFocus(display, &focusedWindow, &revertTo);

    if (focusedWindow == None || focusedWindow == PointerRoot)
        return 0;

    return focusedWindow;
}

static Window GetTopLevelWindow(Display *display, Window window)
{
    Window root;
    Window parent;
    Window *children;
    unsigned int childCount;

    Window current = window;

    while (XQueryTree(display, current, &root, &parent, &children, &childCount))
    {
        if (children)
            XFree(children);

        if (parent == root || parent == 0)
            return current;

        current = parent;
    }

    return window;
}

__attribute__((visibility("default"))) void RegisterUnityWindow()
{
    fprintf(stderr, "Attempting to register Unity window\n");
    if (g_overlayWindow == NULL)
    {
        fprintf(stderr, "g_overlayWindow is null.\n");
        return;
    }
    
    GdkWindow *gdkWindow =
        gtk_widget_get_window(g_overlayWindow);

    if (gdkWindow == NULL) 
    {
        fprintf(stderr, "gdkWindow is null.\n");
        return;
    }

    Display *display =
        GDK_WINDOW_XDISPLAY(gdkWindow);

    Window activeWindow = GetActiveWindow(display);

    if (activeWindow == 0)
    {
        fprintf(stderr, "_NET_ACTIVE_WINDOW failed, trying XGetInputFocus\n");
        activeWindow = GetFocusedWindow(display);
    }

    fprintf(stderr, "Focused/active window candidate: %lu\n",
            (unsigned long)activeWindow);
    fflush(stderr);

    if (activeWindow == 0) {
        fprintf(stderr, "activeWindow is 0.\n");
        return;
    }

    g_unityWindow = GetTopLevelWindow(display, activeWindow);

    fprintf(stderr, "Registered Unity window: %lu\n",
            (unsigned long)g_unityWindow);
    
    gtk_widget_show_all(g_overlayWindow);
    XFlush(display);
    ApplyOverlayTransientAndRaise();
    
}

static int QueueIsFull()
{
    return ((g_dropWriteIndex + 1) % MAX_DROPPED_FILES) == g_dropReadIndex;
}

static int QueueIsEmpty()
{
    return g_dropReadIndex == g_dropWriteIndex;
}

static void QueueDroppedFile(const char *path)
{
    if (path == NULL)
        return;

    if (QueueIsFull())
    {
        fprintf(stderr, "Drop queue full, ignoring: %s\n", path);
        fflush(stderr);
        return;
    }

    g_droppedFiles[g_dropWriteIndex] = strdup(path);
    g_dropWriteIndex = (g_dropWriteIndex + 1) % MAX_DROPPED_FILES;
}

__attribute__((visibility("default"))) int PollDroppedFile(char *buffer, int bufferSize)
{
    if (buffer == NULL || bufferSize <= 0)
        return -1;

    if (QueueIsEmpty())
        return 0;

    char *path = g_droppedFiles[g_dropReadIndex];

    if (path == NULL)
    {
        g_dropReadIndex = (g_dropReadIndex + 1) % MAX_DROPPED_FILES;
        return 0;
    }

    int pathLength = strlen(path);

    if (pathLength + 1 > bufferSize)
    {
        return -1;
    }

    strcpy(buffer, path);

    free(path);
    g_droppedFiles[g_dropReadIndex] = NULL;

    g_dropReadIndex = (g_dropReadIndex + 1) % MAX_DROPPED_FILES;

    return 1;
}

static void OnDragDataReceived(
    GtkWidget *widget,
    GdkDragContext *context,
    gint x,
    gint y,
    GtkSelectionData *data,
    guint info,
    guint time,
    gpointer user_data)
{
    gchar **uris = gtk_selection_data_get_uris(data);

    if (uris == NULL)
    {
        g_print("Drop received, but no URIs found\n");
        return;
    }

    for (int i = 0; uris[i] != NULL; i++)
    {
        gchar *path = g_filename_from_uri(uris[i], NULL, NULL);

        if (path != NULL)
        {
            fprintf(stderr, "Dropped file: %s\n", path);
            fflush(stderr);

            QueueDroppedFile(path);

            g_free(path);
        }
    }

    g_strfreev(uris);

    gtk_drag_finish(context, TRUE, FALSE, time);
    SetOverlayInputNone();
}


__attribute__((visibility("default"))) void SetOverlayBounds(int x, int y, int width, int height)
{
    if (g_overlayWindow == NULL)
        return;

    if (width <= 0 || height <= 0)
        return;

    gtk_window_move(GTK_WINDOW(g_overlayWindow), x, y);
    gtk_window_resize(GTK_WINDOW(g_overlayWindow), width, height);
    fprintf(stderr, "Resized window to : %d,%d %dx%d\n", x, y, width, height);
    
    g_overlayWidth = width;
    g_overlayHeight = height;

    while (gtk_events_pending())
        gtk_main_iteration();

    ApplyOverlayTransientAndRaise();
}

void CreateOverlayWindow()
{
    if (g_overlayWindow != NULL)
        return;

    g_overlayWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(g_overlayWindow), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(g_overlayWindow), FALSE);
    gtk_window_set_focus_on_map(GTK_WINDOW(g_overlayWindow), FALSE);
    gtk_widget_set_app_paintable(g_overlayWindow, TRUE);

    GdkScreen* screen = gtk_widget_get_screen(g_overlayWindow);

    GdkVisual* visual = gdk_screen_get_rgba_visual(screen);

    if (visual != NULL)
    {
        gtk_widget_set_visual(g_overlayWindow, visual);
    }

    gtk_widget_set_opacity(g_overlayWindow, 0.01); //May need to change to 0.01 if transparency is blocking events.

    gtk_window_set_default_size(GTK_WINDOW(g_overlayWindow), 1, 1);
    gtk_window_set_resizable(GTK_WINDOW(g_overlayWindow), FALSE);
    gtk_window_move(GTK_WINDOW(g_overlayWindow), 1, 1);

    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(g_overlayWindow), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(g_overlayWindow), TRUE);

    // This should connect the drag and drop functionality to the window.
    GtkTargetEntry targets[] = { {"text/uri-list", 0, 0} };
    gtk_drag_dest_set(g_overlayWindow, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
    g_signal_connect(
        g_overlayWindow,
        "drag-data-received",
        G_CALLBACK(OnDragDataReceived),
        NULL);


    gtk_widget_show_all(g_overlayWindow);


    GdkWindow *gdkWindow = gtk_widget_get_window(g_overlayWindow);
    Display *display = GDK_WINDOW_XDISPLAY(gdkWindow);
    Window xid = GDK_WINDOW_XID(gdkWindow);

    Window overlayXid = GDK_WINDOW_XID(gtk_widget_get_window(g_overlayWindow));
    XSetTransientForHint(display, overlayXid, g_unityWindow);
    
    XRaiseWindow(display, overlayXid);
    XFlush(display);

    //This should create an empty input region to stop interfering from clicks.
    /*
    XRectangle rect;
    XShapeCombineRectangles(
        display,
        xid,
        ShapeInput,
        0,
        0,
        &rect,
        0,
        ShapeSet,
        YXBanded);
    */

    /*
    Note from gpt:
    Important Nuance

Later, drag/drop MAY stop working if the input region is fully empty.

If that happens, we’ll switch strategies:

Strategy A

Temporarily restore input region during drag enter.

OR

Strategy B

Use a thin active input strip/window.

OR

Strategy C

Use XDND-specific event handling.

But for now:
you need clickthrough first.

    */

    g_print("GTK Window created for Drag'n'Drop Support\n");
    g_signal_connect(g_overlayWindow, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

}

void PumpGTK()
{
    while (gtk_events_pending())
    {
        gtk_main_iteration();
    }
}

__attribute__((visibility("default")))
void InitOverlay()
{
    InitGTK();

    if (!g_initialized)
        return;

    CreateOverlayWindow();
}

__attribute__((visibility("default")))
void PumpOverlay()
{
    PumpGTK();
}

/*
typedef void (*DropCallback)(const char* path);

void InitDragDrop();
void PumpDragDrop();
void RegisterDropCallback(DropCallback cb);
void ShutdownDragDrop();



void PumpDragDrop()
{
    while (gtk_events_pending())
        gtk_main_iteration();
}
*/