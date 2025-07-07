#include <windows.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define IDC_EDIT_KEY 101
#define IDC_EDIT_CPS 102
#define IDC_BTN_SAVE 103
#define IDC_STATIC_STATUS 104

#define HOLD_THRESHOLD_MS 300

// Variables globales
HWND hEditKey, hEditCPS, hStatus;
bool autoclick_activado = false;
bool hold_mode = false;
UINT cps = 10;
UINT tecla_objetivo = 0;  // VK code o XBUTTON1/XBUTTON2
HHOOK hKeyboardHook = NULL;
HHOOK hMouseHook = NULL;
HANDLE hThread = NULL;
CRITICAL_SECTION cs;

DWORD tecla_down_time = 0;

// Prototipos
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI AutoClickThread(LPVOID);
LRESULT CALLBACK LowLevelKeyboardProc(int, WPARAM, LPARAM);
LRESULT CALLBACK LowLevelMouseProc(int, WPARAM, LPARAM);

void ActualizarStatus(const char* texto)
{
    SetWindowText(hStatus, texto);
}

void ToggleAutoclick()
{
    EnterCriticalSection(&cs);
    autoclick_activado = !autoclick_activado;
    LeaveCriticalSection(&cs);

    ActualizarStatus(autoclick_activado ? "Autoclick ACTIVADO (toggle)" : "Autoclick DETENIDO (toggle)");
}

void ActivarAutoclick()
{
    EnterCriticalSection(&cs);
    if (!autoclick_activado)
    {
        autoclick_activado = true;
        ActualizarStatus("Autoclick ACTIVADO (hold)");
    }
    LeaveCriticalSection(&cs);
}

void DesactivarAutoclick()
{
    EnterCriticalSection(&cs);
    if (autoclick_activado)
    {
        autoclick_activado = false;
        ActualizarStatus("Autoclick DETENIDO (hold)");
    }
    LeaveCriticalSection(&cs);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT *kbd = (KBDLLHOOKSTRUCT*)lParam;

        if (kbd->vkCode == tecla_objetivo)
        {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
            {
                if (tecla_down_time == 0)
                {
                    tecla_down_time = GetTickCount();
                    hold_mode = true;
                    ActivarAutoclick(); // Modo hold: activa inmediatamente
                }
            }
            else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
            {
                DWORD tiempo_presionado = GetTickCount() - tecla_down_time;
                tecla_down_time = 0;

                if (hold_mode)
                {
                    hold_mode = false;
                    DesactivarAutoclick();  // Desactiva modo hold al soltar
                }
                else if (tiempo_presionado < HOLD_THRESHOLD_MS)
                {
                    // Pulsación rápida: toggle
                    ToggleAutoclick();
                }
            }
        }
    }
    return CallNextHookEx(hKeyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT*)lParam;
        if (wParam == WM_XBUTTONDOWN)
        {
            WORD button = HIWORD(ms->mouseData);
            if ((button == XBUTTON1 && tecla_objetivo == VK_XBUTTON1) ||
                (button == XBUTTON2 && tecla_objetivo == VK_XBUTTON2))
            {
                // Para mouse: solo toggle con click rápido (sin hold)
                ToggleAutoclick();
            }
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

DWORD WINAPI AutoClickThread(LPVOID param)
{
    INPUT input[2] = {0};

    input[0].type = INPUT_MOUSE;
    input[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

    input[1].type = INPUT_MOUSE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;

    while (true)
    {
        EnterCriticalSection(&cs);
        bool activo = autoclick_activado;
        UINT clicks = cps;
        LeaveCriticalSection(&cs);

        if (!activo)
        {
            Sleep(50);
            continue;
        }

        SendInput(2, input, sizeof(INPUT));
        Sleep(1000 / clicks);
    }
    return 0;
}

void InstalarHooks()
{
    hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
    if (!hKeyboardHook || !hMouseHook)
    {
        MessageBox(NULL, "Error instalando hooks globales.", "Error", MB_ICONERROR);
    }
}

void QuitarHooks()
{
    if (hKeyboardHook)
        UnhookWindowsHookEx(hKeyboardHook);
    if (hMouseHook)
        UnhookWindowsHookEx(hMouseHook);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    InitializeCriticalSection(&cs);

    WNDCLASS wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "AutoClickerClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    HWND hwnd = CreateWindow("AutoClickerClass", "Autoclicker con toggle y hold", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 200, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    InstalarHooks();

    DWORD tid;
    hThread = CreateThread(NULL, 0, AutoClickThread, NULL, 0, &tid);

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    autoclick_activado = false;
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    QuitarHooks();

    DeleteCriticalSection(&cs);

    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static UINT tecla_configurada = 0;

    switch(msg)
    {
        case WM_CREATE:
        {
            CreateWindow("STATIC", "Tecla para activar/desactivar (VK code o 'x1'/'x2'):", WS_VISIBLE | WS_CHILD, 20, 20, 350, 20, hwnd, NULL, NULL, NULL);
            hEditKey = CreateWindow("EDIT", "", WS_VISIBLE | WS_CHILD | WS_BORDER, 20, 45, 100, 25, hwnd, (HMENU)IDC_EDIT_KEY, NULL, NULL);

            CreateWindow("STATIC", "Clics por segundo:", WS_VISIBLE | WS_CHILD, 20, 80, 150, 20, hwnd, NULL, NULL, NULL);
            hEditCPS = CreateWindow("EDIT", "10", WS_VISIBLE | WS_CHILD | WS_BORDER, 20, 105, 100, 25, hwnd, (HMENU)IDC_EDIT_CPS, NULL, NULL);

            CreateWindow("BUTTON", "Guardar configuración", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON, 150, 45, 180, 30, hwnd, (HMENU)IDC_BTN_SAVE, NULL, NULL);

            hStatus = CreateWindow("STATIC", "Estado: esperando configuración", WS_VISIBLE | WS_CHILD, 20, 140, 350, 20, hwnd, (HMENU)IDC_STATIC_STATUS, NULL, NULL);
        }
        break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BTN_SAVE)
            {
                char tecla_texto[50];
                GetWindowText(hEditKey, tecla_texto, 50);

                if (strlen(tecla_texto) == 1)
                {
                    tecla_configurada = VkKeyScan(tecla_texto[0]) & 0xFF;
                }
                else
                {
                    if (_stricmp(tecla_texto, "x1") == 0) tecla_configurada = VK_XBUTTON1;
                    else if (_stricmp(tecla_texto, "x2") == 0) tecla_configurada = VK_XBUTTON2;
                    else tecla_configurada = 0;
                }

                char cps_text[20];
                GetWindowText(hEditCPS, cps_text, 20);
                int cps_val = atoi(cps_text);
                if (cps_val <= 0) cps_val = 10;

                cps = (UINT)cps_val;
                tecla_objetivo = tecla_configurada;

                if (tecla_objetivo != 0)
                {
                    ActualizarStatus("Configuración guardada. Presiona la tecla o botón para toggle o hold.");
                }
                else
                {
                    ActualizarStatus("Tecla inválida. Usa un caracter o 'x1'/'x2'.");
                }
            }
            break;

        case WM_DESTROY:
            autoclick_activado = false;
            hold_mode = false;
            if (hThread)
            {
                WaitForSingleObject(hThread, INFINITE);
                CloseHandle(hThread);
                hThread = NULL;
            }
            QuitarHooks();
            DeleteCriticalSection(&cs);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
