// Overlay renderer: runs inside glfwSwapBuffers, after the game has presented its frame into the mode's
// screen rectangle. Draws the letterbox background, mode border, EyeZoom, mirrors, images and text, then
// restores every piece of GL state it touched.
#include "ts.h"

#include <GL/gl.h>
#include <GL/glext.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <map>
#include <thread>
#include <unordered_map>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#include "../third_party/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"

void* TsGetGLProc(const char* name);
extern "C" const unsigned char ts_font_ttf[];
extern "C" const unsigned int ts_font_ttf_size;

namespace {

// ---------------------------------------------------------------------------
// GL entry points used by the renderer (always the real driver functions)
// ---------------------------------------------------------------------------
#define TS_GL_FUNCS(X)                                                                                  \
    X(void, GetIntegerv, GLenum, GLint*)                                                                \
    X(GLboolean, IsEnabled, GLenum)                                                                     \
    X(void, Enable, GLenum)                                                                             \
    X(void, Disable, GLenum)                                                                            \
    X(void, Viewport, GLint, GLint, GLsizei, GLsizei)                                                   \
    X(void, Scissor, GLint, GLint, GLsizei, GLsizei)                                                    \
    X(void, BlendFuncSeparate, GLenum, GLenum, GLenum, GLenum)                                          \
    X(void, BlendEquationSeparate, GLenum, GLenum)                                                      \
    X(void, ColorMask, GLboolean, GLboolean, GLboolean, GLboolean)                                      \
    X(void, DepthMask, GLboolean)                                                                       \
    X(void, BindFramebuffer, GLenum, GLuint)                                                            \
    X(void, UseProgram, GLuint)                                                                         \
    X(void, BindVertexArray, GLuint)                                                                    \
    X(void, BindBuffer, GLenum, GLuint)                                                                 \
    X(void, ActiveTexture, GLenum)                                                                      \
    X(void, BindTexture, GLenum, GLuint)                                                                \
    X(void, BindSampler, GLuint, GLuint)                                                                \
    X(void, GenTextures, GLsizei, GLuint*)                                                              \
    X(void, DeleteTextures, GLsizei, const GLuint*)                                                     \
    X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)     \
    X(void, TexParameteri, GLenum, GLenum, GLint)                                                       \
    X(void, PixelStorei, GLenum, GLint)                                                                 \
    X(void, GetTexLevelParameteriv, GLenum, GLint, GLenum, GLint*)                                      \
    X(void, CopyTexSubImage2D, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)             \
    X(void, GenFramebuffers, GLsizei, GLuint*)                                                          \
    X(void, DeleteFramebuffers, GLsizei, const GLuint*)                                                 \
    X(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)                                \
    X(GLenum, CheckFramebufferStatus, GLenum)                                                           \
    X(void, ReadBuffer, GLenum)                                                                         \
    X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat)                                             \
    X(void, Clear, GLbitfield)                                                                          \
    X(void, GetFloatv, GLenum, GLfloat*)                                                                \
    X(GLuint, CreateShader, GLenum)                                                                     \
    X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)                          \
    X(void, CompileShader, GLuint)                                                                      \
    X(void, GetShaderiv, GLuint, GLenum, GLint*)                                                        \
    X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                       \
    X(GLuint, CreateProgram)                                                                            \
    X(void, AttachShader, GLuint, GLuint)                                                               \
    X(void, BindAttribLocation, GLuint, GLuint, const GLchar*)                                          \
    X(void, LinkProgram, GLuint)                                                                        \
    X(void, GetProgramiv, GLuint, GLenum, GLint*)                                                       \
    X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)                                      \
    X(void, DeleteShader, GLuint)                                                                       \
    X(GLint, GetUniformLocation, GLuint, const GLchar*)                                                 \
    X(void, Uniform1i, GLint, GLint)                                                                    \
    X(void, Uniform1f, GLint, GLfloat)                                                                  \
    X(void, Uniform2f, GLint, GLfloat, GLfloat)                                                         \
    X(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat)                                       \
    X(void, Uniform1fv, GLint, GLsizei, const GLfloat*)                                                 \
    X(void, Uniform3fv, GLint, GLsizei, const GLfloat*)                                                 \
    X(void, Uniform4fv, GLint, GLsizei, const GLfloat*)                                                 \
    X(void, GenVertexArrays, GLsizei, GLuint*)                                                          \
    X(void, GenBuffers, GLsizei, GLuint*)                                                               \
    X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum)                                        \
    X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)                \
    X(void, EnableVertexAttribArray, GLuint)                                                            \
    X(void, DrawArrays, GLenum, GLint, GLsizei)                                                         \
    X(const GLubyte*, GetString, GLenum)

struct Gl {
#define X(ret, name, ...) ret(APIENTRY* name)(__VA_ARGS__) = nullptr;
    TS_GL_FUNCS(X)
#undef X
} gl;

bool LoadGl() {
    bool ok = true;
#define X(ret, name, ...)                                                                         \
    gl.name = reinterpret_cast<ret(APIENTRY*)(__VA_ARGS__)>(TsGetGLProc("gl" #name));            \
    if (!gl.name && std::string(#name) != "BindSampler") {                                       \
        TsLog("missing GL function gl%s", #name);                                                 \
        ok = false;                                                                               \
    }
    TS_GL_FUNCS(X)
#undef X
    return ok;
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------
const char* kVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform vec2 uTarget;   // target size in pixels
uniform int uTopLeft;   // 1: aPos has a top-left origin, 0: bottom-left (GL)
out vec2 vUV;
out vec2 vLocal;
void main() {
    float y = uTopLeft == 1 ? 1.0 - aPos.y / uTarget.y * 2.0 : aPos.y / uTarget.y * 2.0 - 1.0;
    gl_Position = vec4(aPos.x / uTarget.x * 2.0 - 1.0, y, 0.0, 1.0);
    vUV = aUV;
    vLocal = aUV;
})";

const char* kSolidFrag = R"(#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() { FragColor = uColor; })";

// Textured quad with optional colour keys (image overlays) and opacity.
const char* kImageFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D uTex;
uniform float uOpacity;
uniform int uKeyCount;
uniform vec3 uKeys[8];
uniform float uKeySens[8];
uniform int uForceOpaque;
void main() {
    vec4 c = texture(uTex, vUV);
    for (int i = 0; i < uKeyCount; i++) {
        if (distance(pow(c.rgb, vec3(2.2)), pow(uKeys[i], vec3(2.2))) < uKeySens[i]) discard;
    }
    if (uForceOpaque == 1) c.a = 1.0;
    FragColor = vec4(c.rgb, c.a * uOpacity);
})";

// Font atlas text: coverage in the red channel.
const char* kTextFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
void main() { FragColor = vec4(uColor.rgb, uColor.a * texture(uTex, vUV).r); })";

// Background gradient, ported from Toolscreen's background shader.
const char* kGradientFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 vUV;
#define MAX_STOPS 8
uniform int u_numStops;
uniform vec4 u_stopColors[MAX_STOPS];
uniform float u_stopPositions[MAX_STOPS];
uniform float u_angle;
uniform float u_time;
uniform int u_animationType;
uniform float u_animationSpeed;
uniform int u_colorFade;
vec4 seamless(float t) {
    t = fract(t);
    float lastPos = u_stopPositions[u_numStops - 1];
    float firstPos = u_stopPositions[0];
    float wrapSize = (1.0 - lastPos) + firstPos;
    if (t <= firstPos && wrapSize > 0.001) return mix(u_stopColors[0], u_stopColors[u_numStops - 1], (firstPos - t) / wrapSize);
    if (t >= lastPos && wrapSize > 0.001) return mix(u_stopColors[u_numStops - 1], u_stopColors[0], (t - lastPos) / wrapSize);
    vec4 color = u_stopColors[0];
    for (int i = 0; i < u_numStops - 1; i++) {
        if (t >= u_stopPositions[i] && t <= u_stopPositions[i + 1]) {
            color = mix(u_stopColors[i], u_stopColors[i + 1], (t - u_stopPositions[i]) / max(u_stopPositions[i + 1] - u_stopPositions[i], 0.0001));
            break;
        }
    }
    return color;
}
vec4 grad(float t, float timeOffset) {
    float a = t;
    if (u_colorFade == 1) a = fract(t + timeOffset * 0.1);
    a = clamp(a, 0.0, 1.0);
    vec4 color = u_stopColors[0];
    for (int i = 0; i < u_numStops - 1; i++) {
        if (a >= u_stopPositions[i] && a <= u_stopPositions[i + 1]) {
            color = mix(u_stopColors[i], u_stopColors[i + 1], (a - u_stopPositions[i]) / max(u_stopPositions[i + 1] - u_stopPositions[i], 0.0001));
            break;
        }
    }
    if (a >= u_stopPositions[u_numStops - 1]) color = u_stopColors[u_numStops - 1];
    return color;
}
vec4 fadeColor(float timeOffset) { return seamless(timeOffset * 0.1); }
void main() {
    vec2 uv = vUV - vec2(0.5);
    float timeOffset = u_time * u_animationSpeed;
    vec2 dir = vec2(cos(u_angle), sin(u_angle));
    if (u_animationType == 1) {
        float ang = u_angle + timeOffset;
        FragColor = grad(clamp(dot(uv, vec2(cos(ang), sin(ang))) + 0.5, 0.0, 1.0), timeOffset);
    } else if (u_animationType == 2) {
        FragColor = seamless(dot(uv, dir) + 0.5 + timeOffset * 0.2);
    } else if (u_animationType == 3) {
        vec2 perp = vec2(-sin(u_angle), cos(u_angle));
        float wave = sin(dot(uv, perp) * 8.0 + timeOffset * 2.0) * 0.08;
        FragColor = grad(clamp(dot(uv, dir) + 0.5 + wave, 0.0, 1.0), timeOffset);
    } else if (u_animationType == 4) {
        float t = length(uv) * 2.0 + atan(uv.y, uv.x) / 6.28318 - timeOffset * 0.3;
        FragColor = seamless(t);
    } else if (u_animationType == 5) {
        FragColor = fadeColor(timeOffset);
    } else {
        FragColor = grad(clamp(dot(uv, dir) + 0.5, 0.0, 1.0), timeOffset);
    }
})";

// Mirror capture: colour filter over the game texture (ported from Toolscreen's mirror thread).
const char* kFilterFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D screenTexture;
uniform int u_gammaMode;
uniform vec3 u_targetColors[8];
uniform int u_targetColorCount;
uniform vec4 outputColor;
uniform float u_sensitivity;
uniform int u_passthrough;  // 1: matched pixels keep their own colour
uniform int u_raw;          // 1: plain copy
vec3 SRGBToLinear(vec3 c) {
    bvec3 cutoff = lessThanEqual(c, vec3(0.04045));
    return mix(pow((c + 0.055) / 1.055, vec3(2.4)), c / 12.92, vec3(cutoff));
}
void main() {
    vec3 screenColor = texture(screenTexture, vUV).rgb;
    if (u_raw == 1) { FragColor = vec4(screenColor, 1.0); return; }
    vec3 screenLinear = SRGBToLinear(screenColor);
    bool matches = false;
    for (int i = 0; i < u_targetColorCount; i++) {
        vec3 t = u_targetColors[i];
        vec3 tl = SRGBToLinear(t);
        float dist;
        if (u_gammaMode == 2) dist = distance(screenColor, tl);
        else if (u_gammaMode == 1) dist = distance(screenLinear, tl);
        else dist = min(distance(screenColor, t), distance(screenLinear, tl));
        if (dist < u_sensitivity) { matches = true; break; }
    }
    if (matches) FragColor = u_passthrough == 1 ? vec4(screenColor, 1.0) : outputColor;
    else FragColor = vec4(0.0);
})";

// Mirror output: scaled filter texture with a dynamic outline border.
const char* kMirrorOutFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform sampler2D filterTexture;
uniform int u_borderWidth;
uniform vec4 u_borderColor;
uniform vec2 u_screenPixel;
uniform float u_opacity;
uniform int u_raw;
void main() {
    vec4 c = texture(filterTexture, vUV);
    if (u_raw == 1) { FragColor = vec4(c.rgb, u_opacity); return; }
    if (c.a > 0.001) { FragColor = vec4(c.rgb, c.a * u_opacity); return; }
    float maxA = 0.0;
    for (int x = -u_borderWidth; x <= u_borderWidth; x++) {
        for (int y = -u_borderWidth; y <= u_borderWidth; y++) {
            if (x == 0 && y == 0) continue;
            maxA = max(maxA, texture(filterTexture, vUV + vec2(x, y) * u_screenPixel).a);
        }
    }
    if (maxA > 0.001) FragColor = vec4(u_borderColor.rgb, u_borderColor.a * u_opacity);
    else discard;
})";

// Static mirror border / mode border: rounded box or ellipse outline (SDF), ported from Toolscreen.
const char* kOutlineFrag = R"(#version 330 core
out vec4 FragColor;
in vec2 vUV;
uniform int u_shape;
uniform vec4 u_borderColor;
uniform float u_thickness;
uniform float u_radius;
uniform vec2 u_size;
uniform vec2 u_quadSize;
float sdRoundedBox(vec2 p, vec2 b, float r) {
    r = clamp(r, 0.0, min(b.x, b.y));
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float sdEllipse(vec2 p, vec2 ab) {
    vec2 pn = p / ab;
    float len = length(pn);
    if (len < 0.0001) return -min(ab.x, ab.y);
    vec2 g = pn / (ab * len);
    return (len - 1.0) / length(g);
}
void main() {
    vec2 p = vUV * u_quadSize - u_quadSize * 0.5;
    vec2 halfSize = max(u_size * 0.5, vec2(1.0));
    float dist = u_shape == 0 ? sdRoundedBox(p, halfSize, u_radius) : sdEllipse(p, halfSize);
    if (dist >= -0.5 && dist <= u_thickness + 0.5) FragColor = u_borderColor;
    else discard;
})";

struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> locs;
    GLint loc(const char* n) {
        auto it = locs.find(n);
        if (it != locs.end()) return it->second;
        GLint l = gl.GetUniformLocation(id, n);
        locs[n] = l;
        return l;
    }
};

Program g_solid, g_image, g_text, g_gradient, g_filter, g_mirrorOut, g_outline;
GLuint g_vao = 0, g_vbo = 0;
bool g_ready = false, g_failed = false, g_compat = false;

GLuint Compile(GLenum type, const char* src) {
    GLuint s = gl.CreateShader(type);
    gl.ShaderSource(s, 1, &src, nullptr);
    gl.CompileShader(s);
    GLint ok = 0;
    gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
        TsLog("shader compile failed: %s", log);
    }
    return s;
}

bool Link(Program& p, const char* frag) {
    GLuint vs = Compile(GL_VERTEX_SHADER, kVert);
    GLuint fs = Compile(GL_FRAGMENT_SHADER, frag);
    p.id = gl.CreateProgram();
    gl.AttachShader(p.id, vs);
    gl.AttachShader(p.id, fs);
    gl.BindAttribLocation(p.id, 0, "aPos");
    gl.BindAttribLocation(p.id, 1, "aUV");
    gl.LinkProgram(p.id);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(p.id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        gl.GetProgramInfoLog(p.id, sizeof(log), nullptr, log);
        TsLog("program link failed: %s", log);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Saved GL state
// ---------------------------------------------------------------------------
struct SavedState {
    GLint drawFb, readFb, viewport[4], scissorBox[4], program, vao, arrayBuffer, activeTex, tex2d, sampler;
    GLint blendSrcRgb, blendDstRgb, blendSrcA, blendDstA, blendEqRgb, blendEqA;
    GLint unpackAlign, unpackRowLength, unpackSkipRows, unpackSkipPixels, unpackBuffer, readBuffer;
    GLboolean scissor, blend, depth, stencil, cull, srgb, alphaTest, colorMask[4], depthMask;
    GLfloat clearColor[4];
};

void Save(SavedState& s) {
    gl.GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.drawFb);
    gl.GetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.readFb);
    gl.GetIntegerv(GL_VIEWPORT, s.viewport);
    gl.GetIntegerv(GL_SCISSOR_BOX, s.scissorBox);
    gl.GetIntegerv(GL_CURRENT_PROGRAM, &s.program);
    gl.GetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    gl.GetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    gl.GetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTex);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.GetIntegerv(GL_TEXTURE_BINDING_2D, &s.tex2d);
    s.sampler = 0;
    if (gl.BindSampler) gl.GetIntegerv(GL_SAMPLER_BINDING, &s.sampler);
    gl.GetIntegerv(GL_BLEND_SRC_RGB, &s.blendSrcRgb);
    gl.GetIntegerv(GL_BLEND_DST_RGB, &s.blendDstRgb);
    gl.GetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcA);
    gl.GetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstA);
    gl.GetIntegerv(GL_BLEND_EQUATION_RGB, &s.blendEqRgb);
    gl.GetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blendEqA);
    gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &s.unpackAlign);
    gl.GetIntegerv(GL_UNPACK_ROW_LENGTH, &s.unpackRowLength);
    gl.GetIntegerv(GL_UNPACK_SKIP_ROWS, &s.unpackSkipRows);
    gl.GetIntegerv(GL_UNPACK_SKIP_PIXELS, &s.unpackSkipPixels);
    gl.GetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &s.unpackBuffer);
    gl.GetIntegerv(GL_READ_BUFFER, &s.readBuffer);
    s.scissor = gl.IsEnabled(GL_SCISSOR_TEST);
    s.blend = gl.IsEnabled(GL_BLEND);
    s.depth = gl.IsEnabled(GL_DEPTH_TEST);
    s.stencil = gl.IsEnabled(GL_STENCIL_TEST);
    s.cull = gl.IsEnabled(GL_CULL_FACE);
    s.srgb = gl.IsEnabled(GL_FRAMEBUFFER_SRGB);
    s.alphaTest = g_compat ? gl.IsEnabled(0x0BC0 /* GL_ALPHA_TEST */) : GL_FALSE;
    GLint cm[4];
    gl.GetIntegerv(GL_COLOR_WRITEMASK, cm);
    for (int i = 0; i < 4; i++) s.colorMask[i] = cm[i] ? GL_TRUE : GL_FALSE;
    GLint dm;
    gl.GetIntegerv(GL_DEPTH_WRITEMASK, &dm);
    s.depthMask = dm ? GL_TRUE : GL_FALSE;
    gl.GetFloatv(GL_COLOR_CLEAR_VALUE, s.clearColor);
}

void SetCap(GLenum cap, GLboolean on) { on ? gl.Enable(cap) : gl.Disable(cap); }

void Restore(const SavedState& s) {
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, s.drawFb);
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, s.readFb);
    gl.ReadBuffer(s.readBuffer);
    gl.Viewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
    gl.Scissor(s.scissorBox[0], s.scissorBox[1], s.scissorBox[2], s.scissorBox[3]);
    gl.UseProgram(s.program);
    gl.BindVertexArray(s.vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, s.arrayBuffer);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, s.tex2d);
    if (gl.BindSampler) gl.BindSampler(0, s.sampler);
    gl.ActiveTexture(s.activeTex);
    gl.BlendFuncSeparate(s.blendSrcRgb, s.blendDstRgb, s.blendSrcA, s.blendDstA);
    gl.BlendEquationSeparate(s.blendEqRgb, s.blendEqA);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, s.unpackAlign);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, s.unpackRowLength);
    gl.PixelStorei(GL_UNPACK_SKIP_ROWS, s.unpackSkipRows);
    gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, s.unpackSkipPixels);
    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, s.unpackBuffer);
    SetCap(GL_SCISSOR_TEST, s.scissor);
    SetCap(GL_BLEND, s.blend);
    SetCap(GL_DEPTH_TEST, s.depth);
    SetCap(GL_STENCIL_TEST, s.stencil);
    SetCap(GL_CULL_FACE, s.cull);
    SetCap(GL_FRAMEBUFFER_SRGB, s.srgb);
    if (g_compat) SetCap(0x0BC0, s.alphaTest);
    gl.ColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
    gl.DepthMask(s.depthMask);
    gl.ClearColor(s.clearColor[0], s.clearColor[1], s.clearColor[2], s.clearColor[3]);
}

// ---------------------------------------------------------------------------
// Drawing helpers. Screen coordinates are pixels with a top-left origin.
// ---------------------------------------------------------------------------
int g_targetW = 1, g_targetH = 1;
bool g_targetTopLeft = true;

void UseProgram(Program& p) {
    gl.UseProgram(p.id);
    gl.Uniform2f(p.loc("uTarget"), static_cast<float>(g_targetW), static_cast<float>(g_targetH));
    gl.Uniform1i(p.loc("uTopLeft"), g_targetTopLeft ? 1 : 0);
}

// Quad from (x,y) size (w,h); uv (u0,v0) at the first corner and (u1,v1) at the opposite corner.
void Quad(float x, float y, float w, float h, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    float verts[] = {
        x, y, u0, v0, x + w, y, u1, v0, x + w, y + h, u1, v1, x, y, u0, v0, x + w, y + h, u1, v1, x, y + h, u0, v1,
    };
    gl.BufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, 6);
}

void FillRect(float x, float y, float w, float h, const Color& c, float alphaMul = 1) {
    if (w <= 0 || h <= 0) return;
    UseProgram(g_solid);
    gl.Uniform4f(g_solid.loc("uColor"), c.r, c.g, c.b, c.a * alphaMul);
    Quad(x, y, w, h);
}

// Outline drawn outside (thickness>0) the box at (x,y,w,h); shape 0 = rounded box, 1 = ellipse.
void Outline(float x, float y, float w, float h, float thickness, float radius, int shape, const Color& c, float alphaMul = 1) {
    if (thickness <= 0) return;
    float ext = thickness + 1;
    float qx = x - ext, qy = y - ext, qw = w + 2 * ext, qh = h + 2 * ext;
    UseProgram(g_outline);
    gl.Uniform1i(g_outline.loc("u_shape"), shape);
    gl.Uniform4f(g_outline.loc("u_borderColor"), c.r, c.g, c.b, c.a * alphaMul);
    gl.Uniform1f(g_outline.loc("u_thickness"), thickness);
    gl.Uniform1f(g_outline.loc("u_radius"), radius);
    gl.Uniform2f(g_outline.loc("u_size"), w, h);
    gl.Uniform2f(g_outline.loc("u_quadSize"), qw, qh);
    Quad(qx, qy, qw, qh);
}

void SetTexFilter(bool nearest) {
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

// ---------------------------------------------------------------------------
// Font (stb_truetype atlas)
// ---------------------------------------------------------------------------
constexpr float kBakePx = 64.0f;
constexpr int kAtlasW = 1024, kAtlasH = 512;
stbtt_bakedchar g_glyphs[96];
GLuint g_fontTex = 0;
std::vector<unsigned char> g_fontData;

void InitFont() {
    std::string path = g_rt.cfg.eyezoom.textFontPath;
    if (path.empty()) path = g_rt.cfg.fontPath;
    g_fontData.clear();
    if (!path.empty() && path.find('\\') == std::string::npos) {
        if (FILE* f = fopen(path.c_str(), "rb")) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            g_fontData.resize(n);
            if (fread(g_fontData.data(), 1, n, f) != static_cast<size_t>(n)) g_fontData.clear();
            fclose(f);
        }
    }
    if (g_fontData.empty()) g_fontData.assign(ts_font_ttf, ts_font_ttf + ts_font_ttf_size);
    std::vector<unsigned char> bitmap(kAtlasW * kAtlasH);
    if (stbtt_BakeFontBitmap(g_fontData.data(), 0, kBakePx, bitmap.data(), kAtlasW, kAtlasH, 32, 96, g_glyphs) <= 0 &&
        g_fontData.data() != ts_font_ttf) {
        g_fontData.assign(ts_font_ttf, ts_font_ttf + ts_font_ttf_size);
        stbtt_BakeFontBitmap(g_fontData.data(), 0, kBakePx, bitmap.data(), kAtlasW, kAtlasH, 32, 96, g_glyphs);
    }
    if (!g_fontTex) gl.GenTextures(1, &g_fontTex);
    gl.BindTexture(GL_TEXTURE_2D, g_fontTex);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, kAtlasW, kAtlasH, 0, GL_RED, GL_UNSIGNED_BYTE, bitmap.data());
    SetTexFilter(false);
}

float TextWidth(const std::string& s, float px) {
    float x = 0;
    for (unsigned char ch : s) {
        if (ch < 32 || ch >= 128) ch = '?';
        x += g_glyphs[ch - 32].xadvance;
    }
    return x * px / kBakePx;
}

// Draws text with its top-left at (x,y); px is the font pixel height.
void DrawText(const std::string& s, float x, float y, float px, const Color& c) {
    if (s.empty()) return;
    UseProgram(g_text);
    gl.Uniform4f(g_text.loc("uColor"), c.r, c.g, c.b, c.a);
    gl.BindTexture(GL_TEXTURE_2D, g_fontTex);
    float scale = px / kBakePx;
    float penX = 0, baseline = kBakePx * 0.8f;
    std::vector<float> verts;
    verts.reserve(s.size() * 24);
    for (unsigned char ch : s) {
        if (ch < 32 || ch >= 128) ch = '?';
        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(g_glyphs, kAtlasW, kAtlasH, ch - 32, &penX, &baseline, &q, 1);
        float x0 = x + q.x0 * scale, y0 = y + q.y0 * scale, x1 = x + q.x1 * scale, y1 = y + q.y1 * scale;
        float quad[] = {x0, y0, q.s0, q.t0, x1, y0, q.s1, q.t0, x1, y1, q.s1, q.t1, x0, y0, q.s0, q.t0, x1, y1, q.s1, q.t1, x0, y1, q.s0, q.t1};
        verts.insert(verts.end(), std::begin(quad), std::end(quad));
    }
    gl.BufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STREAM_DRAW);
    gl.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 4));
}

// ---------------------------------------------------------------------------
// Images (decoded on a worker thread, uploaded on the render thread, reloaded when the file changes)
// ---------------------------------------------------------------------------
struct ImageEntry {
    GLuint tex = 0;
    int w = 0, h = 0;
    struct timespec mtime = {0, 0};
    off_t size = -1;
    double lastCheck = 0;
    bool loading = false;
    std::vector<unsigned char> pending;  // decoded RGBA awaiting upload
    int pendingW = 0, pendingH = 0;
    bool pendingReady = false;
    bool pendingVisible = false;
    bool visible = false;  // has at least one non-transparent pixel
};

std::mutex g_imgMutex;
std::condition_variable g_imgCv;
std::map<std::string, ImageEntry> g_images;
std::vector<std::string> g_imgQueue;

void ImageWorker() {
    while (true) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(g_imgMutex);
            g_imgCv.wait(lock, [] { return !g_imgQueue.empty(); });
            path = g_imgQueue.back();
            g_imgQueue.pop_back();
        }
        int w = 0, h = 0, n = 0;
        unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
        std::lock_guard<std::mutex> lock(g_imgMutex);
        ImageEntry& e = g_images[path];
        e.loading = false;
        if (px) {
            e.pending.assign(px, px + static_cast<size_t>(w) * h * 4);
            e.pendingW = w;
            e.pendingH = h;
            e.pendingReady = true;
            e.pendingVisible = false;
            for (size_t i = 3; i < e.pending.size(); i += 4) {
                if (e.pending[i] != 0) {
                    e.pendingVisible = true;
                    break;
                }
            }
            stbi_image_free(px);
        }
    }
}

// Returns the texture for a path (0 while it is not loaded yet).
ImageEntry* GetImage(const std::string& path) {
    if (path.empty()) return nullptr;
    static bool workerStarted = false;
    if (!workerStarted) {
        workerStarted = true;
        std::thread(ImageWorker).detach();
    }
    std::lock_guard<std::mutex> lock(g_imgMutex);
    ImageEntry& e = g_images[path];
    double now = NowSeconds();
    if (now - e.lastCheck > 0.25 && !e.loading) {
        e.lastCheck = now;
        struct stat st;
        if (stat(path.c_str(), &st) == 0 &&
            (st.st_mtim.tv_sec != e.mtime.tv_sec || st.st_mtim.tv_nsec != e.mtime.tv_nsec || st.st_size != e.size)) {
            e.mtime = st.st_mtim;
            e.size = st.st_size;
            e.loading = true;
            g_imgQueue.push_back(path);
            g_imgCv.notify_one();
        }
    }
    if (e.pendingReady) {
        e.pendingReady = false;
        if (!e.tex) gl.GenTextures(1, &e.tex);
        gl.BindTexture(GL_TEXTURE_2D, e.tex);
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl.PixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        gl.PixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, e.pendingW, e.pendingH, 0, GL_RGBA, GL_UNSIGNED_BYTE, e.pending.data());
        e.w = e.pendingW;
        e.h = e.pendingH;
        e.visible = e.pendingVisible;
        std::vector<unsigned char>().swap(e.pending);
    }
    return e.tex ? &e : nullptr;
}

// ---------------------------------------------------------------------------
// Game frame source: the game's own colour texture when known, else a copy of the presented frame.
// ---------------------------------------------------------------------------
GLuint g_gameTex = 0;
std::unordered_map<unsigned, std::pair<int, int>> g_texSizes;
GLuint g_fallbackTex = 0;
int g_fallbackW = 0, g_fallbackH = 0;

struct FrameSource {
    GLuint tex = 0;
    int texW = 0, texH = 0;
    // Mapping from game pixels (top-left origin) to texture pixels (top-left origin).
    float originX = 0, originY = 0, scaleX = 1, scaleY = 1;
} g_src;

bool PrepareFrameSource() {
    Runtime& rt = g_rt;
    g_src = FrameSource{};
    if (g_gameTex) {
        GLint w = 0, h = 0;
        gl.BindTexture(GL_TEXTURE_2D, g_gameTex);
        gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
        gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
        if (w == rt.gameW && h == rt.gameH) {
            g_src.tex = g_gameTex;
            g_src.texW = w;
            g_src.texH = h;
            return true;
        }
    }
    // Fallback: copy the on-screen part of the presented frame.
    int sx0 = std::max(rt.destX, 0), sy0 = std::max(rt.destY, 0);
    int sx1 = std::min(rt.destX + rt.destW, rt.screenW), sy1 = std::min(rt.destY + rt.destH, rt.screenH);
    int w = sx1 - sx0, h = sy1 - sy0;
    if (w <= 0 || h <= 0) return false;
    if (!g_fallbackTex) gl.GenTextures(1, &g_fallbackTex);
    gl.BindTexture(GL_TEXTURE_2D, g_fallbackTex);
    if (w != g_fallbackW || h != g_fallbackH) {
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        g_fallbackW = w;
        g_fallbackH = h;
    }
    gl.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl.ReadBuffer(GL_BACK);
    gl.CopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sx0, rt.screenH - sy1, w, h);
    g_src.tex = g_fallbackTex;
    g_src.texW = w;
    g_src.texH = h;
    g_src.scaleX = static_cast<float>(rt.destW) / std::max(rt.gameW, 1);
    g_src.scaleY = static_cast<float>(rt.destH) / std::max(rt.gameH, 1);
    g_src.originX = (sx0 - rt.destX) / g_src.scaleX;
    g_src.originY = (sy0 - rt.destY) / g_src.scaleY;
    return true;
}

// Game rectangle (top-left origin) -> GL texture coordinates (u0, vBottom, u1, vTop).
void GameRectUV(float gx, float gy, float w, float h, float& u0, float& vb, float& u1, float& vt) {
    float tx0 = (gx - g_src.originX) * g_src.scaleX, ty0 = (gy - g_src.originY) * g_src.scaleY;
    float tx1 = tx0 + w * g_src.scaleX, ty1 = ty0 + h * g_src.scaleY;
    u0 = tx0 / g_src.texW;
    u1 = tx1 / g_src.texW;
    vt = 1.0f - ty0 / g_src.texH;
    vb = 1.0f - ty1 / g_src.texH;
}

// ---------------------------------------------------------------------------
// Anchors (identical maths to Toolscreen's GetRelativeCoords)
// ---------------------------------------------------------------------------
std::string StripAnchor(const std::string& a) {
    if (a.size() > 8 && a.compare(a.size() - 8, 8, "Viewport") == 0) return a.substr(0, a.size() - 8);
    if (a.size() > 6 && a.compare(a.size() - 6, 6, "Screen") == 0) return a.substr(0, a.size() - 6);
    return a;
}

void RelativeCoords(const std::string& type, int relX, int relY, int w, int h, int cw, int ch, int& outX, int& outY) {
    std::string anchor = StripAnchor(type);
    char c = anchor.empty() ? '\0' : anchor[0];
    if (c == 't') {
        outY = relY;
        outX = (anchor == "topLeft") ? relX : cw - w - relX;
    } else if (c == 'c') {
        outX = (cw - w) / 2 + relX;
        outY = (ch - h) / 2 + relY;
    } else if (c == 'p') {
        int baseX = (anchor == "pieLeft") ? cw - 92 : cw - 36;
        outX = baseX + relX;
        outY = ch - 220 + relY;
    } else {
        outY = ch - h - relY;
        outX = (anchor == "bottomRight") ? cw - w - relX : relX;
    }
}

void ImageCoords(const std::string& type, int relX, int relY, int w, int h, int& outX, int& outY) {
    const Runtime& rt = g_rt;
    bool viewport = type.size() > 8 && type.compare(type.size() - 8, 8, "Viewport") == 0;
    std::string a = StripAnchor(type);
    int cw = viewport ? rt.destW : rt.screenW, ch = viewport ? rt.destH : rt.screenH;
    int ax = 0, ay = 0;
    char c = a.empty() ? '\0' : a[0];
    if (c == 't') {
        ax = (a == "topLeft") ? 0 : cw - w;
    } else if (c == 'c') {
        ax = (cw - w) / 2;
        ay = (ch - h) / 2;
    } else if (c == 'b') {
        ax = (a == "bottomLeft") ? 0 : cw - w;
        ay = ch - h;
    }
    outX = ax + relX + (viewport ? rt.destX : 0);
    outY = ay + relY + (viewport ? rt.destY : 0);
}

// ---------------------------------------------------------------------------
// Background and border
// ---------------------------------------------------------------------------
void DrawBackgroundFill(const BackgroundConfig& bg) {
    const Runtime& rt = g_rt;
    float sw = static_cast<float>(rt.screenW), sh = static_cast<float>(rt.screenH);
    if (bg.selectedMode == "gradient" && bg.stops.size() >= 2) {
        UseProgram(g_gradient);
        int n = static_cast<int>(std::min<size_t>(bg.stops.size(), 8));
        float cols[32], pos[8];
        for (int i = 0; i < n; i++) {
            cols[i * 4 + 0] = bg.stops[i].color.r;
            cols[i * 4 + 1] = bg.stops[i].color.g;
            cols[i * 4 + 2] = bg.stops[i].color.b;
            cols[i * 4 + 3] = bg.stops[i].color.a;
            pos[i] = bg.stops[i].position;
        }
        gl.Uniform1i(g_gradient.loc("u_numStops"), n);
        gl.Uniform4fv(g_gradient.loc("u_stopColors"), n, cols);
        gl.Uniform1fv(g_gradient.loc("u_stopPositions"), n, pos);
        gl.Uniform1f(g_gradient.loc("u_angle"), bg.angleDeg * 3.14159265f / 180.0f);
        gl.Uniform1f(g_gradient.loc("u_time"), static_cast<float>(NowSeconds() - rt.startTime));
        gl.Uniform1i(g_gradient.loc("u_animationType"), static_cast<int>(bg.animation));
        gl.Uniform1f(g_gradient.loc("u_animationSpeed"), bg.animationSpeed);
        gl.Uniform1i(g_gradient.loc("u_colorFade"), bg.colorFade ? 1 : 0);
        Quad(0, 0, sw, sh, 0, 1, 1, 0);
        return;
    }
    FillRect(0, 0, sw, sh, bg.color);
    if (bg.selectedMode == "image" && !bg.image.empty()) {
        if (ImageEntry* img = GetImage(bg.image)) {
            UseProgram(g_image);
            gl.BindTexture(GL_TEXTURE_2D, img->tex);
            SetTexFilter(false);
            gl.Uniform1f(g_image.loc("uOpacity"), 1);
            gl.Uniform1i(g_image.loc("uKeyCount"), 0);
            gl.Uniform1i(g_image.loc("uForceOpaque"), 0);
            // Cover the screen while keeping the aspect ratio.
            float scale = std::max(sw / img->w, sh / img->h);
            float w = img->w * scale, h = img->h * scale;
            Quad((sw - w) / 2, (sh - h) / 2, w, h);
        }
    }
}

void DrawLetterbox(const ModeConfig& mode) {
    const Runtime& rt = g_rt;
    int x0 = std::max(rt.destX, 0), y0 = std::max(rt.destY, 0);
    int x1 = std::min(rt.destX + rt.destW, rt.screenW), y1 = std::min(rt.destY + rt.destH, rt.screenH);
    if (x0 == 0 && y0 == 0 && x1 == rt.screenW && y1 == rt.screenH) return;
    // Four strips around the game rectangle, in GL scissor coordinates.
    struct Strip {
        int x, y, w, h;
    } strips[4] = {
        {0, rt.screenH - y0, rt.screenW, y0},             // top
        {0, 0, rt.screenW, rt.screenH - y1},              // bottom
        {0, rt.screenH - y1, x0, y1 - y0},                // left
        {x1, rt.screenH - y1, rt.screenW - x1, y1 - y0},  // right
    };
    gl.Enable(GL_SCISSOR_TEST);
    for (auto& s : strips) {
        if (s.w <= 0 || s.h <= 0) continue;
        gl.Scissor(s.x, s.y, s.w, s.h);
        DrawBackgroundFill(mode.background);
    }
    gl.Disable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------------------
// EyeZoom
// ---------------------------------------------------------------------------
void DrawEyeZoom(const ModeConfig& mode) {
    const Runtime& rt = g_rt;
    const EyeZoomConfig& ez = rt.cfg.eyezoom;
    int zoomW, zoomH, zoomX, zoomY;
    if (ez.useCustomSizePosition) {
        zoomW = ez.zoomAreaWidth;
        zoomH = ez.zoomAreaHeight;
        zoomX = ez.positionX;
        zoomY = ez.positionY;
    } else {
        int vx = rt.destX;
        int margin = vx > 0 ? vx / 10 : 0;
        int vMargin = rt.screenH / 8;
        zoomW = vx - 2 * margin;
        zoomH = rt.screenH - 2 * vMargin;
        zoomX = margin;
        zoomY = (rt.screenH - zoomH) / 2;
    }
    zoomW = std::min(zoomW, rt.screenW);
    if (zoomW <= 20 || zoomH <= 0 || !g_src.tex) return;

    // Source: cloneWidth x cloneHeight game pixels centred on the crosshair.
    float cw = static_cast<float>(ez.cloneWidth), ch = static_cast<float>(ez.cloneHeight);
    float srcX = rt.gameW / 2.0f - static_cast<float>(ez.cloneWidth / 2);
    float srcY = rt.gameH / 2.0f - ch / 2.0f;
    float u0, vb, u1, vt;
    GameRectUV(srcX, srcY, cw, ch, u0, vb, u1, vt);
    UseProgram(g_image);
    gl.BindTexture(GL_TEXTURE_2D, g_src.tex);
    SetTexFilter(true);
    gl.Uniform1f(g_image.loc("uOpacity"), 1);
    gl.Uniform1i(g_image.loc("uKeyCount"), 0);
    gl.Uniform1i(g_image.loc("uForceOpaque"), 1);
    Quad(static_cast<float>(zoomX), static_cast<float>(zoomY), static_cast<float>(zoomW), static_cast<float>(zoomH), u0, vt, u1, vb);

    // Numbered boxes either side of the centre line.
    gl.Enable(GL_SCISSOR_TEST);
    gl.Scissor(zoomX, rt.screenH - zoomY - zoomH, zoomW, zoomH);
    float pw = zoomW / cw;
    int labelsPerSide = ez.cloneWidth / 2;
    int perSide = ez.overlayWidth < 0 ? labelsPerSide : std::min(ez.overlayWidth, labelsPerSide);

    float fontPx = static_cast<float>(ez.textFontSize);
    if (ez.fontSizeMode != "manual") {
        // One shared size fitting the widest label in 82% of a box.
        std::string widest = std::to_string(std::max(perSide, 1));
        float w1 = TextWidth(widest, 1.0f);
        fontPx = w1 > 0 ? pw * 0.82f / w1 : fontPx;
    }
    float boxH = ez.linkRectToFont ? fontPx * 1.2f : static_cast<float>(ez.rectHeight);
    float centerY = zoomY + zoomH / 2.0f;
    for (int xo = -perSide; xo <= perSide; xo++) {
        if (xo == 0) continue;
        int boxIndex = xo + labelsPerSide - (xo > 0 ? 1 : 0);
        float left = zoomX + boxIndex * pw;
        FillRect(left, centerY - boxH / 2, pw, boxH, (boxIndex % 2 == 0) ? ez.gridColor1 : ez.gridColor2);
    }
    for (int xo = -perSide; xo <= perSide; xo++) {
        if (xo == 0) continue;
        int boxIndex = xo + labelsPerSide - (xo > 0 ? 1 : 0);
        float left = zoomX + boxIndex * pw;
        std::string label = std::to_string(std::abs(xo));
        float px = fontPx;
        if (ez.fontSizeMode == "perSquareAuto") {
            float w1 = TextWidth(label, 1.0f);
            px = w1 > 0 ? pw * 0.82f / w1 : px;
        }
        float tw = TextWidth(label, px);
        DrawText(label, left + (pw - tw) / 2, centerY - px * 0.5f, px, ez.textColor);
    }
    float cx = zoomX + zoomW / 2.0f;
    FillRect(cx - 1, static_cast<float>(zoomY), 2, static_cast<float>(zoomH), ez.centerLineColor);
    gl.Disable(GL_SCISSOR_TEST);

    if (mode.border.enabled) {
        Outline(static_cast<float>(zoomX), static_cast<float>(zoomY), static_cast<float>(zoomW), static_cast<float>(zoomH),
                static_cast<float>(mode.border.width), static_cast<float>(mode.border.radius), 0, mode.border.color);
    }
}

// ---------------------------------------------------------------------------
// Mirrors
// ---------------------------------------------------------------------------
struct MirrorInstance {
    GLuint fbo = 0, tex = 0;
    int w = 0, h = 0;
    double lastCapture = -1;
    int lastGameW = 0, lastGameH = 0;
};
std::map<std::string, MirrorInstance> g_mirrors;
std::map<std::string, bool> g_capturedThisFrame;

const MirrorConfig* FindMirror(const std::string& name) {
    for (auto& m : g_rt.cfg.mirrors) {
        if (m.name == name) return &m;
    }
    return nullptr;
}

int GammaMode() {
    const std::string& s = g_rt.cfg.mirrorMatchColorspace;
    if (s == "sRGB") return 1;
    if (s == "Linear") return 2;
    return 0;
}

void CaptureMirror(const MirrorConfig& m, MirrorInstance& inst) {
    const Runtime& rt = g_rt;
    int bt = m.border.isStatic ? 0 : m.border.dynamicThickness;
    int fw = m.captureWidth + 2 * bt, fh = m.captureHeight + 2 * bt;
    if (!inst.fbo) {
        gl.GenFramebuffers(1, &inst.fbo);
        gl.GenTextures(1, &inst.tex);
    }
    if (inst.w != fw || inst.h != fh) {
        gl.BindTexture(GL_TEXTURE_2D, inst.tex);
        gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, fw, fh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        SetTexFilter(true);
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, inst.fbo);
        gl.FramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, inst.tex, 0);
        inst.w = fw;
        inst.h = fh;
    }
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, inst.fbo);
    gl.Viewport(0, 0, fw, fh);
    gl.ClearColor(0, 0, 0, 0);
    gl.Clear(GL_COLOR_BUFFER_BIT);

    int savedW = g_targetW, savedH = g_targetH;
    bool savedTL = g_targetTopLeft;
    g_targetW = fw;
    g_targetH = fh;
    g_targetTopLeft = false;

    UseProgram(g_filter);
    gl.BindTexture(GL_TEXTURE_2D, g_src.tex);
    SetTexFilter(true);
    float colors[24] = {0};
    int n = static_cast<int>(m.targetColors.size());
    for (int i = 0; i < n; i++) {
        colors[i * 3 + 0] = m.targetColors[i].r;
        colors[i * 3 + 1] = m.targetColors[i].g;
        colors[i * 3 + 2] = m.targetColors[i].b;
    }
    gl.Uniform1i(g_filter.loc("u_gammaMode"), GammaMode());
    gl.Uniform3fv(g_filter.loc("u_targetColors"), 8, colors);
    gl.Uniform1i(g_filter.loc("u_targetColorCount"), n);
    gl.Uniform4f(g_filter.loc("outputColor"), m.outputColor.r, m.outputColor.g, m.outputColor.b, 1);
    gl.Uniform1f(g_filter.loc("u_sensitivity"), m.colorSensitivity);
    gl.Uniform1i(g_filter.loc("u_passthrough"), m.colorPassthrough ? 1 : 0);
    gl.Uniform1i(g_filter.loc("u_raw"), m.rawOutput ? 1 : 0);
    // Later inputs are layered over earlier ones; unmatched pixels are transparent.
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (auto& in : m.inputs) {
        int cx, cy;
        RelativeCoords(in.relativeTo, in.x, in.y, m.captureWidth, m.captureHeight, rt.gameW, rt.gameH, cx, cy);
        float u0, vb, u1, vt;
        GameRectUV(static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(m.captureWidth),
                   static_cast<float>(m.captureHeight), u0, vb, u1, vt);
        Quad(static_cast<float>(bt), static_cast<float>(bt), static_cast<float>(m.captureWidth), static_cast<float>(m.captureHeight), u0,
             vb, u1, vt);
    }
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    g_targetW = savedW;
    g_targetH = savedH;
    g_targetTopLeft = savedTL;
    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, rt.screenW, rt.screenH);
}

// Draws one mirror with an output placement (already resolved for groups).
void DrawMirror(const MirrorConfig& m, const MirrorOutput& out) {
    const Runtime& rt = g_rt;
    MirrorInstance& inst = g_mirrors[m.name];
    double now = NowSeconds();
    bool due = m.fps <= 0 || inst.lastCapture < 0 || now - inst.lastCapture >= 1.0 / m.fps || inst.lastGameW != rt.gameW ||
               inst.lastGameH != rt.gameH;
    if (due && !g_capturedThisFrame[m.name]) {
        CaptureMirror(m, inst);
        inst.lastCapture = now;
        inst.lastGameW = rt.gameW;
        inst.lastGameH = rt.gameH;
        g_capturedThisFrame[m.name] = true;
    }
    if (!inst.tex) return;

    float sx = out.separateScale ? out.scaleX : out.scale;
    float sy = out.separateScale ? out.scaleY : out.scale;
    int outW = static_cast<int>(inst.w * sx), outH = static_cast<int>(inst.h * sy);
    if (outW <= 0 || outH <= 0) return;
    int ox = out.x, oy = out.y;
    if (out.useRelativePosition) {
        ox = static_cast<int>(out.relativeX * rt.screenW);
        oy = static_cast<int>(out.relativeY * rt.screenH);
    }
    int x, y;
    const std::string& anchor = out.relativeTo;
    if (anchor.size() > 6 && anchor.compare(anchor.size() - 6, 6, "Screen") == 0) {
        RelativeCoords(anchor, ox, oy, outW, outH, rt.screenW, rt.screenH, x, y);
    } else {
        RelativeCoords(anchor, ox, oy, outW, outH, rt.destW, rt.destH, x, y);
        x += rt.destX;
        y += rt.destY;
    }

    UseProgram(g_mirrorOut);
    gl.BindTexture(GL_TEXTURE_2D, inst.tex);
    SetTexFilter(true);
    gl.Uniform1i(g_mirrorOut.loc("u_borderWidth"), m.border.isStatic ? 0 : m.border.dynamicThickness);
    gl.Uniform4f(g_mirrorOut.loc("u_borderColor"), m.borderColor.r, m.borderColor.g, m.borderColor.b, 1);
    gl.Uniform2f(g_mirrorOut.loc("u_screenPixel"), 1.0f / outW, 1.0f / outH);
    gl.Uniform1f(g_mirrorOut.loc("u_opacity"), m.opacity);
    gl.Uniform1i(g_mirrorOut.loc("u_raw"), m.rawOutput ? 1 : 0);
    Quad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(outW), static_cast<float>(outH), 0, 1, 1, 0);

    if (m.border.isStatic && m.border.staticThickness > 0) {
        int bw = m.border.staticWidth > 0 ? m.border.staticWidth : outW;
        int bh = m.border.staticHeight > 0 ? m.border.staticHeight : outH;
        float bx = static_cast<float>(x - (bw - outW) / 2 + m.border.staticOffsetX);
        float by = static_cast<float>(y - (bh - outH) / 2 + m.border.staticOffsetY);
        // The SDF ring sits just outside the box edge, matching Toolscreen's static border quad.
        Outline(bx, by, static_cast<float>(bw), static_cast<float>(bh), static_cast<float>(m.border.staticThickness),
                static_cast<float>(m.border.staticRadius), m.border.staticCircle ? 1 : 0, m.border.staticColor, m.opacity);
    }
}

void DrawMirrorGroup(const MirrorGroupConfig& g) {
    const Runtime& rt = g_rt;
    int gx = g.output.x, gy = g.output.y;
    if (g.output.useRelativePosition) {
        gx = static_cast<int>(g.output.relativeX * rt.screenW);
        gy = static_cast<int>(g.output.relativeY * rt.screenH);
    }
    for (auto& item : g.items) {
        if (!item.enabled) continue;
        const MirrorConfig* m = FindMirror(item.mirrorId);
        if (!m) continue;
        MirrorOutput out = g.output;
        out.useRelativePosition = false;
        out.x = gx + item.offsetX;
        out.y = gy + item.offsetY;
        float bsx = m->outputScaleAppliesInGroups ? (m->output.separateScale ? m->output.scaleX : m->output.scale) : 1.0f;
        float bsy = m->outputScaleAppliesInGroups ? (m->output.separateScale ? m->output.scaleY : m->output.scale) : 1.0f;
        out.separateScale = true;
        out.scaleX = bsx * item.widthPercent;
        out.scaleY = bsy * item.heightPercent;
        DrawMirror(*m, out);
    }
}

// ---------------------------------------------------------------------------
// Image overlays
// ---------------------------------------------------------------------------
void DrawImageOverlay(const ImageConfig& im) {
    if (!g_rt.imagesVisible) return;
    if (im.name == "Ninjabrain Bot" && !g_rt.ninjabrainVisible) return;
    ImageEntry* img = GetImage(im.path);
    // A fully transparent image (e.g. Ninjabrain Bot with nothing to show) draws nothing, background included.
    if (!img || img->w <= 0 || !img->visible) return;
    int cw = img->w - im.cropLeft - im.cropRight, ch = img->h - im.cropTop - im.cropBottom;
    if (cw <= 0 || ch <= 0) return;
    int dw = static_cast<int>(cw * im.scale), dh = static_cast<int>(ch * im.scale);
    if (!im.relativeSizing && im.width > 0 && im.height > 0) {
        dw = im.width;
        dh = im.height;
    }
    int x, y;
    ImageCoords(im.relativeTo, im.x, im.y, dw, dh, x, y);
    if (im.bgEnabled) FillRect(static_cast<float>(x), static_cast<float>(y), static_cast<float>(dw), static_cast<float>(dh), im.bgColor, im.bgOpacity);

    UseProgram(g_image);
    gl.BindTexture(GL_TEXTURE_2D, img->tex);
    SetTexFilter(im.pixelatedScaling);
    gl.Uniform1f(g_image.loc("uOpacity"), im.opacity);
    gl.Uniform1i(g_image.loc("uForceOpaque"), 0);
    int nk = im.enableColorKey ? static_cast<int>(std::min<size_t>(im.colorKeys.size(), 8)) : 0;
    float keys[24] = {0}, sens[8] = {0};
    for (int i = 0; i < nk; i++) {
        keys[i * 3 + 0] = im.colorKeys[i].color.r;
        keys[i * 3 + 1] = im.colorKeys[i].color.g;
        keys[i * 3 + 2] = im.colorKeys[i].color.b;
        sens[i] = im.colorKeys[i].sensitivity;
    }
    gl.Uniform1i(g_image.loc("uKeyCount"), nk);
    gl.Uniform3fv(g_image.loc("uKeys"), 8, keys);
    gl.Uniform1fv(g_image.loc("uKeySens"), 8, sens);
    float u0 = static_cast<float>(im.cropLeft) / img->w, u1 = static_cast<float>(img->w - im.cropRight) / img->w;
    float v0 = static_cast<float>(im.cropTop) / img->h, v1 = static_cast<float>(img->h - im.cropBottom) / img->h;
    Quad(static_cast<float>(x), static_cast<float>(y), static_cast<float>(dw), static_cast<float>(dh), u0, v0, u1, v1);

    if (im.border.enabled) {
        Outline(static_cast<float>(x), static_cast<float>(y), static_cast<float>(dw), static_cast<float>(dh),
                static_cast<float>(im.border.width), static_cast<float>(im.border.radius), 0, im.border.color);
    }
}

// ---------------------------------------------------------------------------
// Ninjabrain Bot panel (data from the Ninjabrain Bot API client)
// ---------------------------------------------------------------------------
Color Faded(Color c, float a) {
    c.a *= a;
    return c;
}

Color Gradient3(double p, const Color& low, const Color& mid, const Color& high) {
    float t = static_cast<float>(std::clamp(p, 0.0, 1.0));
    const Color& a = t < 0.5f ? low : mid;
    const Color& b = t < 0.5f ? mid : high;
    float k = t < 0.5f ? t * 2 : (t - 0.5f) * 2;
    return Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, 1};
}

struct NbCell {
    std::string text, text2;  // text2 is drawn right after text in its own colour
    Color color, color2;
};

std::string Fmt(const char* fmt, double a, double b = 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, a, b);
    return buf;
}

double NormalizeDeg(double a) {
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    return a;
}

bool DrawNinjabrainPanelImpl();

void DrawNinjabrainPanel() { g_rt.nbDisplayed = DrawNinjabrainPanelImpl(); }

bool DrawNinjabrainPanelImpl() {
    Runtime& rt = g_rt;
    const NinjabrainOverlayConfig& nb = rt.cfg.ninjabrain;
    if (!nb.enabled) return false;
    if (!nb.allowedModes.empty() &&
        std::find(nb.allowedModes.begin(), nb.allowedModes.end(), rt.currentMode) == nb.allowedModes.end()) {
        return false;
    }
    // Track when the current world was entered, so only measurements taken in it count. Entering a world
    // also clears the hotkey's show/hide choice.
    static double worldEnteredAt = 0;
    static bool wasInWorld = false;
    std::string state = GetGameState();
    bool inWorld = state.empty() || state.rfind("inworld", 0) == 0;
    if (inWorld && !wasInWorld) {
        worldEnteredAt = NowSeconds();
        rt.nbOverride = 0;
    }
    wasInWorld = inWorld;
    if (nb.hideInMenus && !inWorld) return false;
    if (rt.nbOverride < 0) return false;
    bool forced = rt.nbOverride > 0;

    NinjabrainData d = NinjabrainSnapshot();
    if (nb.showOnlyAfterMeasurement && !forced) {
        double since = state.empty() ? 0 : worldEnteredAt;
        if (d.lastMeasurement <= 0 || d.lastMeasurement < since) return false;
    }
    bool hasResult = d.resultType != "NONE" && d.resultType != "";
    bool hasData = hasResult || d.hasBlindResult || !d.messages.empty();
    if (!d.connected && !hasData) return false;
    if (!hasData && !nb.alwaysShow && !forced) return false;
    if (nb.hideIfStale && hasData && !forced && NowSeconds() - d.lastUpdate > nb.hideIfStaleDelaySeconds) return false;

    const float op = nb.overlayOpacity;
    const float sc = nb.overlayScale > 0.01f ? nb.overlayScale : 1.0f;
    const float fs = 64.0f * sc;
    const float lineH = fs * 1.3f;
    const float padX = 10.0f * sc * 2;
    const float colGap = 36.0f * sc;

    // Prediction table cells.
    std::vector<const NinjabrainColumn*> cols;
    for (auto& c : nb.columns) {
        if (c.show && (c.id == "coords" || c.id == "certainty" || c.id == "distance" || c.id == "nether" || c.id == "angle")) {
            cols.push_back(&c);
        }
    }
    bool failed = d.resultType == "FAILED";
    bool table = !failed && (d.resultType == "TRIANGULATION" || !d.predictions.empty() || (!hasResult && nb.alwaysShow));
    int rows = table ? nb.shownPredictions : 0;
    std::vector<std::vector<NbCell>> cells(cols.size(), std::vector<NbCell>(rows));
    for (size_t ci = 0; ci < cols.size(); ci++) {
        const std::string& id = cols[ci]->id;
        for (int r = 0; r < rows && r < static_cast<int>(d.predictions.size()); r++) {
            const NbPrediction& p = d.predictions[r];
            NbCell& cell = cells[ci][r];
            cell.color = nb.dataColor;
            if (id == "coords") {
                int x = nb.coordsDisplay == "chunk" ? p.chunkX : p.chunkX * 16 + 4;
                int z = nb.coordsDisplay == "chunk" ? p.chunkZ : p.chunkZ * 16 + 4;
                cell.text = "(" + std::to_string(x) + ", ";
                cell.text2 = std::to_string(z) + ")";
                cell.color = x < 0 ? nb.coordNegativeColor : nb.coordPositiveColor;
                cell.color2 = z < 0 ? nb.coordNegativeColor : nb.coordPositiveColor;
            } else if (id == "certainty") {
                cell.text = Fmt("%.1f%%", p.certainty * 100.0);
                cell.color = Gradient3(p.certainty, nb.certaintyLowColor, nb.certaintyMidColor, nb.certaintyColor);
            } else if (id == "distance") {
                cell.text = std::to_string(static_cast<int>(std::floor(p.overworldDistance)));
            } else if (id == "nether") {
                cell.text = "(" + std::to_string(p.chunkX * 2) + ", " + std::to_string(p.chunkZ * 2) + ")";
            } else if (id == "angle") {
                if (d.hasPlayer) {
                    double bx = p.chunkX * 16.0 + 4.0, bz = p.chunkZ * 16.0 + 4.0;
                    double angle = -std::atan2(bx - d.playerX, bz - d.playerZ) * 180.0 / M_PI;
                    double need = NormalizeDeg(angle - d.playerAngle);
                    cell.text = Fmt("%.2f", angle);
                    if (nb.showDirectionToStronghold) {
                        cell.text += " ";
                        cell.text2 = Fmt(need > 0 ? "(-> %.1f)" : "(<- %.1f)", std::fabs(need));
                        cell.color2 = Gradient3(1.0 - std::fabs(need) / 180.0, nb.certaintyLowColor, nb.certaintyMidColor,
                                                nb.certaintyColor);
                    }
                } else if (!d.throws.empty()) {
                    cell.text = Fmt("%.2f", d.throws.back().angle);
                }
            }
        }
    }
    std::vector<float> colW(cols.size(), 0);
    for (size_t ci = 0; ci < cols.size(); ci++) {
        colW[ci] = cols[ci]->staticWidth > 0 ? cols[ci]->staticWidth * sc : TextWidth(cols[ci]->header, fs);
        for (auto& cell : cells[ci]) colW[ci] = std::max(colW[ci], TextWidth(cell.text + cell.text2, fs));
        if (cols[ci]->id == "coords") colW[ci] = std::max(colW[ci], TextWidth("(-9999, -9999)", fs));
        if (cols[ci]->id == "angle" && nb.showDirectionToStronghold) colW[ci] = std::max(colW[ci], TextWidth("-179.99 (-> 179.9)", fs));
    }

    // Extra lines below the table.
    struct Line {
        std::vector<NbCell> parts;
        bool header = false;
    };
    std::vector<Line> extra;
    if (failed) extra.push_back({{{"Could not determine the stronghold chunk.", "", nb.textColor, {}}}, false});
    if (d.hasBlindResult) {
        std::string coords = "(" + std::to_string(static_cast<int>(d.blindX)) + ", " + std::to_string(static_cast<int>(d.blindZ)) + ")";
        extra.push_back({{{"Blind coords " + coords + "  " + d.blindEvaluation, "", nb.textColor, {}}}, false});
        extra.push_back({{{Fmt("%.1f%% chance of <%.0f block blind", d.blindHighroll * 100.0, d.blindHighrollThreshold), "", nb.dataColor, {}}},
                         false});
    }
    // Eye throws: x, z, angle, error.
    std::vector<std::vector<NbCell>> throwRows;
    if (nb.showThrowDetails && !d.throws.empty() && nb.eyeThrowRows > 0) {
        size_t first = d.throws.size() > static_cast<size_t>(nb.eyeThrowRows) ? d.throws.size() - nb.eyeThrowRows : 0;
        for (size_t i = first; i < d.throws.size(); i++) {
            const NbThrow& t = d.throws[i];
            NbCell ang{Fmt("%.2f", t.angleWithoutCorrection), "", nb.throwsTextColor, {}};
            int inc = t.hasIncrements ? t.correctionIncrements : static_cast<int>(std::lround(t.correction / 0.01));
            if (inc != 0) {
                ang.text2 = (inc > 0 ? "+" : "") + std::to_string(inc);
                ang.color2 = inc > 0 ? nb.subpixelPositiveColor : nb.subpixelNegativeColor;
            }
            throwRows.push_back({{Fmt("%.2f", t.x), "", nb.throwsTextColor, {}},
                                 {Fmt("%.2f", t.z), "", nb.throwsTextColor, {}},
                                 ang,
                                 {Fmt("%.4f", t.error), "", nb.throwsTextColor, {}}});
        }
    }
    for (auto& m : d.messages) {
        Color c = m.severity == "WARNING" ? nb.certaintyMidColor : (m.severity == "ERROR" ? nb.certaintyLowColor : nb.textColor);
        extra.push_back({{{m.message, "", c, {}}}, false});
    }
    std::string boat;
    if (d.boatState != "NONE" && (nb.showBoatStateInTopBar || nb.alwaysShowBoat)) boat = d.boatState;

    // Size.
    float tableW = 0;
    for (float w : colW) tableW += w;
    if (!colW.empty()) tableW += colGap * (colW.size() - 1);
    const char* throwHeaders[4] = {"x", "z", "Angle", "Error"};
    float throwW[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; k++) {
        throwW[k] = TextWidth(throwHeaders[k], fs);
        for (auto& r : throwRows) throwW[k] = std::max(throwW[k], TextWidth(r[k].text + r[k].text2, fs));
    }
    float throwsW = throwRows.empty() ? 0 : throwW[0] + throwW[1] + throwW[2] + throwW[3] + colGap * 3;
    float contentW = std::max(tableW, throwsW);
    for (auto& l : extra) contentW = std::max(contentW, TextWidth(l.parts[0].text, fs));
    float titleH = (nb.showTitleBar || !boat.empty()) ? lineH : 0;
    float h = titleH + (rows > 0 ? lineH * (rows + 1) : 0) + (throwRows.empty() ? 0 : lineH * (throwRows.size() + 1)) + lineH * extra.size();
    float w = contentW + 2 * padX;
    if (h <= 0) return false;
    int px, py;
    ImageCoords(nb.relativeTo, nb.x, nb.y, static_cast<int>(w), static_cast<int>(h), px, py);
    float x0 = static_cast<float>(px), y = static_cast<float>(py);

    auto textAt = [&](const NbCell& c, float tx, float ty) {
        DrawText(c.text, tx, ty + (lineH - fs) / 2, fs, Faded(c.color, op));
        if (!c.text2.empty()) DrawText(c.text2, tx + TextWidth(c.text, fs), ty + (lineH - fs) / 2, fs, Faded(c.color2, op));
    };
    if (nb.bgEnabled) FillRect(x0, y, w, h, nb.bgColor, nb.bgOpacity * op);
    if (titleH > 0) {
        FillRect(x0, y, w, titleH, nb.chromeColor, op);
        if (nb.showTitleBar) textAt({nb.titleText, "", nb.titleTextColor, {}}, x0 + padX, y);
        if (!boat.empty()) {
            Color bc = boat == "VALID" ? nb.certaintyColor : boat == "ERROR" ? nb.certaintyLowColor : nb.certaintyMidColor;
            std::string label = "Boat: " + boat;
            textAt({label, "", bc, {}}, x0 + w - padX - TextWidth(label, fs), y);
        }
        y += titleH;
    }
    if (rows > 0) {
        FillRect(x0, y, w, lineH, nb.headerFillColor, op);
        float cx = x0 + padX;
        for (size_t ci = 0; ci < cols.size(); ci++) {
            textAt({cols[ci]->header, "", nb.textColor, {}}, cx + (colW[ci] - TextWidth(cols[ci]->header, fs)) / 2, y);
            cx += colW[ci] + colGap;
        }
        y += lineH;
        for (int r = 0; r < rows; r++) {
            if (nb.showRowStripes && r % 2 == 1) FillRect(x0, y, w, lineH, Color{1, 1, 1, 0.04f}, op);
            if (nb.showSeparators) FillRect(x0, y, w, std::max(1.0f, sc * 2), nb.dividerColor, op);
            cx = x0 + padX;
            for (size_t ci = 0; ci < cols.size(); ci++) {
                const NbCell& c = cells[ci][r];
                float tw = TextWidth(c.text + c.text2, fs);
                float tx = cols[ci]->id == "angle" ? cx : cx + (colW[ci] - tw) / 2;
                textAt(c, tx, y);
                cx += colW[ci] + colGap;
            }
            y += lineH;
        }
    }
    if (!throwRows.empty()) {
        FillRect(x0, y, w, lineH * (throwRows.size() + 1), nb.throwsBackgroundColor, op);
        float cx = x0 + padX;
        for (int k = 0; k < 4; k++) {
            textAt({throwHeaders[k], "", nb.textColor, {}}, cx + (throwW[k] - TextWidth(throwHeaders[k], fs)) / 2, y);
            cx += throwW[k] + colGap;
        }
        y += lineH;
        for (auto& r : throwRows) {
            cx = x0 + padX;
            for (int k = 0; k < 4; k++) {
                textAt(r[k], cx + (throwW[k] - TextWidth(r[k].text + r[k].text2, fs)) / 2, y);
                cx += throwW[k] + colGap;
            }
            y += lineH;
        }
    }
    for (auto& l : extra) {
        textAt(l.parts[0], x0 + padX, y);
        y += lineH;
    }
    if (nb.borderWidth > 0) Outline(x0, static_cast<float>(py), w, h, static_cast<float>(nb.borderWidth), 0, 0, Faded(nb.borderColor, op));
    return true;
}

// ---------------------------------------------------------------------------
// Text overlays: toast and the Ctrl+I panel
// ---------------------------------------------------------------------------
std::string g_toast;
double g_toastUntil = 0;

std::string VkName(int vk) {
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x6F);
    switch (vk) {
    case 0x01: return "Mouse1";
    case 0x02: return "Mouse2";
    case 0x04: return "Mouse3";
    case 0x05: return "Mouse4";
    case 0x06: return "Mouse5";
    case 0x08: return "Backspace";
    case 0x09: return "Tab";
    case 0x0D: return "Enter";
    case 0x10: return "Shift";
    case 0x11: return "Ctrl";
    case 0x12: return "Alt";
    case 0x14: return "CapsLock";
    case 0x1B: return "Esc";
    case 0x20: return "Space";
    case 0xA0: return "LShift";
    case 0xA1: return "RShift";
    case 0xA2: return "LCtrl";
    case 0xA3: return "RCtrl";
    case 0xA4: return "LAlt";
    case 0xA5: return "RAlt";
    case 0xC0: return "`";
    case 0xBA: return ";";
    case 0xBB: return "=";
    case 0xBC: return ",";
    case 0xBD: return "-";
    case 0xBE: return ".";
    case 0xBF: return "/";
    case 0xDB: return "[";
    case 0xDC: return "\\";
    case 0xDD: return "]";
    case 0xDE: return "'";
    default: return "VK" + std::to_string(vk);
    }
}

std::string ComboName(const std::vector<int>& keys) {
    std::string s;
    for (size_t i = 0; i < keys.size(); i++) {
        if (i) s += "+";
        s += VkName(keys[i]);
    }
    return s.empty() ? "(none)" : s;
}

void DrawPanel(const std::vector<std::string>& lines, float x, float y, float px) {
    float w = 0;
    for (auto& l : lines) w = std::max(w, TextWidth(l, px));
    float lineH = px * 1.25f;
    float pad = px * 0.6f;
    FillRect(x, y, w + 2 * pad, lines.size() * lineH + 2 * pad, Color{0.07f, 0.06f, 0.11f, 0.88f});
    Outline(x, y, w + 2 * pad, lines.size() * lineH + 2 * pad, 2, 0, 0, Color{0.48f, 0.08f, 0.64f, 1});
    float ty = y + pad;
    for (size_t i = 0; i < lines.size(); i++) {
        Color c = i == 0 ? Color{0.85f, 0.7f, 1, 1} : Color{0.92f, 0.92f, 0.92f, 1};
        if (lines[i].rfind("Error", 0) == 0) c = Color{1, 0.45f, 0.45f, 1};
        DrawText(lines[i], x + pad, ty, px, c);
        ty += lineH;
    }
}

void DrawInfoPanel() {
    const Runtime& rt = g_rt;
    const Config& cfg = rt.cfg;
    std::vector<std::string> lines;
    lines.push_back("Toolscreen " TS_VERSION " for Linux");
    lines.push_back("Config: " + ConfigPath());
    std::string state = GetGameState();
    lines.push_back("Mode: " + rt.currentMode + "   Game " + std::to_string(rt.gameW) + "x" + std::to_string(rt.gameH) + "   Screen " +
                    std::to_string(rt.screenW) + "x" + std::to_string(rt.screenH) + "   State: " + (state.empty() ? "(no wpstateout.txt)" : state));
    if (!rt.cfgError.empty()) lines.push_back("Error: " + rt.cfgError);
    if (cfg.ninjabrain.enabled) {
        NinjabrainData nbd = NinjabrainSnapshot();
        lines.push_back(std::string("Ninjabrain Bot API: ") + (nbd.connected ? "connected" : "not connected - enable the API in Ninjabrain Bot's settings") +
                        "  (" + cfg.ninjabrain.apiBaseUrl + ")");
    } else {
        lines.push_back("Ninjabrain Bot panel: off ([ninjabrainOverlay] enabled = false)");
    }
    lines.push_back("");
    lines.push_back("Hotkeys:");
    for (auto& h : cfg.hotkeys) {
        std::string l = "  " + ComboName(h.keys) + "  ->  " + h.secondaryMode;
        if (h.triggerOnHold) l += " (hold)";
        if (h.triggerOnRelease) l += " (on release)";
        lines.push_back(l);
    }
    for (auto& s : cfg.sensitivityHotkeys) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.2f", s.sensitivity);
        lines.push_back("  " + ComboName(s.keys) + "  ->  sensitivity " + buf + (s.toggle ? " (toggle)" : " (hold)"));
    }
    if (!cfg.imageOverlaysHotkey.empty()) lines.push_back("  " + ComboName(cfg.imageOverlaysHotkey) + "  ->  toggle image overlays");
    if (!cfg.ninjabrainOverlayHotkey.empty()) lines.push_back("  " + ComboName(cfg.ninjabrainOverlayHotkey) + "  ->  toggle Ninjabrain overlay");
    lines.push_back("");
    lines.push_back("E  edit config.toml      O  open config folder");
    lines.push_back("R  reload config         " + ComboName(cfg.guiHotkey) + " / Esc  close");
    lines.push_back("The config reloads automatically when the file is saved.");
    float px = std::max(16.0f, rt.screenH / 50.0f);
    DrawPanel(lines, px, px, px);
}

void DrawToast() {
    double now = NowSeconds();
    if (now > g_toastUntil || g_toast.empty()) return;
    float px = std::max(14.0f, g_rt.screenH / 60.0f);
    DrawPanel({g_toast}, px * 0.5f, px * 0.5f, px);
}

}  // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------
void RenderNoteGameTextureAttach(unsigned int tex) { g_gameTex = tex; }

void RenderNoteTextureImage(unsigned int tex, int w, int h) { g_texSizes[tex] = {w, h}; }

void RenderNoteTextureDeleted(unsigned int tex) {
    g_texSizes.erase(tex);
    if (tex == g_gameTex) g_gameTex = 0;
}

void RenderShowToast(const std::string& text, double seconds) {
    g_toast = text;
    g_toastUntil = NowSeconds() + seconds;
}

void RenderInitIfNeeded() {
    if (g_ready || g_failed) return;
    if (!gl.GetIntegerv && !LoadGl()) {
        g_failed = true;
        return;
    }
    const char* ver = reinterpret_cast<const char*>(gl.GetString(GL_VERSION));
    const char* renderer = reinterpret_cast<const char*>(gl.GetString(GL_RENDERER));
    int major = ver ? atoi(ver) : 0;
    TsLog("OpenGL %s on %s", ver ? ver : "?", renderer ? renderer : "?");
    if (major < 3 || (major == 3 && ver[2] < '3')) {
        TsLog("OpenGL 3.3 is required for overlays; mode switching still works");
        g_failed = true;
        return;
    }
    GLint mask = 0;
    gl.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    g_compat = !(mask & GL_CONTEXT_CORE_PROFILE_BIT);
    bool ok = Link(g_solid, kSolidFrag) && Link(g_image, kImageFrag) && Link(g_text, kTextFrag) && Link(g_gradient, kGradientFrag) &&
              Link(g_filter, kFilterFrag) && Link(g_mirrorOut, kMirrorOutFrag) && Link(g_outline, kOutlineFrag);
    if (!ok) {
        g_failed = true;
        return;
    }
    gl.GenVertexArrays(1, &g_vao);
    gl.GenBuffers(1, &g_vbo);
    gl.BindVertexArray(g_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vbo);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    gl.EnableVertexAttribArray(1);
    for (Program* p : {&g_image, &g_text, &g_filter, &g_mirrorOut}) {
        gl.UseProgram(p->id);
        gl.Uniform1i(p->loc("uTex"), 0);
        gl.Uniform1i(p->loc("screenTexture"), 0);
        gl.Uniform1i(p->loc("filterTexture"), 0);
    }
    InitFont();
    g_ready = true;
    TsLog("renderer ready (%s profile)", g_compat ? "compatibility" : "core");
    if (g_rt.cfg.startupIndicatorMode != 0) {
        RenderShowToast("Toolscreen " TS_VERSION " loaded  -  " + ComboName(g_rt.cfg.guiHotkey) + " for info", 6.0);
    }
}

void RenderFrame() {
    Runtime& rt = g_rt;
    if (rt.screenW <= 0 || rt.screenH <= 0) return;
    const ModeConfig* mode = FindMode(rt.cfg, rt.currentMode);

    bool letterbox = rt.destX > 0 || rt.destY > 0 || rt.destX + rt.destW < rt.screenW || rt.destY + rt.destH < rt.screenH;
    bool eyezoom = mode && mode->id == "EyeZoom";
    bool haveSources = mode && !mode->sources.empty();
    bool text = rt.guiOpen || NowSeconds() < g_toastUntil || rt.cfg.ninjabrain.enabled;
    if (!letterbox && !eyezoom && !haveSources && !text && !(mode && mode->border.enabled) && g_ready) return;

    // Keep the font and config in step: a reloaded config may name a different font.
    static std::string fontKey;
    std::string wantFont = rt.cfg.eyezoom.textFontPath + "|" + rt.cfg.fontPath;

    // Save first: one-time initialisation binds objects of its own, which must not leak into the game.
    SavedState saved;
    if (!g_failed) {
        if (!gl.GetIntegerv && !LoadGl()) g_failed = true;
    }
    if (g_failed) return;
    Save(saved);
    RenderInitIfNeeded();
    if (!g_ready) {
        Restore(saved);
        return;
    }
    if (fontKey != wantFont) {
        if (!fontKey.empty()) InitFont();
        fontKey = wantFont;
    }

    gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, rt.screenW, rt.screenH);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_STENCIL_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_FRAMEBUFFER_SRGB);
    if (g_compat) gl.Disable(0x0BC0);
    gl.ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl.DepthMask(GL_FALSE);
    gl.Enable(GL_BLEND);
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.BlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    gl.BindVertexArray(g_vao);
    gl.BindBuffer(GL_ARRAY_BUFFER, g_vbo);
    gl.ActiveTexture(GL_TEXTURE0);
    if (gl.BindSampler) gl.BindSampler(0, 0);
    g_targetW = rt.screenW;
    g_targetH = rt.screenH;
    g_targetTopLeft = true;
    g_capturedThisFrame.clear();

    if (mode) {
        bool needSource = eyezoom || haveSources;
        if (needSource) PrepareFrameSource();
        gl.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        if (letterbox) DrawLetterbox(*mode);
        if (mode->border.enabled && rt.gameSizeFaked) {
            Outline(static_cast<float>(rt.destX), static_cast<float>(rt.destY), static_cast<float>(rt.destW), static_cast<float>(rt.destH),
                    static_cast<float>(mode->border.width), static_cast<float>(mode->border.radius), 0, mode->border.color);
        }
        if (eyezoom) DrawEyeZoom(*mode);
        // Sources draw in the order the mode lists them; mirrors need the game frame, images do not.
        for (auto& src : mode->sources) {
            if (src.type == "Mirror" && g_src.tex) {
                if (const MirrorConfig* m = FindMirror(src.id)) DrawMirror(*m, m->output);
            } else if (src.type == "MirrorGroup" && g_src.tex) {
                for (auto& g : rt.cfg.mirrorGroups) {
                    if (g.name == src.id) DrawMirrorGroup(g);
                }
            } else if (src.type == "Image") {
                for (auto& im : rt.cfg.images) {
                    if (im.name == src.id) DrawImageOverlay(im);
                }
            }
        }
    }
    DrawNinjabrainPanel();
    if (rt.guiOpen) DrawInfoPanel();
    else DrawToast();

    Restore(saved);
}
