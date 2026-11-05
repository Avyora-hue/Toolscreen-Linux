// Test harness mimicking how Minecraft 1.16 (LWJGL 3 + GLFW) uses GLFW and GL: every entry point is looked up
// with dlsym/glXGetProcAddress, the frame is rendered into an FBO sized from glfwGetFramebufferSize and
// presented to framebuffer 0 with glViewport(0,0,w,h) + a textured quad.
// The test pattern contains the colours Toolscreen's default mirrors look for.
#include <GL/gl.h>
#include <GL/glext.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct GLFWwindow;
#define F(ret, name, ...) static ret (*name)(__VA_ARGS__);
F(int, glfwInit)
F(void, glfwWindowHint, int, int)
F(GLFWwindow*, glfwCreateWindow, int, int, const char*, void*, void*)
F(void, glfwMakeContextCurrent, GLFWwindow*)
F(void, glfwSwapBuffers, GLFWwindow*)
F(void, glfwPollEvents)
F(int, glfwWindowShouldClose, GLFWwindow*)
F(void, glfwGetFramebufferSize, GLFWwindow*, int*, int*)
F(void*, glfwSetFramebufferSizeCallback, GLFWwindow*, void (*)(GLFWwindow*, int, int))
F(void*, glfwSetKeyCallback, GLFWwindow*, void (*)(GLFWwindow*, int, int, int, int))
F(void*, glfwSetCursorPosCallback, GLFWwindow*, void (*)(GLFWwindow*, double, double))
F(void, glfwSetWindowPos, GLFWwindow*, int, int)
F(void, glfwSwapInterval, int)
F(void, glfwSetInputMode, GLFWwindow*, int, int)
#undef F

using GPA = void* (*)(const unsigned char*);
static GPA gpa;
namespace P {
#define G(ret, name, ...) static ret(APIENTRY* name)(__VA_ARGS__);
G(void, glViewport, GLint, GLint, GLsizei, GLsizei)
G(void, glClearColor, float, float, float, float)
G(void, glClear, GLbitfield)
G(void, glGenFramebuffers, GLsizei, GLuint*)
G(void, glBindFramebuffer, GLenum, GLuint)
G(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)
G(void, glGenTextures, GLsizei, GLuint*)
G(void, glBindTexture, GLenum, GLuint)
G(void, glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)
G(void, glTexParameteri, GLenum, GLenum, GLint)
G(void, glEnable, GLenum)
G(void, glDisable, GLenum)
G(void, glScissor, GLint, GLint, GLsizei, GLsizei)
G(void, glDrawArrays, GLenum, GLint, GLsizei)
G(GLuint, glCreateShader, GLenum)
G(void, glShaderSource, GLuint, GLsizei, const char* const*, const GLint*)
G(void, glCompileShader, GLuint)
G(GLuint, glCreateProgram, void)
G(void, glAttachShader, GLuint, GLuint)
G(void, glLinkProgram, GLuint)
G(void, glUseProgram, GLuint)
G(void, glGenVertexArrays, GLsizei, GLuint*)
G(void, glBindVertexArray, GLuint)
G(void, glGenBuffers, GLsizei, GLuint*)
G(void, glBindBuffer, GLenum, GLuint)
G(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum)
G(GLint, glGetUniformLocation, GLuint, const char*)
G(void, glUniform2i, GLint, GLint, GLint)
#undef G
}  // namespace P

static int g_w = 0, g_h = 0;
static GLuint g_fbo = 0, g_tex = 0;
static bool g_resized = true;

static void OnFb(GLFWwindow*, int w, int h) {
    printf("harness: framebuffer callback %dx%d\n", w, h);
    fflush(stdout);
    g_w = w;
    g_h = h;
    g_resized = true;
}
static void OnKey(GLFWwindow*, int key, int, int action, int) {
    printf("harness: key %d action %d\n", key, action);
    fflush(stdout);
}
static void OnCursor(GLFWwindow*, double x, double y) {
    printf("harness: cursor %.1f %.1f\n", x, y);
    fflush(stdout);
}

// Fill a rectangle given in top-left game coordinates.
static void Rect(int x, int y, int w, int h, float r, float g, float b) {
    P::glScissor(x, g_h - y - h, w, h);
    P::glClearColor(r, g, b, 1);
    P::glClear(GL_COLOR_BUFFER_BIT);
}

int main() {
    void* lib = dlopen("libglfw.so.3", RTLD_NOW);
    if (!lib) return 1;
#define L(name) name = reinterpret_cast<decltype(name)>(dlsym(lib, #name))
    L(glfwInit); L(glfwWindowHint); L(glfwCreateWindow); L(glfwMakeContextCurrent); L(glfwSwapBuffers); L(glfwPollEvents);
    L(glfwWindowShouldClose); L(glfwGetFramebufferSize); L(glfwSetFramebufferSizeCallback); L(glfwSetKeyCallback);
    L(glfwSetCursorPosCallback); L(glfwSetWindowPos); L(glfwSwapInterval); L(glfwSetInputMode);
#undef L
    void* libgl = dlopen("libGL.so.1", RTLD_NOW);
    gpa = reinterpret_cast<GPA>(dlsym(libgl, "glXGetProcAddressARB"));
    if (!glfwInit()) return 2;
    glfwWindowHint(0x00020005 /* GLFW_DECORATED */, 0);
    GLFWwindow* win = glfwCreateWindow(1920, 1080, "ts-harness", nullptr, nullptr);
    glfwSetWindowPos(win, 0, 0);
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(win, OnFb);
    glfwSetKeyCallback(win, OnKey);
    glfwSetCursorPosCallback(win, OnCursor);
    if (getenv("HARNESS_GRAB")) glfwSetInputMode(win, 0x00033001 /* GLFW_CURSOR */, 0x00034003 /* DISABLED */);
#define L(name) P::name = reinterpret_cast<decltype(P::name)>(gpa(reinterpret_cast<const unsigned char*>(#name)))
    L(glViewport); L(glClearColor); L(glClear); L(glGenFramebuffers); L(glBindFramebuffer); L(glFramebufferTexture2D);
    L(glGenTextures); L(glBindTexture); L(glTexImage2D); L(glTexParameteri); L(glEnable); L(glDisable); L(glScissor);
    L(glDrawArrays); L(glCreateShader); L(glShaderSource); L(glCompileShader); L(glCreateProgram); L(glAttachShader);
    L(glLinkProgram); L(glUseProgram); L(glGenVertexArrays); L(glBindVertexArray); L(glGenBuffers); L(glBindBuffer);
    L(glBufferData); L(glGetUniformLocation); L(glUniform2i);
#undef L
    glfwGetFramebufferSize(win, &g_w, &g_h);
    printf("harness: initial framebuffer %dx%d\n", g_w, g_h);

    const char* vs = "#version 330 core\nout vec2 uv;\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}";
    const char* fs = "#version 330 core\nin vec2 uv;out vec4 c;uniform sampler2D t;void main(){c=texture(t,uv);}";
    GLuint v = P::glCreateShader(GL_VERTEX_SHADER), f = P::glCreateShader(GL_FRAGMENT_SHADER);
    P::glShaderSource(v, 1, &vs, nullptr);
    P::glShaderSource(f, 1, &fs, nullptr);
    P::glCompileShader(v);
    P::glCompileShader(f);
    GLuint prog = P::glCreateProgram();
    P::glAttachShader(prog, v);
    P::glAttachShader(prog, f);
    P::glLinkProgram(prog);
    GLuint vao;
    P::glGenVertexArrays(1, &vao);
    // HARNESS_EYETEST: every pixel within 64 columns of the centre encodes its own game position
    // (R = x - w/2 + 128, G = y low byte, B = y high byte, top-left origin) so a screenshot of a zoom
    // can be decoded back to the exact game pixel each screen pixel shows.
    const bool eyeTest = getenv("HARNESS_EYETEST") != nullptr;
    const char* efs = "#version 330 core\nout vec4 c;uniform ivec2 s;void main(){int x=int(gl_FragCoord.x);int y=s.y-1-int(gl_FragCoord.y);"
                      "c=vec4(float(x-s.x/2+128)/255.0,float(y&255)/255.0,float((y>>8)&255)/255.0,1);}";
    GLuint ef = P::glCreateShader(GL_FRAGMENT_SHADER);
    P::glShaderSource(ef, 1, &efs, nullptr);
    P::glCompileShader(ef);
    GLuint eprog = P::glCreateProgram();
    P::glAttachShader(eprog, v);
    P::glAttachShader(eprog, ef);
    P::glLinkProgram(eprog);

    int frames = 0;
    const char* maxFramesEnv = getenv("HARNESS_FRAMES");
    int maxFrames = maxFramesEnv ? atoi(maxFramesEnv) : 600;
    while (!glfwWindowShouldClose(win) && frames++ < maxFrames) {
        glfwPollEvents();
        if (g_resized) {
            g_resized = false;
            if (!g_fbo) {
                P::glGenFramebuffers(1, &g_fbo);
                P::glGenTextures(1, &g_tex);
            }
            P::glBindTexture(GL_TEXTURE_2D, g_tex);
            P::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_w, g_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            P::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            P::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            P::glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
            P::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0);
        }
        // "World" render into the game framebuffer.
        P::glBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        P::glViewport(0, 0, g_w, g_h);
        P::glEnable(GL_SCISSOR_TEST);
        Rect(0, 0, g_w, g_h, 0.25f, 0.45f, 0.25f);
        // Stripes every 10 px across the middle (visible in EyeZoom), a 1 px crosshair at the centre.
        for (int x = 0; x < g_w; x += 20) Rect(x, g_h / 2 - 400, 10, 800, 0.3f, 0.3f, 0.6f);
        Rect(g_w / 2 - 1, g_h / 2 - 1, 2, 2, 1, 1, 1);
        // Fake dragon boss bar at GUI scale 2: 364x10 at the top centre, 24 px down (pink fill, dark rest).
        Rect(g_w / 2 - 182, 24, 364, 10, 0.25f, 0.05f, 0.2f);
        Rect(g_w / 2 - 182, 24, 250, 10, 0.93f, 0.0f, 0.73f);
        // Exact-centre markers: red row at y = h/2, yellow column at x = w/2.
        Rect(0, g_h / 2, g_w, 1, 1, 0, 0);
        Rect(g_w / 2, g_h / 2 - 300, 1, 600, 1, 1, 0);
        // F3 entity counter area: colour 221,221,221 at (14,38).
        Rect(14, 38, 12, 7, 221 / 255.f, 221 / 255.f, 221 / 255.f);
        // Pie chart "blockentities" colour 233,109,77 near pieRight.
        Rect(g_w - 36, g_h - 220, 19, 4, 233 / 255.f, 109 / 255.f, 77 / 255.f);
        if (eyeTest) {
            P::glScissor(g_w / 2 - 64, 0, 128, g_h);
            P::glUseProgram(eprog);
            P::glUniform2i(P::glGetUniformLocation(eprog, "s"), g_w, g_h);
            P::glBindVertexArray(vao);
            P::glDrawArrays(GL_TRIANGLES, 0, 3);
        }
        P::glDisable(GL_SCISSOR_TEST);
        // Present like Minecraft's Framebuffer.draw.
        P::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        P::glViewport(0, 0, g_w, g_h);
        P::glUseProgram(prog);
        P::glBindVertexArray(vao);
        P::glBindTexture(GL_TEXTURE_2D, g_tex);
        P::glDrawArrays(GL_TRIANGLES, 0, 3);
        if (getenv("HARNESS_BIND_PBO")) {
            // Like some mods: leave a pixel-unpack buffer bound at the end of the frame.
            static GLuint pbo = 0;
            if (!pbo) {
                P::glGenBuffers(1, &pbo);
                P::glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
                P::glBufferData(GL_PIXEL_UNPACK_BUFFER, 64 << 20, nullptr, GL_STREAM_DRAW);
            }
            P::glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
        }
        glfwSwapBuffers(win);
    }
    return 0;
}
