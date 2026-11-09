//
// open horizon -- undefined_darkness@outlook.com
//

#include "platform.h"
#include "GLFW/glfw3.h"
#include "render/render.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

//------------------------------------------------------------

//debug helpers, enabled via env vars:
//  OH_DUMP_DIR   - dump default framebuffer (RGB + alpha channel as grayscale BMPs)
//  OH_DUMP_AFTER - frame number to start dumping after (default 0)
//  OH_DUMP_EVERY - dump every N frames (default 60)
//  OH_DUMP_MAX   - max dumps (default 200)
//  OH_FF_AUTO    - auto-drive menu into free flight, then hold throttle
static void write_bmp(const char *path, int w, int h, int bpp, const unsigned char *src, bool src_is_bgr_or_gray, bool flip_rows)
{
    const int channels = bpp / 8;
    const size_t row_size = (size_t(w) * channels + 3) & ~size_t(3);
    const size_t img_size = row_size * h;
    const size_t file_size = 54 + img_size;

    FILE *f = fopen(path, "wb");
    if (!f)
        return;

    unsigned char hdr[54] = {0};
    hdr[0] = 'B', hdr[1] = 'M';
    memcpy(hdr + 2, &file_size, 4);
    unsigned int off = 54;
    memcpy(hdr + 10, &off, 4);
    unsigned int ihdr = 40;
    memcpy(hdr + 14, &ihdr, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    unsigned short planes = 1, bpp16 = (unsigned short)bpp;
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp16, 2);
    unsigned int isz = (unsigned int)img_size;
    memcpy(hdr + 34, &isz, 4);
    int ppm = 2835;
    memcpy(hdr + 38, &ppm, 4);
    memcpy(hdr + 42, &ppm, 4);
    fwrite(hdr, 1, 54, f);

    std::vector<unsigned char> row(row_size, 0);
    for (int y = 0; y < h; ++y)
    {
        const int sy = flip_rows ? h - 1 - y : y;
        for (int x = 0; x < w; ++x)
        {
            const unsigned char *s = src + (size_t(sy) * w + x) * channels;
            unsigned char *d = row.data() + x * channels;
            if (channels == 3)
            {
                d[0] = s[2], d[1] = s[1], d[2] = s[0]; //to BGR
            }
            else //gray
            {
                d[0] = d[1] = d[2] = src_is_bgr_or_gray ? s[0] : s[0];
            }
        }
        fwrite(row.data(), 1, row_size, f);
    }
    fclose(f);
}

static void dump_frame(const char *dir, int w, int h, int frame_no)
{
    std::vector<unsigned char> px(size_t(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());

    char path[1024];

    //RGB (convert RGBA->BGR24, rows already bottom-up like BMP)
    {
        std::vector<unsigned char> bgr(size_t(w) * h * 3);
        for (size_t i = 0; i < size_t(w) * h; ++i)
        {
            bgr[i * 3 + 0] = px[i * 4 + 2];
            bgr[i * 3 + 1] = px[i * 4 + 1];
            bgr[i * 3 + 2] = px[i * 4 + 0];
        }
        snprintf(path, sizeof(path), "%s/f%08d.bmp", dir, frame_no);
        write_bmp(path, w, h, 24, bgr.data(), true, false);
    }

    //alpha channel as grayscale (0=transparent..255=opaque)
    {
        std::vector<unsigned char> bgra(size_t(w) * h * 3);
        for (size_t i = 0; i < size_t(w) * h; ++i)
        {
            bgra[i * 3 + 0] = bgra[i * 3 + 1] = bgra[i * 3 + 2] = px[i * 4 + 3];
        }
        snprintf(path, sizeof(path), "%s/f%08d.a.bmp", dir, frame_no);
        write_bmp(path, w, h, 24, bgra.data(), true, false);
    }
}

//auto-drive: menu = [Mission, DM, TDM, Free flight(4th), MP, Settings, Exit]
//schedule in frames (60fps): after each ENTER the next screen needs a moment to load
struct auto_key_event { int frame; int key; int hold; };
static const auto_key_event ff_schedule[] =
{
    { 600,  GLFW_KEY_DOWN,  1 }, //down 1 (to DM)
    { 700,  GLFW_KEY_DOWN,  1 }, //down 2 (to TDM)
    { 800,  GLFW_KEY_DOWN,  1 }, //down 3 (to Free flight)
    { 900,  GLFW_KEY_ENTER, 1 }, //confirm -> map_select
    { 1500, GLFW_KEY_ENTER, 1 }, //confirm map -> ac_select
    { 2100, GLFW_KEY_ENTER, 1 }, //confirm ac -> color_select
    { 2700, GLFW_KEY_ENTER, 1 }, //confirm color -> start (loading)
};
static const int ff_schedule_size = (int)(sizeof(ff_schedule) / sizeof(ff_schedule[0]));
static const int ff_throttle_from = 3600; //hold W from here (in game, after loading)

static void debug_frame_hooks(int w, int h)
{
    static int frame_no = 0;
    static int dumped = 0;

    ++frame_no;

    const char *dir = getenv("OH_DUMP_DIR");
    if (dir && dir[0])
    {
        const int every = getenv("OH_DUMP_EVERY") ? atoi(getenv("OH_DUMP_EVERY")) : 60;
        const int maxd = getenv("OH_DUMP_MAX") ? atoi(getenv("OH_DUMP_MAX")) : 200;
        const int after = getenv("OH_DUMP_AFTER") ? atoi(getenv("OH_DUMP_AFTER")) : 0;

        if (frame_no > after && dumped < maxd && (frame_no - after) % every == 0)
            dump_frame(dir, w, h, frame_no), ++dumped;
    }

    if (!getenv("OH_FF_AUTO"))
        return;

    //key injection: m_buttons is read by the main loop next iteration
    auto &buttons = platform::debug_buttons();
    for (int i = 0; i < ff_schedule_size; ++i)
    {
        if (frame_no == ff_schedule[i].frame)
            buttons[ff_schedule[i].key] = true;
        if (frame_no == ff_schedule[i].frame + ff_schedule[i].hold)
            buttons[ff_schedule[i].key] = false;
    }

    if (frame_no >= ff_throttle_from)
    {
        if (frame_no == ff_throttle_from)
            buttons[GLFW_KEY_W] = true;
        if (frame_no == ff_throttle_from + 60 * 90) //release after 90s
            buttons[GLFW_KEY_W] = false;
    }

    if (frame_no == ff_throttle_from + 60 * 100) //auto-quit ~100s into flight
        buttons[GLFW_KEY_ESCAPE] = true;

    //diagnostic: magenta clear color (env OH_MAGENTA) - reveals coverage holes
    if (frame_no == 10 && getenv("OH_MAGENTA"))
        nya_render::set_clear_color(1.0f, 0.0f, 1.0f, 1.0f);
}

//------------------------------------------------------------

std::map<int, bool> platform::m_buttons;
platform::key_callback platform::m_key_callback;

//------------------------------------------------------------

bool platform::init(int width, int height, const char *title, bool fullscreen)
{
    if (!glfwInit())
        return false;
/*
    if (nya_render::get_render_api() == nya_render::render_api_opengl3)
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    }
*/
    glfwSwapInterval(1);

    /*
#ifdef _WIN32
        typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int interval);
        PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;
        wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
        if (wglSwapIntervalEXT)
            wglSwapIntervalEXT(1);
#endif
    */

#if GLFW_VERSION_MAJOR > 3 || GLFW_VERSION_MINOR >= 3
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
#endif

    if (fullscreen)
    {
        auto monitor = glfwGetPrimaryMonitor();
        if (!monitor)
            return init(width, height, title, false);

        auto mode = glfwGetVideoMode(monitor);
        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
        m_window = glfwCreateWindow(mode->width, mode->height, title, monitor, NULL);
    }
    else
        m_window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!m_window)
    {
        glfwTerminate();
        return false;
    }

    glfwSetKeyCallback(m_window, key_func);
    glfwMakeContextCurrent(m_window);
    UpdateSize();

    return true;
}

//------------------------------------------------------------

void platform::terminate()
{
    if (!m_window)
        return;

    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = 0;
}

//------------------------------------------------------------

bool platform::should_terminate()
{
    return m_window ? glfwWindowShouldClose(m_window) > 0 : true;
}

//------------------------------------------------------------

void platform::end_frame()
{
    if (!m_window)
        return;

    m_last_buttons = m_buttons;
    glfwPollEvents();

    debug_frame_hooks(m_frame_w, m_frame_h); //debug: dump framebuffer / auto-drive, no-op without env vars

    double mx,my;
    glfwGetCursorPos(m_window, &mx, &my);
    m_mouse_x = int(mx), m_mouse_y = int(my);
    UpdateSize();

    glfwSwapBuffers(m_window);
}

//------------------------------------------------------------

bool platform::was_pressed(int key)
{
    auto k = m_buttons.find(key);
    if (k == m_buttons.end() || !k->second)
        return false;

    auto l = m_last_buttons.find(key);
    return l == m_last_buttons.end() || !l->second;
}

//------------------------------------------------------------

bool platform::get_key(int key) { auto k = m_buttons.find(key); return k == m_buttons.end() ? false : k->second; }
bool platform::get_mouse_lbtn() { return m_window ? glfwGetMouseButton(m_window, 0) > 0 : false; }
bool platform::get_mouse_rbtn() { return m_window ? glfwGetMouseButton(m_window, 1) > 0 : false; }
int platform::get_mouse_x() { return m_mouse_x; }
int platform::get_mouse_y() { return m_mouse_y; }
int platform::get_frame_width() { return m_frame_w; }
int platform::get_frame_height() { return m_frame_h; }
int platform::get_window_width() { return m_window_w; }
int platform::get_window_height() { return m_window_h; }

//------------------------------------------------------------

void platform::set_fullscreen(bool value, int windowed_width, int windowed_height)
{
    if (!m_window)
        return;

    auto monitor = glfwGetPrimaryMonitor();
    if (!monitor)
        return;

#if GLFW_VERSION_MAJOR > 3 || GLFW_VERSION_MINOR >= 2
    auto mode = glfwGetVideoMode(monitor);
    const int desktop_width = mode->width, desktop_height = mode->height;

    if (value)
    {
        glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    else
    {
        const int x = (desktop_width - windowed_width) / 2, y = (desktop_height - windowed_height) / 2;
        glfwSetWindowMonitor(m_window, 0, x, y, windowed_width, windowed_height, 0);
    }
    UpdateSize();
#endif
}

//------------------------------------------------------------

void platform::set_keyboard_callback(key_callback &k) { m_key_callback = k; }

//------------------------------------------------------------

void platform::key_func(GLFWwindow *, int key, int scancode, int action, int mods)
{
    if (action == GLFW_PRESS)
    {
        m_buttons[key] = true;
        if (m_key_callback)
        {
            if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
                m_key_callback('0' + key - GLFW_KEY_0);
            else if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
                m_key_callback('A' + key - GLFW_KEY_A);
            else if (key == GLFW_KEY_PERIOD)
                m_key_callback('.');
            else if (key == GLFW_KEY_SEMICOLON)
                m_key_callback(':');
            else if (key == GLFW_KEY_MINUS)
                m_key_callback('-');
            else if (key == GLFW_KEY_SLASH)
                m_key_callback('/');
            else if (key == GLFW_KEY_SPACE)
                m_key_callback(' ');
            else if (key == GLFW_KEY_BACKSPACE)
                m_key_callback(8);
        }
    }
    else if (action == GLFW_RELEASE)
        m_buttons[key] = false;

#ifdef __APPLE__
    m_buttons[GLFW_KEY_LEFT_CONTROL] = mods & GLFW_MOD_CONTROL;
    m_buttons[GLFW_KEY_LEFT_SHIFT] = mods & GLFW_MOD_SHIFT;
#endif
}

//------------------------------------------------------------

void platform::UpdateSize()
{
    glfwGetWindowSize(m_window, &m_window_w, &m_window_h);
    glfwGetFramebufferSize(m_window, &m_frame_w, &m_frame_h);
}

//------------------------------------------------------------
