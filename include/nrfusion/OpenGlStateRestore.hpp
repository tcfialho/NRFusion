#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>

namespace nrfusion {

using OpenGlGetIntegervFn = void (APIENTRY*)(GLenum, GLint*);
using OpenGlBindTextureFn = void (APIENTRY*)(GLenum, GLuint);

class OpenGlTextureBindingGuard {
public:
    OpenGlTextureBindingGuard(
        OpenGlGetIntegervFn getIntegerv,
        OpenGlBindTextureFn bindTexture) noexcept
        : bindTexture_(bindTexture) {
        if (getIntegerv == nullptr || bindTexture_ == nullptr) return;
        getIntegerv(GL_TEXTURE_BINDING_2D, &previous_);
        captured_ = true;
    }

    ~OpenGlTextureBindingGuard() {
        if (captured_)
            bindTexture_(GL_TEXTURE_2D, static_cast<GLuint>(previous_));
    }

    OpenGlTextureBindingGuard(const OpenGlTextureBindingGuard&) = delete;
    OpenGlTextureBindingGuard& operator=(const OpenGlTextureBindingGuard&) = delete;

    bool Captured() const noexcept { return captured_; }

private:
    OpenGlBindTextureFn bindTexture_ = nullptr;
    GLint previous_ = 0;
    bool captured_ = false;
};

} // namespace nrfusion
