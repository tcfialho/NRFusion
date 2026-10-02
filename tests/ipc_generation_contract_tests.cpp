#include "nrfusion/IpcProtocol.hpp"

#include <cassert>

using namespace nrfusion;

int main() {
    static_assert(NRFUSION_IPC_VERSION == 3u);
    assert(IpcConnectionMatches(7, 7));
    assert(!IpcConnectionMatches(0, 0));
    assert(!IpcConnectionMatches(7, 8));
    assert(IpcSessionMatches(7, 11, 7, 11));
    assert(!IpcSessionMatches(7, 11, 8, 11));
    assert(!IpcSessionMatches(7, 11, 7, 12));

    IpcBuildMessage build{};
    build.connectionGeneration = 3;
    build.sessionId = 5;
    IpcFrameMessage frame{};
    frame.connectionGeneration = 3;
    frame.sessionId = 5;
    assert(IpcSessionMatches(
        build.connectionGeneration, build.sessionId,
        frame.connectionGeneration, frame.sessionId));
    return 0;
}
