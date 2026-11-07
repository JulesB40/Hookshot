#include "vk_os.h"
// The Insert menu. Drawn on the CPU (GDI text into a bitmap) and copied over the window's image at the last step of a present,
// so it costs nothing while closed and it never touches the game's picture, nor what frame interpolation sees. Keys: Insert
// opens and closes it, Up and Down choose, Left and Right change, Esc closes; the game gets no keyboard input while it is open.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "vk_menu.h"

namespace mkwvk {
#if defined(_WIN32)
HWND app_window();
#endif
static std::atomic<uint32_t> injected_keys{0};
void menu_key(unsigned key, bool down) {
    if (key >= 7) return;
    if (down) injected_keys.fetch_or(1u << key); else injected_keys.fetch_and(~(1u << key));
}

Options opts;

namespace {

struct Row {
    const char* name;
    int* value;
    int lo, hi, step;
    const char* (*text)(int v);
};
const char* tx_framegen(int v) {
    static const char* t[] = {"Off", "", "2x", "3x", "4x"};
    return t[v];
}
const char* tx_onoff(int v) { return v ? "On" : "Off"; }
const char* tx_upscale(int v) {
    static const char* t[] = {"Off", "Catmull-Rom", "Lanczos"};
    return t[v];
}
const char* tx_percent(int v) {
    static char b[8];
    snprintf(b, sizeof b, "%d%%", v);
    return b;
}
const char* tx_resolution(int v) {
    static const char* t[] = {"Auto", "100%", "75%", "67%", "50%"};
    return t[v];
}
const char* tx_speed(int v) { return v ? "Real time" : "Per frame"; }
const char* tx_handheld(int v) { return v ? "On (restart)" : "Off (restart)"; }
const char* tx_ultrawide(int v) { return v ? "Monitor's shape (restart)" : "16:9 (restart)"; }

Row rows[] = {
#if defined(_WIN32)
    {"Fullscreen", &opts.fullscreen, 0, 1, 1, tx_onoff},
#endif
    {"Frame generation", &opts.framegen, 0, 4, 1, tx_framegen},
    {"Frame pacing", &opts.pace, 0, 1, 1, tx_onoff},
    {"Resolution", &opts.resolution, 0, 4, 1, tx_resolution},
    {"Upscaling", &opts.upscale, 0, 2, 1, tx_upscale},
    {"Sharpness", &opts.sharpness, 0, 100, 10, tx_percent},
    {"Game speed", &opts.realtime, 0, 1, 1, tx_speed},
    {"Handheld profile", &opts.handheld, 0, 1, 1, tx_handheld},
    {"Ultrawide", &opts.ultrawide, 0, 1, 1, tx_ultrawide},
    {"FPS counter", &opts.show_fps, 0, 1, 1, tx_onoff},
};
constexpr int kRows = sizeof rows / sizeof rows[0];

std::atomic<bool> open_now{false};
int selected = 0;
bool menu_refresh = false;
std::string ini_path() {
    return os::cache_path("hookshot_options.ini");
}
const char* ini_key(const Row& r) {
    static char k[32];
    size_t n = 0;
    for (const char* p = r.name; *p && n + 1 < sizeof k; p++) k[n++] = *p == ' ' ? '_' : (char)tolower(*p);
    k[n] = 0;
    return k;
}
void save() {
    if (FILE* f = fopen(ini_path().c_str(), "w")) {
        for (const Row& r : rows) fprintf(f, "%s=%d\n", ini_key(r), *r.value);
        fclose(f);
    }
}

// ---- what is drawn: bitmaps of the menu and the counter, in the window's byte order ----
struct Bitmap {
    uint32_t w = 0, h = 0;
    std::vector<uint8_t> px;
    uint64_t version = 0;
    bool keyed = false;                   // (black is see-through: copied as the runs of pixels in between)
    std::vector<VkBufferImageCopy> runs;
};
Bitmap menu_bmp, fps_bmp;
std::mutex bmp_mu;
int scale_of(uint32_t h) { return std::max(1u, h / 720); }

#if defined(_WIN32)
void gdi_draw(Bitmap& out, uint32_t w, uint32_t h, void (*paint)(HDC, uint32_t, uint32_t, int), int scale) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = (LONG)w;
    bi.bmiHeader.biHeight = -(LONG)h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteDC(dc); return; }
    HGDIOBJ old = SelectObject(dc, bmp);
    paint(dc, w, h, scale);
    GdiFlush();
    const bool rgba = dev.swap_format == VK_FORMAT_R8G8B8A8_UNORM || dev.swap_format == VK_FORMAT_R8G8B8A8_SRGB;
    std::lock_guard lk(bmp_mu);
    out.w = w, out.h = h;
    out.px.assign((const uint8_t*)bits, (const uint8_t*)bits + (size_t)w * h * 4);
    if (out.keyed) {
        out.runs.clear();
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w;) {
                const uint8_t* p = out.px.data() + ((size_t)y * w + x) * 4;
                if (!(p[0] | p[1] | p[2])) { x++; continue; }
                uint32_t e = x + 1;
                for (; e < w; e++) {
                    const uint8_t* q = out.px.data() + ((size_t)y * w + e) * 4;
                    if (!(q[0] | q[1] | q[2])) break;
                }
                VkBufferImageCopy c{};
                c.bufferOffset = ((uint64_t)y * w + x) * 4;
                c.bufferRowLength = w;
                c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                c.imageOffset = {(int32_t)x, (int32_t)y, 0};
                c.imageExtent = {e - x, 1, 1};
                out.runs.push_back(c);
                x = e;
            }
        }
    }
    for (size_t i = 0; i < out.px.size(); i += 4) {
        if (rgba) std::swap(out.px[i], out.px[i + 2]);
        out.px[i + 3] = 255;
    }
    out.version++;
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
}
HFONT font(int px, int weight) {
    return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH, "Segoe UI");
}
void fill(HDC dc, int x, int y, int w, int h, COLORREF c) {
    RECT r{x, y, x + w, y + h};
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
void paint_menu(HDC dc, uint32_t w, uint32_t h, int s) {
    fill(dc, 0, 0, (int)w, (int)h, RGB(16, 20, 18));
    fill(dc, 0, 0, (int)w, 2 * s, RGB(96, 176, 112));
    SetBkMode(dc, TRANSPARENT);
    HFONT title = font(20 * s, FW_SEMIBOLD), body = font(16 * s, FW_NORMAL);
    HGDIOBJ old = SelectObject(dc, title);
    SetTextColor(dc, RGB(236, 240, 236));
    TextOutA(dc, 16 * s, 12 * s, "Hookshot", 8);
    SelectObject(dc, body);
    const int rh = 28 * s, top = 44 * s;
    for (int i = 0; i < kRows; i++) {
        const int y = top + i * rh;
        if (i == selected) fill(dc, 8 * s, y, (int)w - 16 * s, rh, RGB(44, 72, 54));
        SetTextColor(dc, i == selected ? RGB(255, 255, 255) : RGB(196, 204, 198));
        TextOutA(dc, 16 * s, y + 4 * s, rows[i].name, (int)strlen(rows[i].name));
        char v[40];
        snprintf(v, sizeof v, i == selected ? "<  %s  >" : "%s", rows[i].text(*rows[i].value));
        SIZE sz{};
        GetTextExtentPoint32A(dc, v, (int)strlen(v), &sz);
        TextOutA(dc, (int)w - 16 * s - sz.cx, y + 4 * s, v, (int)strlen(v));
    }
    SetTextColor(dc, RGB(140, 150, 144));
    const char* hint = "Arrows change   Insert or F1 closes";
    TextOutA(dc, 16 * s, top + kRows * rh + 6 * s, hint, (int)strlen(hint));
    SelectObject(dc, old);
    DeleteObject(title);
    DeleteObject(body);
}
char fps_text[48] = "";
char fps_sub[32] = "";
/// The usual overlay: a bold green number with a dark edge, nothing behind it. Hard-edged letters: see-through pixels are copied or not.
void outlined(HDC dc, int x, int y, const char* text, COLORREF colour, int edge) {
    SetTextColor(dc, RGB(6, 8, 6));
    const int n = (int)strlen(text);
    for (int dy = -edge; dy <= edge; dy++)
        for (int dx = -edge; dx <= edge; dx++)
            if (dx || dy) TextOutA(dc, x + dx, y + dy, text, n);
    SetTextColor(dc, colour);
    TextOutA(dc, x, y, text, n);
}
HFONT hard_font(int px, int weight) {
    return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                       DEFAULT_PITCH, "Segoe UI");
}
void paint_fps(HDC dc, uint32_t w, uint32_t h, int s) {
    fill(dc, 0, 0, (int)w, (int)h, RGB(0, 0, 0));
    SetBkMode(dc, TRANSPARENT);
    SetTextAlign(dc, TA_RIGHT | TA_TOP);
    const COLORREF green = RGB(72, 255, 110);
    const int right = (int)w - 2 * s;
    HFONT label = hard_font(13 * s, FW_BOLD), big = hard_font(28 * s, FW_BOLD);
    HGDIOBJ old = SelectObject(dc, label);
    outlined(dc, right, 14 * s, "FPS", green, s);
    SelectObject(dc, big);
    outlined(dc, right - 30 * s, 0, fps_text, green, s);
    if (fps_sub[0]) {
        SelectObject(dc, label);
        outlined(dc, right, 32 * s, fps_sub, RGB(226, 232, 226), s);
    }
    SelectObject(dc, old);
    DeleteObject(label);
    DeleteObject(big);
}
void redraw_menu(int s) {
    gdi_draw(menu_bmp, 360u * s, (uint32_t)((44 + kRows * 28 + 34) * s), paint_menu, s);
}

void redraw_fps(int s) { gdi_draw(fps_bmp, 120u * s, 54u * s, paint_fps, s); }
#else
#include "third_party/font8x8_basic.h"
char fps_text[48] = "", fps_sub[32] = "";
void pixel(Bitmap& b, int x, int y, uint32_t colour) {
    if (x < 0 || y < 0 || (uint32_t)x >= b.w || (uint32_t)y >= b.h) return;
    uint8_t* p = b.px.data() + ((size_t)y * b.w + (size_t)x) * 4;
    p[0] = (uint8_t)(colour >> 16); p[1] = (uint8_t)(colour >> 8); p[2] = (uint8_t)colour; p[3] = 255;
}
void text(Bitmap& b, int x, int y, const char* message, uint32_t colour, int scale) {
    for (; *message; ++message, x += 8 * scale) {
        const unsigned ch = (unsigned char)*message;
        if (ch >= 128) continue;
        for (int gy = 0; gy < 8; ++gy) for (int gx = 0; gx < 8; ++gx)
            if ((unsigned char)font8x8_basic[ch][gy] >> gx & 1)
                for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx)
                    pixel(b, x + gx * scale + dx, y + gy * scale + dy, colour);
    }
}
void finish_bitmap(Bitmap& b) {
    b.runs.clear();
    for (uint32_t y = 0; b.keyed && y < b.h; ++y) for (uint32_t x = 0; x < b.w;) {
        const uint8_t* p = b.px.data() + ((size_t)y * b.w + x) * 4;
        if (!(p[0] | p[1] | p[2])) { ++x; continue; }
        uint32_t end = x + 1;
        while (end < b.w) {
            const uint8_t* q = b.px.data() + ((size_t)y * b.w + end) * 4;
            if (!(q[0] | q[1] | q[2])) break;
            ++end;
        }
        VkBufferImageCopy copy{};
        copy.bufferOffset = ((uint64_t)y * b.w + x) * 4; copy.bufferRowLength = b.w;
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageOffset = {(int32_t)x, (int32_t)y, 0}; copy.imageExtent = {end - x, 1, 1};
        b.runs.push_back(copy); x = end;
    }
    if (dev.swap_format == VK_FORMAT_R8G8B8A8_UNORM || dev.swap_format == VK_FORMAT_R8G8B8A8_SRGB)
        for (size_t i = 0; i < b.px.size(); i += 4) std::swap(b.px[i], b.px[i + 2]);
    ++b.version;
}
void redraw_menu(int scale) {
    std::lock_guard lock(bmp_mu);
    Bitmap& b = menu_bmp;
    b.w = 480u * scale; b.h = (60u + kRows * 28u) * scale;
    b.px.assign((size_t)b.w * b.h * 4, 0);
    for (uint32_t y = 0; y < b.h; ++y) for (uint32_t x = 0; x < b.w; ++x)
        pixel(b, x, y, y >= (uint32_t)((40 + selected * 28) * scale) &&
            y < (uint32_t)((68 + selected * 28) * scale) ? 0x2c4836 : 0x101412);
    text(b, 12 * scale, 12 * scale, "Hookshot", 0xecf0ec, scale);
    for (int i = 0; i < kRows; ++i) {
        text(b, 12 * scale, (48 + i * 28) * scale, rows[i].name, 0xecf0ec, scale);
        const char* value = rows[i].text(*rows[i].value);
        text(b, (int)b.w - (int)strlen(value) * 8 * scale - 12 * scale, (48 + i * 28) * scale, value, 0x60b070, scale);
    }
    finish_bitmap(b);
}
void redraw_fps(int scale) {
    std::lock_guard lock(bmp_mu);
    Bitmap& b = fps_bmp;
    b.w = 160u * scale; b.h = 40u * scale;
    b.px.assign((size_t)b.w * b.h * 4, 0);
    text(b, 0, 0, fps_text, 0x48ff6e, scale * 2);
    text(b, 0, 24 * scale, fps_sub, 0xe2e8e2, scale);
    finish_bitmap(b);
}
#endif

std::atomic<int64_t> game_frames{0}, shown_frames{0};
int cur_scale = 1;

struct Upload {
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    void* map = nullptr;
    uint64_t size = 0, version = 0;
};
Upload ups[2][3];   // (the menu's, the counter's; a frame in flight's each)

void copy_over(VkCommandBuffer cmd, VkImage dst, Bitmap& b, Upload& u, int x, int y, uint32_t w, uint32_t h) {
    std::lock_guard lk(bmp_mu);
    if (!b.w || (uint32_t)x + b.w > w || (uint32_t)y + b.h > h) return;
    const uint64_t size = (uint64_t)b.w * b.h * 4;
    if (u.size < size) {
        if (!create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, u.buf, u.alloc, &u.map)) return;
        u.size = size, u.version = 0;
    }
    if (u.version != b.version) std::memcpy(u.map, b.px.data(), size), u.version = b.version;
    if (b.keyed) {
        static thread_local std::vector<VkBufferImageCopy> at;
        at = b.runs;
        for (auto& r : at) r.imageOffset.x += x, r.imageOffset.y += y;
        if (!at.empty()) vkCmdCopyBufferToImage(cmd, u.buf, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)at.size(), at.data());
        return;
    }
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageOffset = {x, y, 0};
    c.imageExtent = {b.w, b.h, 1};
    vkCmdCopyBufferToImage(cmd, u.buf, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
}

} // namespace

void menu_init() {
    static bool done;
    if (done) return;
    done = true;
    if (FILE* f = fopen(ini_path().c_str(), "r")) {
        char line[80];
        bool has_resolution = false;
        int old_dynres = -1;   // (the row before Resolution: 0, the full scale, is its 100%; 1 is Auto)
        while (fgets(line, sizeof line, f)) {
            char* eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            for (Row& r : rows)
                if (!strcmp(line, ini_key(r))) *r.value = std::min(std::max(atoi(eq + 1), r.lo), r.hi);
            has_resolution |= !strcmp(line, "resolution");
            if (!strcmp(line, "dynamic_resolution")) old_dynres = atoi(eq + 1);
        }
        fclose(f);
        if (!has_resolution && old_dynres >= 0) opts.resolution = old_dynres ? 0 : 1;
    }
    if (opts.framegen == 1) opts.framegen = 2;
    if (const char* e = getenv("RX_VK_FRAMEGEN_MULT")) opts.framegen = std::min(std::max(atoi(e), 2), 4);
    if (const char* e = getenv("RX_VK_PACE")) opts.pace = atoi(e) != 0;
    if (const char* e = getenv("RX_VK_UPSCALE")) opts.upscale = atoi(e);
    if (const char* e = getenv("HOS_REALTIME_STEP")) opts.realtime = atoi(e) != 0;
    if (const char* e = getenv("HOS_DYNRES")) opts.resolution = atoi(e) != 0 ? 0 : 1;
}

bool menu_is_open() { return open_now.load(std::memory_order_relaxed); }

void menu_note_frame(bool game_frame) {
    shown_frames.fetch_add(1, std::memory_order_relaxed);
    if (game_frame) game_frames.fetch_add(1, std::memory_order_relaxed);
}

// The Fullscreen row and F11 are one setting: the row asks the window's thread (host/display.c), F11 shows in the row
#if defined(_WIN32)
void fullscreen_tick() {
    static int applied = 0, settle = 0;
    const HWND main_window = app_window();
    if (!main_window) return;
    static const bool env = getenv("HOS_FULLSCREEN") != nullptr;   // (a test's own choice: the row is left alone)
    if (env) return;
    if (opts.fullscreen != applied) {
        applied = opts.fullscreen;
        settle = 60;
        PostMessageA(main_window, WM_APP + 7, (WPARAM)applied, 0);
        return;
    }
    if (settle > 0) { settle--; return; }
    const int now = !(GetWindowLongA(main_window, GWL_STYLE) & WS_CAPTION);
    if (now != applied) {
        opts.fullscreen = applied = now;
        menu_refresh = true;
        save();
    }
}

#endif
void menu_tick() {
#if defined(_WIN32)
    fullscreen_tick();
    static bool held[7];
    static const int keys[7] = {VK_INSERT, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_ESCAPE, VK_F1};   // (F1: for keyboards without Insert)
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    const bool mine = pid == GetCurrentProcessId();
    bool press[7] = {};
    for (int i = 0; i < 7; i++) {
        const bool down = mine && (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        press[i] = down && !held[i];
        held[i] = down;
    }
#else
    static bool held[7]{};
    bool press[7]{};
    const uint32_t keys = injected_keys.load();
    for (int i = 0; i < 7; ++i) {
        const bool down = (keys >> i & 1) != 0;
        press[i] = down && !held[i]; held[i] = down;
    }
#endif
    const int scale = scale_of(dev.swap_extent.height);
    bool changed = scale != cur_scale || menu_refresh;
    menu_refresh = false;
    cur_scale = scale;
    if (press[0] || press[6]) open_now = !open_now.load(), changed = true;
    if (open_now.load()) {
        if (press[5]) open_now = false;
        if (press[1]) selected = (selected + kRows - 1) % kRows, changed = true;
        if (press[2]) selected = (selected + 1) % kRows, changed = true;
        if (press[3] || press[4]) {
            Row& r = rows[selected];
            int v = *r.value + (press[4] ? r.step : -r.step);
            if (r.value == &opts.framegen && v == 1) v = press[4] ? 2 : 0;   // (off, 2x, 3x, 4x)
            *r.value = v > r.hi ? r.lo : v < r.lo ? r.hi : v;
            changed = true;
            save();
        }
        if (changed) redraw_menu(scale);
    }
    if (opts.show_fps) {
        static int64_t next = 0, last_game = 0, last_shown = 0;
        const int64_t now = prof_now();
        if (now >= next) {
            const double secs = next ? (now - next + 500000000) / 1e9 : 1.0;
            const int64_t g = game_frames.load(), sh = shown_frames.load();
            const double shown = (sh - last_shown) / secs, game = (g - last_game) / secs;
            snprintf(fps_text, sizeof fps_text, "%.0f", shown);
            if (opts.framegen >= 2) snprintf(fps_sub, sizeof fps_sub, "game %.0f", game);
            else fps_sub[0] = 0;
            last_game = g, last_shown = sh;
            next = now + 500000000;
            fps_bmp.keyed = true;
            redraw_fps(scale);
        }
    }
}

void menu_record(VkCommandBuffer cmd, VkImage dst, int slot, uint32_t w, uint32_t h) {
    if (opts.show_fps) {
        std::unique_lock lk(bmp_mu);
        const uint32_t fw = fps_bmp.w;
        lk.unlock();
        if (fw) copy_over(cmd, dst, fps_bmp, ups[1][slot], (int)w - (int)fw - (int)(12 * cur_scale), (int)(10 * cur_scale), w, h);
    }
    if (open_now.load(std::memory_order_relaxed)) {
        std::unique_lock lk(bmp_mu);
        const uint32_t mw = menu_bmp.w, mh = menu_bmp.h;
        lk.unlock();
        // (one copy finished before the next: on a Radeon the menu came out with its top torn away beside the FPS counter)
        VkMemoryBarrier2 mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        mb.srcStageMask = mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
        mb.srcAccessMask = mb.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        VkDependencyInfo di{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        di.memoryBarrierCount = 1, di.pMemoryBarriers = &mb;
        vkCmdPipelineBarrier2(cmd, &di);
        if (mw) copy_over(cmd, dst, menu_bmp, ups[0][slot], (int)(w - mw) / 2, (int)(h - mh) / 2, w, h);
    }
}

} // namespace mkwvk
