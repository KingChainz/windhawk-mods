// ==WindhawkMod==
// @id              dynamic-island-teleprompter
// @name            Dynamic Island Teleprompter
// @description     Minimalist Dynamic Island teleprompter with spring animations and interactive shortcuts panel
// @version         1.6.0
// @author          0xCh4inz
// @github          https://github.com/KingChainz
// @homepage        https://github.com/KingChainz/dynamic-island-teleprompter
// @include         explorer.exe
// @compilerOptions -ld2d1 -ldwrite -lole32 -lshcore -ldwmapi
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Dynamic Island Teleprompter

A minimalist, hardware-accelerated **Dynamic Island** teleprompter for Windows 10/11, powered by **Direct2D** and **DirectWrite**.

Positioned directly under your webcam, it allows you to read your script smoothly while maintaining natural eye contact during video meetings, presentations, and live recordings.

### Key Features
- **Spring Physics Animations:** Organic pill resizing based on a damped harmonic oscillator.
- **Eye-Line Guide:** Subtly marks the optimal eye contact height aligned with your webcam lens.
- **Click-Through Ghost Mode:** Press the yellow button or `Ctrl + Alt + G` to pass all mouse clicks directly through to background apps (`WS_EX_TRANSPARENT`).
- **Interactive Shortcuts Drawer:** Press `Ctrl + Alt + X` to reveal an interactive sub-island where all shortcut rows are clickable.
- **Mouse Wheel Speed Adjustment:** Change scrolling speed dynamically with the mouse wheel.
- **Right-Click Paste:** Instantly replace the teleprompter text with clipboard content.
- **Non-blocking Footprint:** Window physically shrinks to 220x62 px when collapsed, freeing screen space.

### Shortcuts
- `Ctrl + Alt + Space`: Play / Pause scrolling
- `Ctrl + Alt + R`: Reset text to the top
- `Ctrl + Alt + T`: Expand / Collapse island
- `Ctrl + Alt + G`: Toggle Ghost Mode (Click-through)
- `Ctrl + Alt + X`: Toggle Shortcuts Drawer
- `Double Click` (<= 300ms): Reset text to the top
- `Mouse Wheel`: Adjust reading speed
- `Right Click`: Paste text from clipboard
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- script_text: "¡Hola! Este es tu teleprónter estilo Dynamic Island.\n\nUbicado estratégicamente justo debajo de tu cámara web para mantener contacto visual natural en grabaciones y videollamadas.\n\nAtajos y controles:\n- Ctrl + Alt + Espacio: Pausar o reanudar el scroll.\n- Ctrl + Alt + T: Expandir o contraer la isla manualmente.\n- Ctrl + Alt + R: Reiniciar Texto al principio.\n- Ctrl + Alt + G: Modo Fantasma (clic a través hacia otras apps).\n- Ctrl + Alt + X: Ver panel de atajos.\n- Rueda del ratón: Regular velocidad de lectura."
  $name: Guion de lectura
- default_speed: 35
  $name: Velocidad de lectura (px/segundo)
- font_size: 21
  $name: Tamaño de fuente
- island_opacity: 92
  $name: Opacidad de la isla (%)
  $description: Nivel de transparencia de la isla (10 a 100)
- top_margin: 12
  $name: Distancia superior (px)
  $description: Separación desde el borde superior de la pantalla (12px es la altura estándar de devcode90)
*/
// ==/WindhawkModSettings==

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <string>
#include <algorithm>
#include <cmath>

// --- MOTOR DE FÍSICA SPRING ---
struct Spring {
    float current = 160.0f;
    float target = 160.0f;
    float velocity = 0.0f;
    float tension = 210.0f;
    float damping = 23.0f;

    void update(float dt) {
        float force = -tension * (current - target);
        float loss = -damping * velocity;
        float accel = force + loss;
        velocity += accel * dt;
        current += velocity * dt;
    }
};

// --- ESTADO GLOBAL ---
struct TeleprompterState {
    bool expanded = false;
    bool is_paused = true;
    bool ghost_mode = false;
    bool show_shortcuts = false;
    bool hover_clickable = false;
    int hovered_drawer_item = -1;
    
    int window_w = 220;
    int window_h = 65;

    Spring widthSpring;
    Spring heightSpring;
    Spring drawerHeightSpring;
    
    std::wstring text;
    float scroll_y = 0.0f;
    float total_text_height = 0.0f;
    float scroll_speed = 35.0f;
    float font_size = 21.0f;
    float opacity = 0.92f;
    int top_margin = 12;

    ULONGLONG last_click_time = 0;

    // Recursos Direct2D & DirectWrite
    ID2D1Factory* pD2DFactory = nullptr;
    IDWriteFactory* pDWriteFactory = nullptr;
    ID2D1HwndRenderTarget* pRenderTarget = nullptr;
    
    ID2D1SolidColorBrush* pPillBrush = nullptr;
    ID2D1SolidColorBrush* pBorderBrush = nullptr;
    ID2D1SolidColorBrush* pTextBrush = nullptr;
    ID2D1SolidColorBrush* pAccentBrush = nullptr;
    ID2D1SolidColorBrush* pDimBrush = nullptr;
    ID2D1SolidColorBrush* pGhostBrush = nullptr;
    ID2D1SolidColorBrush* pEyeLineBrush = nullptr;
    ID2D1SolidColorBrush* pKeyBadgeBrush = nullptr;
    ID2D1SolidColorBrush* pGhostYellowBrush = nullptr;
    ID2D1SolidColorBrush* pGhostDimBrush = nullptr;
    ID2D1SolidColorBrush* pGhostBorderBrush = nullptr;
    ID2D1SolidColorBrush* pHoverItemBrush = nullptr;

    ID2D1LinearGradientBrush* pTopFadeBrush = nullptr;
    ID2D1LinearGradientBrush* pBottomFadeBrush = nullptr;

    IDWriteTextFormat* pTextFormat = nullptr;
    IDWriteTextFormat* pSmallFormat = nullptr;
    IDWriteTextFormat* pKeyBadgeFormat = nullptr;
    IDWriteTextFormat* pKeyActionFormat = nullptr;
    IDWriteTextLayout* pTextLayout = nullptr;
    
    HWND hwnd = NULL;
} g_app;

HANDLE g_hThread = NULL;

void SetGhostMode(bool enable) {
    g_app.ghost_mode = enable;
    if (g_app.hwnd) {
        LONG_PTR exStyle = GetWindowLongPtrW(g_app.hwnd, GWL_EXSTYLE);
        if (enable) {
            SetWindowLongPtrW(g_app.hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
        } else {
            SetWindowLongPtrW(g_app.hwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
        }
    }
}

void ExpandWindowPhysical() {
    if (g_app.window_w != 800 && g_app.hwnd) {
        g_app.window_w = 800;
        g_app.window_h = 450;
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int posX = (screenW - 800) / 2;
        SetWindowPos(g_app.hwnd, NULL, posX, 0, 800, 450, SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_app.pRenderTarget) {
            g_app.pRenderTarget->Resize(D2D1::SizeU(800, 450));
        }
    }
}

void CollapseWindowPhysical() {
    int targetH = g_app.top_margin + 50;
    if (g_app.window_w != 220 && g_app.hwnd) {
        g_app.window_w = 220;
        g_app.window_h = targetH;
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int posX = (screenW - 220) / 2;
        SetWindowPos(g_app.hwnd, NULL, posX, 0, 220, targetH, SWP_NOZORDER | SWP_NOACTIVATE);
        if (g_app.pRenderTarget) {
            g_app.pRenderTarget->Resize(D2D1::SizeU(220, targetH));
        }
    }
}

void PasteFromClipboard(HWND hwnd) {
    if (OpenClipboard(hwnd)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            WCHAR* pText = (WCHAR*)GlobalLock(hData);
            if (pText) {
                g_app.text = pText;
                g_app.scroll_y = 0.0f;
                g_app.is_paused = true;
                if (g_app.pTextLayout) {
                    g_app.pTextLayout->Release();
                    g_app.pTextLayout = nullptr;
                }
                if (g_app.pDWriteFactory && g_app.pTextFormat && !g_app.text.empty()) {
                    g_app.pDWriteFactory->CreateTextLayout(
                        g_app.text.c_str(),
                        (UINT32)g_app.text.length(),
                        g_app.pTextFormat,
                        620.0f,
                        30000.0f,
                        &g_app.pTextLayout
                    );
                    if (g_app.pTextLayout) {
                        DWRITE_TEXT_METRICS tm;
                        if (SUCCEEDED(g_app.pTextLayout->GetMetrics(&tm))) {
                            g_app.total_text_height = tm.height;
                        }
                    }
                }
                GlobalUnlock(hData);
            }
        }
        CloseClipboard();
    }
}

void RecreateTextLayout() {
    if (g_app.pTextLayout) {
        g_app.pTextLayout->Release();
        g_app.pTextLayout = nullptr;
    }
    if (g_app.pDWriteFactory && g_app.pTextFormat && !g_app.text.empty()) {
        g_app.pDWriteFactory->CreateTextLayout(
            g_app.text.c_str(),
            (UINT32)g_app.text.length(),
            g_app.pTextFormat,
            620.0f,
            30000.0f,
            &g_app.pTextLayout
        );

        if (g_app.pTextLayout) {
            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(g_app.pTextLayout->GetMetrics(&tm))) {
                g_app.total_text_height = tm.height;
            }
        }
    }
}

void UpdateDynamicBrushes() {
    if (!g_app.pRenderTarget) return;

    if (g_app.pPillBrush) {
        g_app.pPillBrush->Release();
        g_app.pPillBrush = nullptr;
    }
    g_app.pRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(0.04f, 0.04f, 0.05f, g_app.opacity),
        &g_app.pPillBrush
    );

    if (g_app.pTopFadeBrush) { g_app.pTopFadeBrush->Release(); g_app.pTopFadeBrush = nullptr; }
    if (g_app.pBottomFadeBrush) { g_app.pBottomFadeBrush->Release(); g_app.pBottomFadeBrush = nullptr; }

    ID2D1GradientStopCollection* pStopsTop = nullptr;
    D2D1_GRADIENT_STOP stopsTop[2];
    stopsTop[0].color = D2D1::ColorF(0.04f, 0.04f, 0.05f, g_app.opacity);
    stopsTop[0].position = 0.0f;
    stopsTop[1].color = D2D1::ColorF(0.04f, 0.04f, 0.05f, 0.0f);
    stopsTop[1].position = 1.0f;
    if (SUCCEEDED(g_app.pRenderTarget->CreateGradientStopCollection(stopsTop, 2, &pStopsTop))) {
        g_app.pRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 30)),
            pStopsTop, &g_app.pTopFadeBrush
        );
        pStopsTop->Release();
    }

    ID2D1GradientStopCollection* pStopsBottom = nullptr;
    D2D1_GRADIENT_STOP stopsBottom[2];
    stopsBottom[0].color = D2D1::ColorF(0.04f, 0.04f, 0.05f, 0.0f);
    stopsBottom[0].position = 0.0f;
    stopsBottom[1].color = D2D1::ColorF(0.04f, 0.04f, 0.05f, g_app.opacity);
    stopsBottom[1].position = 1.0f;
    if (SUCCEEDED(g_app.pRenderTarget->CreateGradientStopCollection(stopsBottom, 2, &pStopsBottom))) {
        g_app.pRenderTarget->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 30)),
            pStopsBottom, &g_app.pBottomFadeBrush
        );
        pStopsBottom->Release();
    }
}

bool InitD2D(HWND hwnd) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_app.pD2DFactory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_app.pDWriteFactory))) return false;

    RECT rc;
    GetClientRect(hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    D2D1_HWND_RENDER_TARGET_PROPERTIES hrtp = D2D1::HwndRenderTargetProperties(
        hwnd, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY
    );

    if (FAILED(g_app.pD2DFactory->CreateHwndRenderTarget(rtp, hrtp, &g_app.pRenderTarget))) return false;

    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f), &g_app.pBorderBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.96f, 0.96f, 1.0f), &g_app.pTextBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.06f, 0.76f, 0.52f, 1.0f), &g_app.pAccentBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.65f, 0.65f, 0.70f, 1.0f), &g_app.pDimBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.72f, 0.12f, 1.0f), &g_app.pGhostBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.08f), &g_app.pEyeLineBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), &g_app.pKeyBadgeBrush);
    
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.75f, 0.12f, 0.95f), &g_app.pGhostYellowBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.75f, 0.12f, 0.20f), &g_app.pGhostDimBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.96f, 0.75f, 0.12f, 0.50f), &g_app.pGhostBorderBrush);
    g_app.pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f), &g_app.pHoverItemBrush);

    UpdateDynamicBrushes();

    // Formato de texto del guion (Centrado)
    g_app.pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        NULL,
        DWRITE_FONT_WEIGHT_SEMI_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        g_app.font_size,
        L"es-es",
        &g_app.pTextFormat
    );
    if (g_app.pTextFormat) {
        g_app.pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    }

    // Formato de texto de ayuda (Centrado horizontal y verticalmente)
    g_app.pDWriteFactory->CreateTextFormat(
        L"Segoe UI Variable Display",
        NULL,
        DWRITE_FONT_WEIGHT_MEDIUM,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"es-es",
        &g_app.pSmallFormat
    );
    if (g_app.pSmallFormat) {
        g_app.pSmallFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_app.pSmallFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Formato de insignias de teclas
    g_app.pDWriteFactory->CreateTextFormat(
        L"Segoe UI Variable Display",
        NULL,
        DWRITE_FONT_WEIGHT_BOLD,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        10.5f,
        L"es-es",
        &g_app.pKeyBadgeFormat
    );
    if (g_app.pKeyBadgeFormat) {
        g_app.pKeyBadgeFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        g_app.pKeyBadgeFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    // Formato de acciones en el panel de atajos
    g_app.pDWriteFactory->CreateTextFormat(
        L"Segoe UI",
        NULL,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        11.5f,
        L"es-es",
        &g_app.pKeyActionFormat
    );
    if (g_app.pKeyActionFormat) {
        g_app.pKeyActionFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        g_app.pKeyActionFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }

    RecreateTextLayout();
    return true;
}

void CleanUpD2D() {
    if (g_app.pTopFadeBrush) { g_app.pTopFadeBrush->Release(); g_app.pTopFadeBrush = nullptr; }
    if (g_app.pBottomFadeBrush) { g_app.pBottomFadeBrush->Release(); g_app.pBottomFadeBrush = nullptr; }
    if (g_app.pTextLayout) { g_app.pTextLayout->Release(); g_app.pTextLayout = nullptr; }
    if (g_app.pTextFormat) { g_app.pTextFormat->Release(); g_app.pTextFormat = nullptr; }
    if (g_app.pSmallFormat) { g_app.pSmallFormat->Release(); g_app.pSmallFormat = nullptr; }
    if (g_app.pKeyBadgeFormat) { g_app.pKeyBadgeFormat->Release(); g_app.pKeyBadgeFormat = nullptr; }
    if (g_app.pKeyActionFormat) { g_app.pKeyActionFormat->Release(); g_app.pKeyActionFormat = nullptr; }
    if (g_app.pPillBrush) { g_app.pPillBrush->Release(); g_app.pPillBrush = nullptr; }
    if (g_app.pBorderBrush) { g_app.pBorderBrush->Release(); g_app.pBorderBrush = nullptr; }
    if (g_app.pTextBrush) { g_app.pTextBrush->Release(); g_app.pTextBrush = nullptr; }
    if (g_app.pAccentBrush) { g_app.pAccentBrush->Release(); g_app.pAccentBrush = nullptr; }
    if (g_app.pDimBrush) { g_app.pDimBrush->Release(); g_app.pDimBrush = nullptr; }
    if (g_app.pGhostBrush) { g_app.pGhostBrush->Release(); g_app.pGhostBrush = nullptr; }
    if (g_app.pEyeLineBrush) { g_app.pEyeLineBrush->Release(); g_app.pEyeLineBrush = nullptr; }
    if (g_app.pKeyBadgeBrush) { g_app.pKeyBadgeBrush->Release(); g_app.pKeyBadgeBrush = nullptr; }
    if (g_app.pGhostYellowBrush) { g_app.pGhostYellowBrush->Release(); g_app.pGhostYellowBrush = nullptr; }
    if (g_app.pGhostDimBrush) { g_app.pGhostDimBrush->Release(); g_app.pGhostDimBrush = nullptr; }
    if (g_app.pGhostBorderBrush) { g_app.pGhostBorderBrush->Release(); g_app.pGhostBorderBrush = nullptr; }
    if (g_app.pHoverItemBrush) { g_app.pHoverItemBrush->Release(); g_app.pHoverItemBrush = nullptr; }
    if (g_app.pRenderTarget) { g_app.pRenderTarget->Release(); g_app.pRenderTarget = nullptr; }
    if (g_app.pDWriteFactory) { g_app.pDWriteFactory->Release(); g_app.pDWriteFactory = nullptr; }
    if (g_app.pD2DFactory) { g_app.pD2DFactory->Release(); g_app.pD2DFactory = nullptr; }
}

void RenderIsland() {
    if (!g_app.pRenderTarget) return;

    g_app.pRenderTarget->BeginDraw();
    g_app.pRenderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    float pillW = g_app.widthSpring.current;
    float pillH = g_app.heightSpring.current;
    float pillX = (float)(g_app.window_w - (int)pillW) / 2.0f;
    float pillY = (float)g_app.top_margin;
    float cornerRadius = std::min(pillH / 2.0f, 28.0f);

    D2D1_ROUNDED_RECT pillRect = D2D1::RoundedRect(
        D2D1::RectF(pillX, pillY, pillX + pillW, pillY + pillH),
        cornerRadius,
        cornerRadius
    );

    // 1. Cuerpo de la isla principal y borde
    g_app.pRenderTarget->FillRoundedRectangle(pillRect, g_app.pPillBrush);
    ID2D1SolidColorBrush* borderToUse = g_app.ghost_mode ? g_app.pGhostBorderBrush : g_app.pBorderBrush;
    g_app.pRenderTarget->DrawRoundedRectangle(pillRect, borderToUse, g_app.ghost_mode ? 1.5f : 1.0f);

    // 2. Estado compacto (Píldora en reposo)
    if (pillW < 240.0f) {
        float dotX = pillX + 22.0f;
        float dotY = pillY + pillH / 2.0f;
        
        ID2D1SolidColorBrush* dotBrush = g_app.ghost_mode ? g_app.pGhostBrush : (g_app.is_paused ? g_app.pDimBrush : g_app.pAccentBrush);
        g_app.pRenderTarget->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(dotX, dotY), 4.0f, 4.0f),
            dotBrush
        );

        std::wstring status = g_app.ghost_mode ? L"Modo Fantasma" : (g_app.is_paused ? L"Teleprompter" : L"Leyendo...");
        D2D1_RECT_F textRect = D2D1::RectF(pillX, pillY, pillX + pillW + 10.0f, pillY + pillH);
        if (g_app.pSmallFormat) {
            g_app.pRenderTarget->DrawText(
                status.c_str(),
                (UINT32)status.length(),
                g_app.pSmallFormat,
                textRect,
                g_app.pTextBrush
            );
        }
    } 
    // 3. Estado expandido (Teleprónter activo)
    else {
        float clipMarginX = 26.0f;
        float clipMarginTop = 40.0f; // Espacio libre superior para el botón de modo fantasma
        float clipMarginBottom = 34.0f;
        
        D2D1_RECT_F clipRect = D2D1::RectF(
            pillX + clipMarginX,
            pillY + clipMarginTop,
            pillX + pillW - clipMarginX,
            pillY + pillH - clipMarginBottom
        );

        // Guía visual tenue de altura de la mirada
        float eyeLineY = pillY + clipMarginTop + 24.0f;
        g_app.pRenderTarget->DrawLine(
            D2D1::Point2F(pillX + clipMarginX + 40.0f, eyeLineY),
            D2D1::Point2F(pillX + pillW - clipMarginX - 40.0f, eyeLineY),
            g_app.pEyeLineBrush, 1.0f
        );

        // Área de recorte del texto
        g_app.pRenderTarget->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        if (g_app.pTextLayout) {
            D2D1_POINT_2F textOrigin = D2D1::Point2F(pillX + clipMarginX, pillY + clipMarginTop + 10.0f - g_app.scroll_y);
            g_app.pRenderTarget->DrawTextLayout(textOrigin, g_app.pTextLayout, g_app.pTextBrush);
        }

        g_app.pRenderTarget->PopAxisAlignedClip();

        // Degradado superior (Fade in)
        if (g_app.pTopFadeBrush) {
            g_app.pTopFadeBrush->SetStartPoint(D2D1::Point2F(pillX, clipRect.top));
            g_app.pTopFadeBrush->SetEndPoint(D2D1::Point2F(pillX, clipRect.top + 28.0f));
            g_app.pRenderTarget->FillRectangle(
                D2D1::RectF(clipRect.left, clipRect.top, clipRect.right, clipRect.top + 28.0f),
                g_app.pTopFadeBrush
            );
        }

        // Degradado inferior (Fade out)
        if (g_app.pBottomFadeBrush) {
            g_app.pBottomFadeBrush->SetStartPoint(D2D1::Point2F(pillX, clipRect.bottom - 28.0f));
            g_app.pBottomFadeBrush->SetEndPoint(D2D1::Point2F(pillX, clipRect.bottom));
            g_app.pRenderTarget->FillRectangle(
                D2D1::RectF(clipRect.left, clipRect.bottom - 28.0f, clipRect.right, clipRect.bottom),
                g_app.pBottomFadeBrush
            );
        }

        // Barra de progreso del guion
        float maxScroll = (std::max)(1.0f, g_app.total_text_height - (clipRect.bottom - clipRect.top));
        float progress = std::clamp(g_app.scroll_y / maxScroll, 0.0f, 1.0f);
        float progressWidth = (pillW - (clipMarginX * 2.0f)) * progress;
        
        g_app.pRenderTarget->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(pillX + clipMarginX, pillY + pillH - 26.0f, pillX + clipMarginX + progressWidth, pillY + pillH - 24.5f),
                1.0f, 1.0f
            ),
            g_app.ghost_mode ? g_app.pGhostBrush : g_app.pAccentBrush
        );

        // Texto de ayuda inferior con cálculo de porcentaje en Rueda: Vel(X%)
        if (pillH > 100.0f && g_app.pSmallFormat) {
            int speedPct = (int)((g_app.scroll_speed / 35.0f) * 100.0f);
            std::wstring velStr = L"Rueda: Vel(" + std::to_wstring(speedPct) + L"%)";

            std::wstring info;
            if (g_app.ghost_mode) {
                info = L"👻 MODO FANTASMA ACTIVO (Clics pasan a través • Usa Ctrl+Alt+G para volver)";
            } else if (g_app.is_paused) {
                info = L"⏸ PAUSADO  •  Clic: Reanudar  •  " + velStr + L"  •  Doble Clic: Reiniciar Texto  •  Ctrl+Alt+X: Atajos";
            } else {
                info = L"▶ REPRODUCIENDO  •  " + velStr + L"  •  Clic: Pausar";
            }

            D2D1_RECT_F infoRect = D2D1::RectF(pillX, pillY + pillH - 24.0f, pillX + pillW, pillY + pillH - 4.0f);
            g_app.pRenderTarget->DrawText(
                info.c_str(),
                (UINT32)info.length(),
                g_app.pSmallFormat,
                infoRect,
                g_app.ghost_mode ? g_app.pGhostBrush : g_app.pDimBrush
            );
        }

        // --- DIBUJAR BOTÓN MODO FANTASMA POR ENCIMA DE TODO ---
        if (!g_app.ghost_mode) {
            float btnRadius = 13.0f;
            float btnCenterX = pillX + pillW - 28.0f;
            float btnCenterY = pillY + 22.0f;

            D2D1_ELLIPSE btnEllipse = D2D1::Ellipse(D2D1::Point2F(btnCenterX, btnCenterY), btnRadius, btnRadius);
            g_app.pRenderTarget->FillEllipse(btnEllipse, g_app.pGhostYellowBrush);
            g_app.pRenderTarget->DrawEllipse(btnEllipse, g_app.pBorderBrush, 1.0f);

            D2D1_RECT_F iconRect = D2D1::RectF(btnCenterX - 11.0f, btnCenterY - 11.0f, btnCenterX + 11.0f, btnCenterY + 11.0f);
            if (g_app.pKeyBadgeFormat) {
                g_app.pRenderTarget->DrawText(L"👻", 2, g_app.pKeyBadgeFormat, iconRect, g_app.pTextBrush);
            }
        } else {
            float badgeW = 100.0f;
            float badgeH = 22.0f;
            float badgeX = pillX + pillW - badgeW - 16.0f;
            float badgeY = pillY + 11.0f;

            D2D1_ROUNDED_RECT badgeRect = D2D1::RoundedRect(
                D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH),
                11.0f, 11.0f
            );
            g_app.pRenderTarget->FillRoundedRectangle(badgeRect, g_app.pGhostDimBrush);
            g_app.pRenderTarget->DrawRoundedRectangle(badgeRect, g_app.pGhostBorderBrush, 1.0f);

            std::wstring ghostHint = L"👻 Ctrl+Alt+G";
            D2D1_RECT_F textRect = D2D1::RectF(badgeX, badgeY, badgeX + badgeW, badgeY + badgeH);
            if (g_app.pKeyBadgeFormat) {
                g_app.pRenderTarget->DrawText(ghostHint.c_str(), (UINT32)ghostHint.length(), g_app.pKeyBadgeFormat, textRect, g_app.pGhostYellowBrush);
            }
        }
    }

    // 4. Cajón desplegable inferior de atajos (Anexo estilo sub-isla interactivo)
    float drawerH = g_app.drawerHeightSpring.current;
    if (drawerH > 4.0f && g_app.expanded) {
        float drawerW = 510.0f;
        float drawerX = (float)(g_app.window_w - (int)drawerW) / 2.0f;
        float drawerY = pillY + pillH + 5.0f;

        D2D1_ROUNDED_RECT drawerRect = D2D1::RoundedRect(
            D2D1::RectF(drawerX, drawerY, drawerX + drawerW, drawerY + drawerH),
            18.0f, 18.0f
        );

        g_app.pRenderTarget->FillRoundedRectangle(drawerRect, g_app.pPillBrush);
        g_app.pRenderTarget->DrawRoundedRectangle(drawerRect, g_app.pBorderBrush, 1.0f);

        g_app.pRenderTarget->PushAxisAlignedClip(
            D2D1::RectF(drawerX + 8.0f, drawerY + 4.0f, drawerX + drawerW - 8.0f, drawerY + drawerH - 4.0f),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE
        );

        if (drawerH > 35.0f) {
            D2D1_RECT_F titleRect = D2D1::RectF(drawerX, drawerY + 6.0f, drawerX + drawerW, drawerY + 24.0f);
            std::wstring title = L"⚡ ATAJOS DE TECLADO (Haz clic sobre cualquiera para accionarlo)";
            g_app.pRenderTarget->DrawText(title.c_str(), (UINT32)title.length(), g_app.pSmallFormat, titleRect, g_app.pAccentBrush);

            float col1X = drawerX + 20.0f;
            float col2X = drawerX + (drawerW / 2.0f) + 12.0f;
            float startY = drawerY + 28.0f;
            float rowGap = 24.0f;

            struct Item {
                std::wstring key;
                std::wstring desc;
            };

            Item col1[] = {
                { L"Ctrl+Alt+Espacio", L"Pausa / Reproducir" },
                { L"Ctrl+Alt+R",       L"Reiniciar Texto" },
                { L"Ctrl+Alt+T",       L"Contraer / Abrir" },
                { L"Doble Clic",       L"Reiniciar Texto" }
            };

            Item col2[] = {
                { L"Rueda Ratón",      L"Ajustar Velocidad" },
                { L"Ctrl+Alt+G",       L"Modo Fantasma" },
                { L"Clic Derecho",     L"Pegar Portapapeles" },
                { L"Ctrl+Alt+X",       L"Cerrar este panel" }
            };

            for (int i = 0; i < 4; ++i) {
                float y = startY + (float)i * rowGap;

                // Fila Columna 1
                int idx1 = i;
                D2D1_RECT_F item1Rect = D2D1::RectF(col1X - 4.0f, y - 2.0f, col1X + 225.0f, y + 20.0f);
                if (g_app.hovered_drawer_item == idx1) {
                    g_app.pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(item1Rect, 5.0f, 5.0f), g_app.pHoverItemBrush);
                }

                D2D1_RECT_F kRect1 = D2D1::RectF(col1X, y, col1X + 112.0f, y + 18.0f);
                g_app.pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(kRect1, 4.0f, 4.0f), g_app.pKeyBadgeBrush);
                if (g_app.pKeyBadgeFormat) {
                    g_app.pRenderTarget->DrawText(col1[i].key.c_str(), (UINT32)col1[i].key.length(), g_app.pKeyBadgeFormat, kRect1, g_app.pTextBrush);
                }
                D2D1_RECT_F dRect1 = D2D1::RectF(col1X + 118.0f, y, col1X + 225.0f, y + 18.0f);
                if (g_app.pKeyActionFormat) {
                    g_app.pRenderTarget->DrawText(col1[i].desc.c_str(), (UINT32)col1[i].desc.length(), g_app.pKeyActionFormat, dRect1, g_app.pDimBrush);
                }

                // Fila Columna 2
                int idx2 = i + 4;
                D2D1_RECT_F item2Rect = D2D1::RectF(col2X - 4.0f, y - 2.0f, col2X + 225.0f, y + 20.0f);
                if (g_app.hovered_drawer_item == idx2) {
                    g_app.pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(item2Rect, 5.0f, 5.0f), g_app.pHoverItemBrush);
                }

                D2D1_RECT_F kRect2 = D2D1::RectF(col2X, y, col2X + 105.0f, y + 18.0f);
                g_app.pRenderTarget->FillRoundedRectangle(D2D1::RoundedRect(kRect2, 4.0f, 4.0f), g_app.pKeyBadgeBrush);
                if (g_app.pKeyBadgeFormat) {
                    g_app.pRenderTarget->DrawText(col2[i].key.c_str(), (UINT32)col2[i].key.length(), g_app.pKeyBadgeFormat, kRect2, g_app.pTextBrush);
                }
                D2D1_RECT_F dRect2 = D2D1::RectF(col2X + 111.0f, y, col2X + 225.0f, y + 18.0f);
                if (g_app.pKeyActionFormat) {
                    g_app.pRenderTarget->DrawText(col2[i].desc.c_str(), (UINT32)col2[i].desc.length(), g_app.pKeyActionFormat, dRect2, g_app.pDimBrush);
                }
            }
        }

        g_app.pRenderTarget->PopAxisAlignedClip();
    }

    g_app.pRenderTarget->EndDraw();
}

LRESULT CALLBACK IslandWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && g_app.hover_clickable && !g_app.ghost_mode) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;

    case WM_NCHITTEST: {
        if (g_app.ghost_mode) {
            return HTTRANSPARENT;
        }

        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hwnd, &pt);

        float pillW = g_app.widthSpring.current;
        float pillH = g_app.heightSpring.current;
        float pillX = (float)(g_app.window_w - (int)pillW) / 2.0f;
        float pillY = (float)g_app.top_margin;

        if (pt.x >= pillX && pt.x <= (pillX + pillW) && pt.y >= pillY && pt.y <= (pillY + pillH)) {
            return HTCLIENT;
        }

        float drawerH = g_app.drawerHeightSpring.current;
        if (drawerH > 10.0f) {
            float drawerW = 510.0f;
            float drawerX = (float)(g_app.window_w - (int)drawerW) / 2.0f;
            float drawerY = pillY + pillH + 5.0f;
            if (pt.x >= drawerX && pt.x <= (drawerX + drawerW) && pt.y >= drawerY && pt.y <= (drawerY + drawerH)) {
                return HTCLIENT;
            }
        }

        return HTTRANSPARENT;
    }

    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        
        if (!g_app.expanded) {
            ExpandWindowPhysical();
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_app.expanded = true;
            g_app.widthSpring.target = 680.0f;
            g_app.heightSpring.target = 220.0f;
        }

        bool was_clickable = g_app.hover_clickable;
        g_app.hover_clickable = false;
        g_app.hovered_drawer_item = -1;

        float pillW = g_app.widthSpring.current;
        float pillH = g_app.heightSpring.current;
        float pillX = (float)(g_app.window_w - (int)pillW) / 2.0f;
        float pillY = (float)g_app.top_margin;

        // 1. Botón fantasma
        if (!g_app.ghost_mode && pillW > 300.0f) {
            float btnCenterX = pillX + pillW - 28.0f;
            float btnCenterY = pillY + 22.0f;
            float dist = std::hypot((float)pt.x - btnCenterX, (float)pt.y - btnCenterY);
            if (dist <= 15.0f) {
                g_app.hover_clickable = true;
            }
        }

        // 2. Elementos del cajón de atajos
        float drawerH = g_app.drawerHeightSpring.current;
        if (drawerH > 35.0f) {
            float drawerW = 510.0f;
            float drawerX = (float)(g_app.window_w - (int)drawerW) / 2.0f;
            float drawerY = pillY + pillH + 5.0f;
            float col1X = drawerX + 20.0f;
            float col2X = drawerX + (drawerW / 2.0f) + 12.0f;
            float startY = drawerY + 28.0f;
            float rowGap = 24.0f;

            for (int i = 0; i < 4; ++i) {
                float y = startY + (float)i * rowGap;
                if (pt.x >= (col1X - 4.0f) && pt.x <= (col1X + 225.0f) && pt.y >= (y - 2.0f) && pt.y <= (y + 20.0f)) {
                    g_app.hovered_drawer_item = i;
                    g_app.hover_clickable = true;
                    break;
                }
                if (pt.x >= (col2X - 4.0f) && pt.x <= (col2X + 225.0f) && pt.y >= (y - 2.0f) && pt.y <= (y + 20.0f)) {
                    g_app.hovered_drawer_item = i + 4;
                    g_app.hover_clickable = true;
                    break;
                }
            }
        }

        // 3. Barra de ayuda inferior
        if (pt.y >= (pillY + pillH - 26.0f) && pt.y <= (pillY + pillH - 2.0f) && pt.x >= pillX && pt.x <= (pillX + pillW)) {
            g_app.hover_clickable = true;
        }

        if (was_clickable != g_app.hover_clickable) {
            SetCursor(LoadCursor(NULL, g_app.hover_clickable ? IDC_HAND : IDC_ARROW));
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_app.hover_clickable = false;
        g_app.hovered_drawer_item = -1;
        if (g_app.is_paused && !g_app.ghost_mode && !g_app.show_shortcuts) {
            g_app.expanded = false;
            g_app.widthSpring.target = 160.0f;
            g_app.heightSpring.target = 36.0f;
            g_app.drawerHeightSpring.target = 0.0f;
        }
        return 0;

    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        float pillW = g_app.widthSpring.current;
        float pillH = g_app.heightSpring.current;
        float pillX = (float)(g_app.window_w - (int)pillW) / 2.0f;
        float pillY = (float)g_app.top_margin;

        // Clic en el botón amarillo del modo fantasma
        if (!g_app.ghost_mode && pillW > 300.0f) {
            float btnCenterX = pillX + pillW - 28.0f;
            float btnCenterY = pillY + 22.0f;
            float dist = std::hypot((float)pt.x - btnCenterX, (float)pt.y - btnCenterY);
            if (dist <= 15.0f) {
                SetGhostMode(true);
                return 0;
            }
        }

        // Clics interactivos dentro del cajón de atajos
        if (g_app.hovered_drawer_item >= 0) {
            switch (g_app.hovered_drawer_item) {
            case 0: // Pausa / Reproducir
                g_app.is_paused = !g_app.is_paused;
                break;
            case 1: // Reiniciar Texto (Ctrl + Alt + R)
            case 3: // Reiniciar Texto (Doble Clic)
                g_app.scroll_y = 0.0f;
                g_app.is_paused = true;
                break;
            case 2: // Contraer / Abrir
                g_app.expanded = false;
                g_app.show_shortcuts = false;
                g_app.widthSpring.target = 160.0f;
                g_app.heightSpring.target = 36.0f;
                g_app.drawerHeightSpring.target = 0.0f;
                break;
            case 4: // Rueda Ratón / Ajustar Velocidad (+10 px/s)
                g_app.scroll_speed = (g_app.scroll_speed >= 90.0f ? 20.0f : g_app.scroll_speed + 10.0f);
                break;
            case 5: // Modo Fantasma
                SetGhostMode(true);
                break;
            case 6: // Pegar Portapapeles
                PasteFromClipboard(hwnd);
                break;
            case 7: // Cerrar este panel
                g_app.show_shortcuts = false;
                g_app.drawerHeightSpring.target = 0.0f;
                break;
            }
            return 0;
        }

        // Clics en la barra de ayuda inferior
        if (pt.y >= (pillY + pillH - 26.0f) && pt.y <= (pillY + pillH - 2.0f)) {
            if (pt.x >= (pillX + pillW - 160.0f)) {
                g_app.show_shortcuts = !g_app.show_shortcuts;
                g_app.drawerHeightSpring.target = g_app.show_shortcuts ? 135.0f : 0.0f;
                return 0;
            }
            if (pt.x >= (pillX + (pillW / 2.0f) - 60.0f) && pt.x <= (pillX + (pillW / 2.0f) + 60.0f)) {
                g_app.scroll_speed = (g_app.scroll_speed >= 90.0f ? 20.0f : g_app.scroll_speed + 10.0f);
                return 0;
            }
        }

        // Clic general: Pausa / Reproducir con detección de doble clic rápido
        ULONGLONG now = GetTickCount64();
        if (g_app.last_click_time > 0 && (now - g_app.last_click_time) <= 300) {
            g_app.scroll_y = 0.0f;
            g_app.is_paused = true;
            g_app.last_click_time = 0;
        } else {
            g_app.last_click_time = now;
            g_app.is_paused = !g_app.is_paused;
        }
        return 0;
    }

    case WM_MBUTTONUP:
        SetGhostMode(!g_app.ghost_mode);
        return 0;

    case WM_RBUTTONUP:
        PasteFromClipboard(hwnd);
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_app.scroll_speed = std::clamp(g_app.scroll_speed + (delta > 0 ? 5.0f : -5.0f), 10.0f, 200.0f);
        return 0;
    }

    case WM_HOTKEY: {
        switch (wParam) {
        case 101: // Ctrl + Alt + Espacio: Play / Pause
            g_app.is_paused = !g_app.is_paused;
            if (!g_app.is_paused && !g_app.expanded) {
                ExpandWindowPhysical();
                g_app.expanded = true;
                g_app.widthSpring.target = 680.0f;
                g_app.heightSpring.target = 220.0f;
            }
            break;
        case 102: // Ctrl + Alt + T: Toggle Expand
            g_app.expanded = !g_app.expanded;
            if (g_app.expanded) {
                ExpandWindowPhysical();
                g_app.widthSpring.target = 680.0f;
                g_app.heightSpring.target = 220.0f;
            } else {
                g_app.show_shortcuts = false;
                g_app.widthSpring.target = 160.0f;
                g_app.heightSpring.target = 36.0f;
                g_app.drawerHeightSpring.target = 0.0f;
            }
            break;
        case 103: // Ctrl + Alt + R: Reiniciar Texto
            g_app.scroll_y = 0.0f;
            g_app.is_paused = true;
            break;
        case 106: // Ctrl + Alt + G: Toggle Ghost Mode
            SetGhostMode(!g_app.ghost_mode);
            break;
        case 107: // Ctrl + Alt + X: Desplegar / Ocultar panel de atajos
            g_app.show_shortcuts = !g_app.show_shortcuts;
            if (g_app.show_shortcuts && !g_app.expanded) {
                ExpandWindowPhysical();
                g_app.expanded = true;
                g_app.widthSpring.target = 680.0f;
                g_app.heightSpring.target = 220.0f;
            }
            g_app.drawerHeightSpring.target = g_app.show_shortcuts ? 135.0f : 0.0f;
            break;
        }
        return 0;
    }

    case WM_TIMER: {
        float dt = 0.016f;
        g_app.widthSpring.update(dt);
        g_app.heightSpring.update(dt);
        g_app.drawerHeightSpring.update(dt);

        if (!g_app.is_paused && g_app.expanded) {
            g_app.scroll_y += g_app.scroll_speed * dt;
        }

        if (!g_app.expanded && g_app.widthSpring.current <= 170.0f && g_app.heightSpring.current <= 40.0f) {
            CollapseWindowPhysical();
        }

        RenderIsland();
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        UnregisterHotKey(hwnd, 101);
        UnregisterHotKey(hwnd, 102);
        UnregisterHotKey(hwnd, 103);
        UnregisterHotKey(hwnd, 106);
        UnregisterHotKey(hwnd, 107);
        CleanUpD2D();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

DWORD WINAPI IslandThreadProc(LPVOID) {
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = IslandWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"DynamicIslandTeleprompterClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassExW(&wc);

    g_app.window_w = 220;
    g_app.window_h = g_app.top_margin + 50;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int posX = (screenW - g_app.window_w) / 2;
    int posY = 0;

    g_app.widthSpring.current = 160.0f;
    g_app.widthSpring.target = 160.0f;
    g_app.heightSpring.current = 36.0f;
    g_app.heightSpring.target = 36.0f;

    g_app.drawerHeightSpring.current = 0.0f;
    g_app.drawerHeightSpring.target = 0.0f;

    g_app.hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
        wc.lpszClassName,
        L"DynamicIslandTeleprompter",
        WS_POPUP,
        posX, posY, g_app.window_w, g_app.window_h,
        NULL, NULL, wc.hInstance, NULL
    );

    if (!g_app.hwnd) return 0;

    MARGINS margins = { -1, -1, -1, -1 };
    DwmExtendFrameIntoClientArea(g_app.hwnd, &margins);
    SetLayeredWindowAttributes(g_app.hwnd, 0, 255, LWA_ALPHA);

    if (!InitD2D(g_app.hwnd)) {
        DestroyWindow(g_app.hwnd);
        return 0;
    }

    RegisterHotKey(g_app.hwnd, 101, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE);
    RegisterHotKey(g_app.hwnd, 102, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T');
    RegisterHotKey(g_app.hwnd, 103, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'R');
    RegisterHotKey(g_app.hwnd, 106, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'G');
    RegisterHotKey(g_app.hwnd, 107, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'X');

    ShowWindow(g_app.hwnd, SW_SHOWNOACTIVATE);
    SetTimer(g_app.hwnd, 1, 16, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

void LoadSettings() {
    PCWSTR script = Wh_GetStringSetting(L"script_text");
    if (script) {
        g_app.text = script;
        Wh_FreeStringSetting(script);
    }
    g_app.scroll_speed = (float)Wh_GetIntSetting(L"default_speed");
    if (g_app.scroll_speed <= 0.0f) g_app.scroll_speed = 35.0f;

    g_app.font_size = (float)Wh_GetIntSetting(L"font_size");
    if (g_app.font_size <= 0.0f) g_app.font_size = 21.0f;

    int op = Wh_GetIntSetting(L"island_opacity");
    if (op < 10 || op > 100) op = 92;
    g_app.opacity = (float)op / 100.0f;

    int tm = Wh_GetIntSetting(L"top_margin");
    if (tm < 0 || tm > 100) tm = 12;
    g_app.top_margin = tm;
}

BOOL Wh_ModInit() {
    Wh_Log(L"Iniciando Dynamic Island Teleprompter v1.6.0 por 0xCh4inz...");
    LoadSettings();

    g_hThread = CreateThread(NULL, 0, IslandThreadProc, NULL, 0, NULL);
    return (g_hThread != NULL);
}

void Wh_ModUninit() {
    Wh_Log(L"Cerrando Dynamic Island Teleprompter...");
    if (g_app.hwnd) {
        PostMessage(g_app.hwnd, WM_CLOSE, 0, 0);
    }
    if (g_hThread) {
        WaitForSingleObject(g_hThread, 2000);
        CloseHandle(g_hThread);
        g_hThread = NULL;
    }
}

void Wh_ModSettingsChanged() {
    LoadSettings();
    if (g_app.hwnd) {
        UpdateDynamicBrushes();
        RecreateTextLayout();
    }
}
