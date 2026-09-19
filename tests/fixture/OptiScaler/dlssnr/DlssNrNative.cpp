#include "DlssNrNative.h"
#include <mutex>
#include <stdexcept>
#include <string>
namespace DlssNrNative { namespace {
struct State { std::recursive_mutex mutex; bool enabled=false,candidate=false,restartRequired=false,active=false; std::string status="FP8 selected"; };
State&S(){static State s;return s;}
void VerifyAssets(){ /* fixture: authoritative verifier anchor */ }
}
void SetPrecision(unsigned precision){auto&s=S();std::lock_guard<std::recursive_mutex>g(s.mutex);const bool on=precision==4,candidate=on;if(s.restartRequired)return;s.enabled=on;s.candidate=candidate;s.active=false;}
void SetEnabled(bool on){SetPrecision(on?4u:0u);}
bool IsActive(){return S().active;}
std::string Status(){return S().status;}
// fixture: forma reduzida da cadeia NVAPI real, so o bastante para as ancoras do patcher.
NvAPI_Status __cdecl CreateFunction(ID3D12Device*d,NVDX_ObjectHandle m,const char*n,NVDX_ObjectHandle*out){auto&s=S();auto rc=s.createFunction(d,m,n,out);if(rc==0&&n){unsigned kind=99;if(kind!=99)s.targets[*out]={d,m,kind};}return rc;}
NvAPI_Status __cdecl Launch(ID3D12GraphicsCommandList*c,const NVAPI_CU_KERNEL_LAUNCH_PARAMS*k,NvU32 count){auto&s=S();if(!s.enabled)return s.launch(c,k,count);auto target=s.targets.find(k->hFunction);if(target==s.targets.end())return s.launch(c,k,count);auto&t=target->second;(void)t;return s.launch(c,k,count);}
NvAPI_Status __cdecl DestroyFunction(ID3D12Device*d,NVDX_ObjectHandle f){auto&s=S();s.targets.erase(f);return s.destroyFunction(d,f);}
void* WrapNvapi(unsigned,void* original){return original;}
}
