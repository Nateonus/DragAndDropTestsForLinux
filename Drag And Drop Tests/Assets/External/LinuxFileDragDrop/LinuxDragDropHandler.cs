using System;
using System.Collections;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

public class LinuxDragDropHandler : MonoBehaviour
{
    private const int BufferSize = 4096;

    [DllImport("UnityDragDrop")]
    private static extern void InitOverlay();

    [DllImport("UnityDragDrop")]
    private static extern void PumpOverlay();

    [DllImport("UnityDragDrop")]
    private static extern int PollDroppedFile(StringBuilder buffer, int bufferSize);

    [DllImport("UnityDragDrop")]
    private static extern void SetOverlayBounds(int x, int y, int width, int height);

    [DllImport("UnityDragDrop")]
    private static extern void RegisterUnityWindow();

    [DllImport("UnityDragDrop")]
    private static extern void SetOverlayInputEnabled(int enabled);

    private int lastWidth;
    private int lastHeight;
    private int lastX;
    private int lastY;

    private bool isUnityWindowRegistered;

    private readonly StringBuilder _pathBuffer = new StringBuilder(BufferSize);
    private bool isFocused;

    private IEnumerator Start()
    {
        Application.runInBackground = true;

        InitOverlay();
        PumpOverlay();

        yield return null;
        yield return null;

        SyncOverlay();

        // Wait a couple frames for Unity's native window/focus to settle.
        RegisterUnityWindow();

        isUnityWindowRegistered = true;
    }

    private void OnApplicationFocus(bool focus)
    {
        isFocused = focus;  
        if (focus)
        {
            PumpOverlay();

            RegisterUnityWindow();

            lastX = int.MinValue;
            lastY = int.MinValue;
            lastWidth = int.MinValue;
            lastHeight = int.MinValue;

            SyncOverlay();

            SetOverlayInputEnabled(0);
        }
        else
        {
            PumpOverlay();
            //SyncOverlay();

            SetOverlayInputEnabled(1);
        }
    }

    private void Update()
    {
        PumpOverlay();

        if (!isUnityWindowRegistered)
            return;

        if (isFocused)
        {
            SyncOverlay();
        }

        while (true)
        {
            _pathBuffer.Clear();
            _pathBuffer.EnsureCapacity(BufferSize); 

            int result = PollDroppedFile(_pathBuffer, BufferSize);

            if (result == 0)
                break;

            if (result < 0)
            {
                Debug.LogWarning("Failed to poll dropped file from native plugin.");
                break;
            }

            string path = _pathBuffer.ToString();
            Debug.Log("Dropped file: " + path);
        }
    }

    private void SyncOverlay()
    {
        Vector2Int pos = Screen.mainWindowPosition;

        int x = pos.x + 16;
        int y = pos.y + 60;
        int width = Screen.width - 16;
        int height = Screen.height - 60;

        if (x == lastX &&
            y == lastY &&
            width == lastWidth &&
            height == lastHeight)
        {
            return;
        }

        lastX = x;
        lastY = y;
        lastWidth = width;
        lastHeight = height;

        SetOverlayBounds(x, y, width, height);
    }

    /*

    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    delegate void DropCallback(IntPtr path);

    [DllImport("DragDrop")]
    static extern void InitDragDrop();

    [DllImport("DragDrop")]
    static extern void PumpDragDrop();

    [DllImport("DragDrop")]
    static extern void RegisterDropCallback(DropCallback cb);

    void Start()
    {
        DropCallback callback = OnFileDropped;

        InitDragDrop();
        RegisterDropCallback(callback);
    }

    void Update()
    {
        PumpDragDrop();
    }

    static void OnFileDropped(IntPtr pathPtr)
    {
        string path = Marshal.PtrToStringAnsi(pathPtr);

        Debug.Log("Dropped: " + path);
    }
    */
}
