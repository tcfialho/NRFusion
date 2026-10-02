#include "nrfusion/OpenGlStateRestore.hpp"

#include <cassert>

using namespace nrfusion;

namespace {

GLint gBinding = 0;
int gGetCalls = 0;
int gBindCalls = 0;

void APIENTRY FakeGetIntegerv(GLenum target, GLint* value) {
    assert(target == GL_TEXTURE_BINDING_2D);
    assert(value != nullptr);
    ++gGetCalls;
    *value = gBinding;
}

void APIENTRY FakeBindTexture(GLenum target, GLuint texture) {
    assert(target == GL_TEXTURE_2D);
    ++gBindCalls;
    gBinding = static_cast<GLint>(texture);
}

bool EarlyReturnAfterMutation() {
    OpenGlTextureBindingGuard guard(FakeGetIntegerv, FakeBindTexture);
    assert(guard.Captured());
    FakeBindTexture(GL_TEXTURE_2D, 99);
    return false;
}

} // namespace

int main() {
    gBinding = 41;
    gGetCalls = 0;
    gBindCalls = 0;
    {
        OpenGlTextureBindingGuard guard(FakeGetIntegerv, FakeBindTexture);
        assert(guard.Captured());
        FakeBindTexture(GL_TEXTURE_2D, 7);
        assert(gBinding == 7);
    }
    assert(gBinding == 41);
    assert(gGetCalls == 1);
    assert(gBindCalls == 2);

    gBinding = 53;
    assert(!EarlyReturnAfterMutation());
    assert(gBinding == 53);

    gBinding = 61;
    {
        OpenGlTextureBindingGuard guard(nullptr, FakeBindTexture);
        assert(!guard.Captured());
        FakeBindTexture(GL_TEXTURE_2D, 11);
    }
    assert(gBinding == 11);

    return 0;
}
