#!/usr/bin/env python3
from __future__ import annotations
import argparse
import pathlib
import shutil

ROOT = pathlib.Path(__file__).resolve().parents[1]


def replace_once(path: pathlib.Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if new in text:
        return
    n = text.count(old)
    # The patcher is safe to rerun on its own output. If the exact replacement is already present,
    # the desired state has been reached; do not demand an anchor that the first run intentionally
    # consumed. A file containing neither state still fails closed instead of guessing.
    if n != 1:
        raise RuntimeError(f"{path}: expected one anchor, got {n}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


def insert_after(path: pathlib.Path, anchor: str, addition: str) -> None:
    text = path.read_text(encoding="utf-8")
    if addition in text:
        return
    replace_once(path, anchor, anchor + addition)


def insert_after_unique(path: pathlib.Path, anchor: str, addition: str) -> None:
    # insert_after's own idempotency check (addition in text) is wrong when addition is generic,
    # identical text repeated after several different anchors in the same file: after the first
    # insertion succeeds, every later call sees addition already present elsewhere and skips its
    # own anchor. This checks the specific anchor+addition pairing instead.
    text = path.read_text(encoding="utf-8")
    if anchor + addition in text:
        return
    replace_once(path, anchor, anchor + addition)


def add_project_item(vcx: pathlib.Path, tag: str, include: str, anchor: str, not_using_pch: bool = False) -> None:
    text = vcx.read_text(encoding="utf-8")
    if not_using_pch:
        item = (
            f'    <{tag} Include="{include}">\n'
            f'      <PrecompiledHeader>NotUsing</PrecompiledHeader>\n'
            f'    </{tag}>\n'
        )
    else:
        item = f'    <{tag} Include="{include}" />\n'
    if f'Include="{include}"' in text:
        return
    if anchor not in text:
        raise RuntimeError(f"{vcx}: project anchor missing: {anchor}")
    vcx.write_text(text.replace(anchor, anchor + item, 1), encoding="utf-8")


# The Ada W4A8 kernel has to run on the game's own D3D12 command list, reading buffers the model
# owns, so it goes through the same NVAPI cubin entry points the model itself uses. A CUDA-runtime
# launch cannot address those buffers, which is why the interceptor alone was never enough.
ADA_CUBIN_LAUNCHER_MARK = "// NRFUSION ada cubin launcher v13"
ADA_CUBIN_LAUNCHER = ADA_CUBIN_LAUNCHER_MARK + r"""
// Sixteen blocks share the kernel and none of them share weights, so the module is per device
// but the uploaded weights are per block. One set per device is what would silently run every
// block with whichever block's weights happened to arrive first.
struct AdaCubinWeights{Buffer expW,expS,expI,expV,prjW,prjS,prjI,prjV;std::vector<ComPtr<ID3D12Resource>>uploads;};
struct AdaCubinDevice{NVDX_ObjectHandle module=nullptr,tensorCore=nullptr,dp4a=nullptr,publish=nullptr;
 std::map<unsigned,AdaCubinWeights>blocks;bool ready=false,failed=false;};
std::map<ID3D12Device*,AdaCubinDevice>&AdaCubinDevices(){static std::map<ID3D12Device*,AdaCubinDevice>m;return m;}

fs::path AdaCubinDir(){wchar_t p[MAX_PATH]{};HMODULE m=nullptr;
 if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
   (LPCWSTR)&AdaCubinDevices,&m)&&GetModuleFileNameW(m,p,MAX_PATH))return fs::path(p).parent_path();
 return fs::current_path();}

Buffer AdaUpload(ID3D12Device*d,ID3D12GraphicsCommandList*c,const void*src,uint64_t bytes,
                 std::vector<ComPtr<ID3D12Resource>>&keep){
 // An 8-bit container carries no outliers, so its correction buffers are legitimately empty.
 // A one-byte placeholder keeps the kernel's pointer valid without special-casing the launch.
 if(!bytes){auto b=Allocate(d,256);return b;}
 if(!src)throw std::runtime_error("Ada W4A8 null upload");
 auto b=Allocate(d,bytes);auto up=Resource(d,std::max<uint64_t>(bytes,256),D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
 void*p=nullptr;Check(up->Map(0,nullptr,&p));memcpy(p,src,(size_t)bytes);up->Unmap(0,nullptr);
 Transition(c,b.gpu.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_DEST);
 c->CopyBufferRegion(b.gpu.Get(),0,up.Get(),0,bytes);
 Transition(c,b.gpu.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
 keep.push_back(up);return b;}

bool AdaCubinModule(ID3D12Device*dev,AdaCubinDevice&a){
 if(a.ready)return true;if(a.failed)return false;auto&s=S();
 if(!s.createModule||!s.createFunction||!s.launch){a.failed=true;return false;}
 try{fs::path dir=AdaCubinDir(),cubin;
  for(auto cand:{dir/"w4a8_ffn_sm89.cubin",dir/"OptiScaler"/"w4a8_ffn_sm89.cubin"})
   if(fs::exists(cand)){cubin=cand;break;}
  if(cubin.empty())throw std::runtime_error("w4a8_ffn_sm89.cubin not found");
  auto blob=Read(cubin);
  NvCheck(s.createModule(dev,blob.data(),(unsigned)blob.size(),&a.module));
  NvCheck(s.createFunction(dev,a.module,"AdaW4A8FusedFfnKernel_TensorCore",&a.tensorCore));
  NvCheck(s.createFunction(dev,a.module,"AdaW4A8FusedFfnKernel_Dp4a",&a.dp4a));
  NvCheck(s.createFunction(dev,a.module,"AdaW4A8PublishKernel",&a.publish));
  a.ready=true;
  fprintf(stderr,"NRFusion Ada W4A8 cubin ready from %s\n",cubin.string().c_str());
  return true;}
 catch(const std::exception&e){a.failed=true;
  fprintf(stderr,"NRFusion Ada W4A8 cubin preparation failed: %s\n",e.what());return false;}}

const AdaCubinWeights*AdaCubinBlock(ID3D12Device*dev,AdaCubinDevice&a,const nrfusion::AdaW4A8Params&p){
 auto found=a.blocks.find(p.blockIndex);if(found!=a.blocks.end())return &found->second;
 try{AdaCubinWeights w;auto&batch=*new UploadBatch(dev);auto*c=batch.cmd.Get();
  const uint64_t g=p.groups,gch=p.groupChannels,wide=p.wideChannels,gsz=p.groupSize;
  if(!g||!gsz)throw std::runtime_error("Ada W4A8 invalid geometry");
  const uint64_t wbits=p.weightBits?p.weightBits:4;
  w.expW=AdaUpload(dev,c,p.expandWeightsInt4,g*gch*wide*wbits/8,w.uploads);
  w.expS=AdaUpload(dev,c,p.expandScales,g*(gch/gsz)*wide*2,w.uploads);
  w.expI=AdaUpload(dev,c,p.expandOutlierIndices,(uint64_t)p.expandOutliersTotal*2,w.uploads);
  w.expV=AdaUpload(dev,c,p.expandOutlierValues,(uint64_t)p.expandOutliersTotal,w.uploads);
  w.prjW=AdaUpload(dev,c,p.projectWeightsInt4,g*wide*gch*wbits/8,w.uploads);
  w.prjS=AdaUpload(dev,c,p.projectScales,g*(wide/gsz)*gch*2,w.uploads);
  w.prjI=AdaUpload(dev,c,p.projectOutlierIndices,(uint64_t)p.projectOutliersTotal*2,w.uploads);
  w.prjV=AdaUpload(dev,c,p.projectOutlierValues,(uint64_t)p.projectOutliersTotal,w.uploads);
  batch.Finish();
  fprintf(stderr,"NRFusion Ada W4A8 weights uploaded for block %u\n",p.blockIndex);
  return &(a.blocks[p.blockIndex]=std::move(w));}
 catch(const std::exception&e){
  fprintf(stderr,"NRFusion Ada W4A8 weight upload failed for block %u: %s\n",p.blockIndex,e.what());
  return nullptr;}}

bool AdaCubinLaunch(ID3D12GraphicsCommandList*cmdList,const nrfusion::AdaW4A8Params&p)noexcept{
 if(!cmdList)return false;
 // Windows set to zero asks for the flag alone. That is the way out when the expanding stage was
 // already suppressed and the substitution then could not run: without it the model would wait
 // forever on a word nobody is left to write.
 if(!p.windows&&p.publishFlag){
  try{ComPtr<ID3D12Device>dev;if(FAILED(cmdList->GetDevice(IID_PPV_ARGS(&dev)))||!dev)return false;
   auto&a=AdaCubinDevices()[dev.Get()];if(!AdaCubinModule(dev.Get(),a)||!a.publish)return false;
   unsigned char f[12]{};UINT64 addr=(UINT64)p.publishFlag;memcpy(f,&addr,8);
   unsigned n=p.publisherBlocks?p.publisherBlocks:1;memcpy(f+8,&n,4);
   NVAPI_CU_KERNEL_LAUNCH_PARAMS q{};q.hFunction=a.publish;q.gridDim={n,1,1};q.blockDim={32,1,1};
   q.pParams=f;q.paramSize=sizeof(f);
   if(S().launch(cmdList,&q,1)!=NVAPI_OK)return false;
   Barrier(cmdList);return true;}
  catch(...){return false;}}
 if(!p.input||!p.output||!p.windows||!p.groups)return false;
 try{ComPtr<ID3D12Device>dev;if(FAILED(cmdList->GetDevice(IID_PPV_ARGS(&dev)))||!dev)return false;
  auto&a=AdaCubinDevices()[dev.Get()];if(!AdaCubinModule(dev.Get(),a))return false;
  const AdaCubinWeights*w=AdaCubinBlock(dev.Get(),a,p);if(!w)return false;
  // CUDA packs kernel parameters at natural alignment: pointers on 8, integers on 4.
  // Exactly the parameter block the kernel declares (EIATTR_CBANK_PARAM_SIZE): eleven
  // pointers and seven integers with natural alignment. NVAPI refuses a launch whose
  // paramSize does not match, which is what a padded 128 bytes turned into.
  unsigned char blob[124]{};
  auto put64=[&](size_t o,UINT64 v){memcpy(blob+o,&v,8);};
  auto put32=[&](size_t o,unsigned v){memcpy(blob+o,&v,4);};
  put64(0,(UINT64)p.input);put64(8,0);
  put64(16,w->expW.address());put64(24,w->expS.address());put64(32,w->expI.address());put64(40,w->expV.address());
  put32(48,p.expandOutliersTotal/p.groups);
  put64(56,w->prjW.address());put64(64,w->prjS.address());put64(72,w->prjI.address());put64(80,w->prjV.address());
  put32(88,p.projectOutliersTotal/p.groups);
  put64(96,(UINT64)p.output);put32(104,p.windows);put32(108,p.channels);put32(112,p.groups);
  put32(116,p.activationFormat);put32(120,p.weightBits);
  NVAPI_CU_KERNEL_LAUNCH_PARAMS k{};
  k.hFunction=(p.computeMode==nrfusion::AdaW4A8ComputeMode::Dp4a)?a.dp4a:a.tensorCore;
  k.gridDim={p.windows,p.groups,1};k.blockDim={256,1,1};
  // Must match kSharedBytes in W4A8FfnSm89.cu: the working set plus the correction arena that
  // lets the epilogue fold the outliers in instead of writing them back through atomics.
  k.pParams=blob;k.paramSize=sizeof(blob);k.dynSharedMemBytes=86272;
  if(S().launch(cmdList,&k,1)!=NVAPI_OK)return false;
  Barrier(cmdList);
  // The replaced stage published a word the next one waits on. The barrier above orders this
  // after every block of the work, which is what makes a separate launch enough.
  if(p.publishFlag&&a.publish){unsigned char f[12]{};UINT64 addr=(UINT64)p.publishFlag;memcpy(f,&addr,8);
   unsigned n=p.publisherBlocks?p.publisherBlocks:1;memcpy(f+8,&n,4);
   NVAPI_CU_KERNEL_LAUNCH_PARAMS q{};q.hFunction=a.publish;q.gridDim={n,1,1};q.blockDim={32,1,1};
   q.pParams=f;q.paramSize=sizeof(f);
   if(S().launch(cmdList,&q,1)!=NVAPI_OK)return false;
   Barrier(cmdList);}
  return true;}
 catch(...){return false;}}

const bool g_adaCubinInstalled=[]{nrfusion::SetAdaW4A8CubinLauncher(&AdaCubinLaunch);return true;}();
"""


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("checkout", type=pathlib.Path)
    args = ap.parse_args()
    checkout = args.checkout.resolve()
    opti = checkout / "OptiScaler"
    if not (opti / "Config.h").exists():
        raise SystemExit("Not an OptiScaler checkout")

    dest = opti / "nrfusion"
    dest.mkdir(exist_ok=True)
    # Host-facing portable closure. Keep this list complete for every local include reachable from
    # FusionRuntime/OptiScalerAdapter; tests verify both include closure and flattened-source syntax.
    headers = ["Types.hpp", "FrameContract.hpp", "PerformanceController.hpp", "NrCostModel.hpp", "WorkLedger.hpp", "TimingWorkMapper.hpp",
               "AsyncOverlapEstimator.hpp", "CrossQueueClockCalibrator.hpp", "D3D12QueueClockBridge.hpp", "D3D12AsyncFenceBridge.hpp", "AsyncQualification.hpp", "PrecisionAutotuner.hpp",
               "AutoDecision.hpp", "AutoTuneCoordinator.hpp", "RuntimeCapabilities.hpp", "FrameContractProvider.hpp",
               "ProfileStore.hpp", "CompatibilityDatabase.hpp", "Diagnostics.hpp", "FrameLimitPolicy.hpp", "ResidualReprojection.hpp", "ResidualEngine.hpp",
               "SchedulerPolicy.hpp", "ProviderPolicy.hpp", "TransportPolicy.hpp", "PipelinePolicy.hpp",
               "ResidualPolicy.hpp", "TemporalConfidence.hpp", "MgpuPlanner.hpp", "NvofPolicy.hpp", "NvofWrapper.hpp",
               "TelemetryTracker.hpp", "GuideValidation.hpp", "MotionNormalization.hpp", "MotionConfidence.hpp",
               "MotionGuideSelection.hpp", "GuideHistoryState.hpp", "MotionGuideBinding.hpp", "TemporalHistoryRegistry.hpp",
               "PipelinedExecutorState.hpp",
               "AdaptiveExposure.hpp", "AdaptiveExposureController.hpp",
               "FusionRuntime.hpp", "OptiScalerAdapter.hpp", "Presets.hpp", "QualityValidator.hpp",
                "Dlss5NeuralRendering.hpp", "NrKernelProfile.hpp",
                "W4A8Ffn.hpp", "AdaW4A8Interceptor.hpp", "DlssgTransfusion.hpp",
                "SyntheticProvider.hpp", "SyntheticDlaaContract.hpp", "SyntheticDx12Provider.hpp",
                "MatchedResidualShader.hpp",
                "MotionVectorResolver.hpp", "NvofMotionProvider.hpp", "SyntheticDx11BridgeProvider.hpp",
                "IpcProtocol.hpp", "SyntheticVulkanProvider.hpp"]
    # CaptureProvider32(Export)/HostServer64 are the x86-carrier transport: a client DLL that runs
    # inside the 32-bit game (nrfusion_capture32.dll, its own CMake target) and a headless x64 host
    # process (NRFusionHost64.exe, ditto), never OptiScaler.dll itself. CaptureProvider32Export.cpp's
    # DllMain previously got copied in here too and collided with OptiScaler's own dllmain.cpp
    # (LNK2005/LNK1169) -- nothing in OptiScaler.dll references any of these four files.
    sources = ["PerformanceController.cpp", "NrCostModel.cpp", "WorkLedger.cpp", "TimingWorkMapper.cpp",
               "AsyncOverlapEstimator.cpp", "CrossQueueClockCalibrator.cpp", "AsyncQualification.cpp", "PrecisionAutotuner.cpp",
               "AutoTuneCoordinator.cpp", "ProfileStore.cpp", "CompatibilityDatabase.cpp", "Diagnostics.cpp", "FrameLimitPolicy.cpp",
               "ResidualReprojection.cpp", "ResidualEngine.cpp",
               "SchedulerPolicy.cpp", "ProviderPolicy.cpp", "TransportPolicy.cpp", "PipelinePolicy.cpp",
               "ResidualPolicy.cpp", "TemporalConfidence.cpp", "MgpuPlanner.cpp", "NvofPolicy.cpp", "NvofWrapper.cpp",
               "TelemetryTracker.cpp", "GuideValidation.cpp", "MotionNormalization.cpp", "MotionConfidence.cpp", "MotionGuideBinding.cpp", "TemporalHistoryRegistry.cpp",
               "PipelinedExecutorState.cpp", "NrKernelProfile.cpp",
               "AdaptiveExposure.cpp", "AdaptiveExposureController.cpp",
               "OptiScalerAdapter.cpp", "Presets.cpp", "QualityValidator.cpp", "Dlss5NeuralRendering.cpp",
               "W4A8FfnStub.cpp", "AdaW4A8Interceptor.cpp", "DlssgTransfusion.cpp",
               "SyntheticDx12Provider.cpp", "NvofMotionProvider.cpp", "SyntheticDx11BridgeProvider.cpp",
               "SyntheticVulkanProvider.cpp"]
    for name in headers:
        shutil.copy2(ROOT / "include" / "nrfusion" / name, dest / name)
    for name in sources:
        src = (ROOT / "src" / name).read_text(encoding="utf-8")
        # Flattened host directory: rewrite portable include prefix locally.
        src = src.replace('#include "nrfusion/', '#include "')
        (dest / name).write_text(src, encoding="utf-8")
    for name in headers:
        p = dest / name
        p.write_text(p.read_text(encoding="utf-8").replace('#include "nrfusion/', '#include "'), encoding="utf-8")

    configh = opti / "Config.h"
    if "CustomOptional<bool> CheckForUpdate { true };" in configh.read_text(encoding="utf-8"):
        replace_once(configh, "CustomOptional<bool> CheckForUpdate { true };\n",
                              "CustomOptional<bool> CheckForUpdate { false };\n")
    if "CustomOptional<uint32_t> DlssNrWhitePointSource { 1 };" in configh.read_text(encoding="utf-8"):
        replace_once(configh, "CustomOptional<uint32_t> DlssNrWhitePointSource { 1 };\n",
                              "CustomOptional<uint32_t> DlssNrWhitePointSource { 3 };\n")

    if "CustomOptional<bool> DlssNrAdaRuntime" not in configh.read_text(encoding="utf-8"):
        insert_after(configh,
            "    CustomOptional<float> DlssNrWorkingScale { 1.0f };\n",
            """
    // NRFusion normal UI: only mode + rendered-FPS target are persisted.
    CustomOptional<int> DlssNrFusionMode { 0 }; // 0 Auto, 1 Max FPS, 2 Balanced, 3 Quality, 4 Custom
    CustomOptional<float> DlssNrTargetFps { 60.0f };
    CustomOptional<bool> DlssNrAdaRuntime { false };
    // Off means Auto owns the precision. On pins DlssNrPrecision to the user's pick, so a manual
    // choice is never silently overwritten by the runtime's own trade.
    CustomOptional<bool> DlssNrPrecisionManual { false };
    CustomOptional<bool> DlssNrAdaW4A8 { true };
    CustomOptional<std::string> DlssNrBackend { "Auto" };
    CustomOptional<bool> DlssNrRecalibrateBackend { false };
""")
    elif "CustomOptional<bool> DlssNrAdaW4A8" not in configh.read_text(encoding="utf-8"):
        insert_after(configh,
            "    CustomOptional<bool> DlssNrPrecisionManual { false };\n",
            "    CustomOptional<bool> DlssNrAdaW4A8 { true };\n")
    elif "CustomOptional<std::string> DlssNrBackend" not in configh.read_text(encoding="utf-8"):
        insert_after(configh,
            "    CustomOptional<bool> DlssNrPrecisionManual { false };\n",
            '    CustomOptional<std::string> DlssNrBackend { "Auto" };\n'
            "    CustomOptional<bool> DlssNrRecalibrateBackend { false };\n")

    if "CustomOptional<std::string> FGDLSSGControlMode" not in configh.read_text(encoding="utf-8"):
        if "CustomOptional<bool, NoDefault> FGDLSSGAdaBlackwellKernels;" in configh.read_text(encoding="utf-8"):
            insert_after(configh,
                "    CustomOptional<bool, NoDefault> FGDLSSGAdaBlackwellKernels;\n",
                '    CustomOptional<std::string> FGDLSSGControlMode { "follow_game" };\n'
                '    CustomOptional<std::string> FGDLSSGQualityMode { "performance" };\n'
                '    CustomOptional<std::string> FGDLSSGUiRecomposition { "auto" };\n'
                '    CustomOptional<bool> FGDLSSGTransfusion { true };\n')
        else:
            insert_after(configh,
                "    CustomOptional<float> DlssNrWorkingScale { 1.0f };\n",
                '    CustomOptional<std::string> FGDLSSGControlMode { "follow_game" };\n'
                '    CustomOptional<std::string> FGDLSSGQualityMode { "performance" };\n'
                '    CustomOptional<std::string> FGDLSSGUiRecomposition { "auto" };\n'
                '    CustomOptional<bool> FGDLSSGTransfusion { true };\n')

    configcpp = opti / "Config.cpp"
    if 'DlssNrAdaRuntime.set_from_config(readBool("DlssNr", "AdaRuntime"));' not in configcpp.read_text(encoding="utf-8"):
        insert_after(configcpp,
            '            DlssNrWorkingScale.set_from_config(readFloat("DlssNr", "WorkingScale"));\n',
            '            DlssNrFusionMode.set_from_config(readInt("DlssNr", "FusionMode"));\n'
            '            DlssNrTargetFps.set_from_config(readFloat("DlssNr", "TargetFps"));\n'
            '            DlssNrAdaRuntime.set_from_config(readBool("DlssNr", "AdaRuntime"));\n'
            '            DlssNrPrecisionManual.set_from_config(readBool("DlssNr", "PrecisionManual"));\n'
            '            DlssNrAdaW4A8.set_from_config(readBool("DlssNr", "AdaW4A8"));\n'
            '            DlssNrBackend.set_from_config(readString("DlssNr", "Backend"));\n'
            '            DlssNrRecalibrateBackend.set_from_config(readBool("DlssNr", "RecalibrateBackend"));\n')
    elif 'DlssNrAdaW4A8.set_from_config(readBool("DlssNr", "AdaW4A8"));' not in configcpp.read_text(encoding="utf-8"):
        insert_after(configcpp,
            '            DlssNrPrecisionManual.set_from_config(readBool("DlssNr", "PrecisionManual"));\n',
            '            DlssNrAdaW4A8.set_from_config(readBool("DlssNr", "AdaW4A8"));\n')
    elif 'DlssNrBackend.set_from_config(readString("DlssNr", "Backend"));' not in configcpp.read_text(encoding="utf-8"):
        insert_after(configcpp,
            '            DlssNrPrecisionManual.set_from_config(readBool("DlssNr", "PrecisionManual"));\n',
            '            DlssNrBackend.set_from_config(readString("DlssNr", "Backend"));\n'
            '            DlssNrRecalibrateBackend.set_from_config(readBool("DlssNr", "RecalibrateBackend"));\n')

    if 'FGDLSSGControlMode.set_from_config' not in configcpp.read_text(encoding="utf-8"):
        if 'FGDLSSGAdaBlackwellKernels.set_from_config' in configcpp.read_text(encoding="utf-8"):
            insert_after(configcpp,
                '            FGDLSSGAdaBlackwellKernels.set_from_config(readBool("DLSSG", "AdaBlackwellKernels"));\n',
                '            FGDLSSGControlMode.set_from_config(readString("DLSSG", "ControlMode"));\n'
                '            FGDLSSGQualityMode.set_from_config(readString("DLSSG", "QualityMode"));\n'
                '            FGDLSSGUiRecomposition.set_from_config(readString("DLSSG", "UiRecomposition"));\n'
                '            FGDLSSGTransfusion.set_from_config(readBool("DLSSG", "Transfusion"));\n')
            insert_after(configcpp,
                '        ini.SetValue("DLSSG", "AdaBlackwellKernels", GetBoolValue(Instance()->FGDLSSGAdaBlackwellKernels.value_for_config()).c_str());\n',
                '        ini.SetValue("DLSSG", "ControlMode", Instance()->FGDLSSGControlMode.value_for_config_or("follow_game").c_str());\n'
                '        ini.SetValue("DLSSG", "QualityMode", Instance()->FGDLSSGQualityMode.value_for_config_or("performance").c_str());\n'
                '        ini.SetValue("DLSSG", "UiRecomposition", Instance()->FGDLSSGUiRecomposition.value_for_config_or("auto").c_str());\n'
                '        ini.SetValue("DLSSG", "Transfusion", GetBoolValue(Instance()->FGDLSSGTransfusion.value_for_config()).c_str());\n')

    if 'ini.SetValue("DlssNr", "AdaRuntime", GetBoolValue(Instance()->DlssNrAdaRuntime.value_for_config()).c_str());' not in configcpp.read_text(encoding="utf-8"):
        insert_after(configcpp,
            '    ini.SetValue("DlssNr", "WorkingScale", GetFloatValue(Instance()->DlssNrWorkingScale.value_for_config()).c_str());\n',
            '    ini.SetValue("DlssNr", "FusionMode", GetIntValue(Instance()->DlssNrFusionMode.value_for_config()).c_str());\n'
            '    ini.SetValue("DlssNr", "TargetFps", GetFloatValue(Instance()->DlssNrTargetFps.value_for_config()).c_str());\n'
            '    ini.SetValue("DlssNr", "AdaRuntime", GetBoolValue(Instance()->DlssNrAdaRuntime.value_for_config()).c_str());\n'
            '    ini.SetValue("DlssNr", "PrecisionManual", GetBoolValue(Instance()->DlssNrPrecisionManual.value_for_config()).c_str());\n'
            '    ini.SetValue("DlssNr", "AdaW4A8", GetBoolValue(Instance()->DlssNrAdaW4A8.value_for_config()).c_str());\n'
            '    ini.SetValue("DlssNr", "Backend", Instance()->DlssNrBackend.value_for_config_or("Auto").c_str());\n'
            '    ini.SetValue("DlssNr", "RecalibrateBackend", GetBoolValue(Instance()->DlssNrRecalibrateBackend.value_for_config()).c_str());\n')
    elif 'ini.SetValue("DlssNr", "AdaW4A8",' not in configcpp.read_text(encoding="utf-8"):
        insert_after(configcpp,
            '    ini.SetValue("DlssNr", "PrecisionManual", GetBoolValue(Instance()->DlssNrPrecisionManual.value_for_config()).c_str());\n',
            '    ini.SetValue("DlssNr", "AdaW4A8", GetBoolValue(Instance()->DlssNrAdaW4A8.value_for_config()).c_str());\n')
    elif 'ini.SetValue("DlssNr", "Backend",' not in configcpp.read_text(encoding="utf-8"):
        insert_after(configcpp,
            '    ini.SetValue("DlssNr", "PrecisionManual", GetBoolValue(Instance()->DlssNrPrecisionManual.value_for_config()).c_str());\n',
            '    ini.SetValue("DlssNr", "Backend", Instance()->DlssNrBackend.value_for_config_or("Auto").c_str());\n'
            '    ini.SetValue("DlssNr", "RecalibrateBackend", GetBoolValue(Instance()->DlssNrRecalibrateBackend.value_for_config()).c_str());\n')

    native_h = opti / "dlssnr/DlssNrNative.h"
    native_cpp = opti / "dlssnr/DlssNrNative.cpp"
    if not native_h.exists() or not native_cpp.exists():
        raise RuntimeError("OptiScaler DLSS-NR native backend files are missing")

    native_decl_anchor = "bool IsActive();\n"
    if "bool HybridAvailable();" not in native_h.read_text(encoding="utf-8"):
        insert_after(native_h, native_decl_anchor, "bool HybridAvailable();\n")

    native_impl_anchor = "void SetEnabled(bool on){SetPrecision(on?4u:0u);}"
    native_text = native_cpp.read_text(encoding="utf-8")
    if "bool HybridAvailable()" not in native_text:
        candidate_verifier = "VerifyCandidateAssets();" if "VerifyCandidateAssets()" in native_text else ""
        hybrid_impl = """// NRFusion precision Auto uses the same authoritative verifier as the hybrid backend.
// Cache the result so this expensive full-file hash pass can run at most once per process.
bool HybridAvailable(){
 auto&s=S();std::lock_guard<std::recursive_mutex>g(s.mutex);
 static bool checked=false,available=false;if(checked)return available;checked=true;
 try{VerifyAssets();""" + candidate_verifier + """available=true;}catch(const std::exception&e){s.status=std::string(\"Hybrid unavailable: \")+e.what();available=false;}
 return available;
}
        """
        replace_once(native_cpp, native_impl_anchor, hybrid_impl + native_impl_anchor)

    # Optional CUDA function/launch tracing is diagnostic only. It lets an Ada candidate audit
    # which FP16/FP8 entry points the real model actually calls without changing the default path.
    native_trace_helper_anchor = "State&S(){static State*s=new State;return*s;}"
    native_trace_helper = (
        "State&S(){static State*s=new State;return*s;}"
        "bool TraceCuda(){static const bool enabled=[](){char value[2]{};"
        "return GetEnvironmentVariableA(\"NRFUSION_TRACE_CUDA\",value,sizeof(value))>0;}();"
        "return enabled;}"
        "bool DumpCudaParams(){static const bool enabled=[](){char value[2]{};"
        "return GetEnvironmentVariableA(\"NRFUSION_DUMP_CUDA_PARAMS\",value,sizeof(value))>0;}();"
        "return enabled;}"
    )
    if native_trace_helper_anchor in native_cpp.read_text(encoding="utf-8") and "bool TraceCuda()" not in native_cpp.read_text(encoding="utf-8"):
        replace_once(native_cpp, native_trace_helper_anchor, native_trace_helper)
    native_state_trace_anchor = "std::map<NVDX_ObjectHandle,Target>targets;"
    if native_state_trace_anchor in native_cpp.read_text(encoding="utf-8") and "functionNames" not in native_cpp.read_text(encoding="utf-8"):
        replace_once(native_cpp, native_state_trace_anchor,
                     native_state_trace_anchor + "std::map<NVDX_ObjectHandle,std::string>functionNames;")
    native_create_trace_anchor = "auto rc=s.createFunction(d,m,n,out);if(rc==0&&n){"
    native_create_trace_new = (
        "auto rc=s.createFunction(d,m,n,out);if(rc==0&&n){"
        "s.functionNames[*out]=n;"
        "if(TraceCuda())fprintf(stderr,\"NRFusion CUDA create %s\\n\",n);"
    )
    if native_create_trace_anchor in native_cpp.read_text(encoding="utf-8"):
        replace_once(native_cpp, native_create_trace_anchor, native_create_trace_new)
    native_launch_trace_anchor = "auto target=s.targets.find(k->hFunction);if(target==s.targets.end())return s.launch(c,k,count);auto&t=target->second;"
    native_launch_trace_new = (
        "auto target=s.targets.find(k->hFunction);"
        "if(TraceCuda()){auto name=s.functionNames.find(k->hFunction);"
        "fprintf(stderr,\"NRFusion CUDA launch %s\\n\",name==s.functionNames.end()?\"unknown\":name->second.c_str());}"
        "if(target==s.targets.end())return s.launch(c,k,count);auto&t=target->second;"
    )
    if native_launch_trace_anchor in native_cpp.read_text(encoding="utf-8"):
        replace_once(native_cpp, native_launch_trace_anchor, native_launch_trace_new)
    if "#include <nrfusion/AdaW4A8Interceptor.hpp>" not in native_cpp.read_text(encoding="utf-8"):
        insert_after(native_cpp, '#include "DlssNrNative.h"\n', '#include <nrfusion/AdaW4A8Interceptor.hpp>\n')
    if "#include <nrfusion/W4A8Ffn.hpp>" not in native_cpp.read_text(encoding="utf-8"):
        insert_after(native_cpp, '#include <nrfusion/AdaW4A8Interceptor.hpp>\n',
                     '#include <nrfusion/W4A8Ffn.hpp>\n')

    native_launch_early_anchor = (
        "NvAPI_Status __cdecl Launch(ID3D12GraphicsCommandList*c,const NVAPI_CU_KERNEL_LAUNCH_PARAMS*k,NvU32 count){"
        "auto&s=S();std::lock_guard<std::recursive_mutex>g(s.mutex);"
    )
    native_launch_early_new = (
        ADA_CUBIN_LAUNCHER +
        "NvAPI_Status __cdecl Launch(ID3D12GraphicsCommandList*c,const NVAPI_CU_KERNEL_LAUNCH_PARAMS*k,NvU32 count){\n"
        "auto&s=S();std::lock_guard<std::recursive_mutex>g(s.mutex);\n"
        "if(nrfusion::TryInterceptAdaW4A8(c,k,count))return NVAPI_OK;\n"
        "if(TraceCuda()&&k){auto name=s.functionNames.find(k->hFunction);"
        "const char*label=name==s.functionNames.end()?\"unknown\":name->second.c_str();"
        "fprintf(stderr,\"NRFusion CUDA launch early %s param=%u grid=%u,%u,%u block=%u,%u,%u shared=%u\\n\","
        "label,k->paramSize,k->gridDim.x,k->gridDim.y,k->gridDim.z,k->blockDim.x,k->blockDim.y,k->blockDim.z,"
        "k->dynSharedMemBytes);"
        "if(DumpCudaParams()){static unsigned dumped=0;"
        "if(dumped<128&&k->pParams&&k->paramSize){const unsigned bytes=std::min<unsigned>(k->paramSize,128);"
        "fprintf(stderr,\"NRFusion CUDA params %s bytes=\",label);"
        "for(unsigned i=0;i<bytes;++i)fprintf(stderr,\"%02X\",reinterpret_cast<const unsigned char*>(k->pParams)[i]);"
        "fprintf(stderr,\"\\n\");++dumped;}}}"
    )
    native_text = native_cpp.read_text(encoding="utf-8")
    if native_launch_early_anchor in native_text and "NRFusion CUDA launch early" not in native_text:
        replace_once(native_cpp, native_launch_early_anchor, native_launch_early_new)
    elif "bool AdaCubinLaunch(" in native_text and (
            ADA_CUBIN_LAUNCHER_MARK not in native_text
            or native_text.count("struct AdaCubinWeights{") > 1):
        # Patched by an earlier version of this script. Skipping would leave the old launcher in
        # place, and the old one uploads one set of weights per device instead of one per block --
        # which is exactly the mistake the new one exists to fix.
        #
        # The cut has to start at the first line the launcher owns, not at one of the structs inside
        # it: starting lower leaves the previous version's opening lines behind, and the file then
        # declares the same struct twice.
        openers = [native_text.find(mark) for mark in
                   ("// NRFUSION ada cubin launcher", "// Sixteen blocks share the kernel",
                    "struct AdaCubinWeights{", "struct AdaCubinDevice{")]
        start = min(index for index in openers if index >= 0)
        installed = native_text.index("const bool g_adaCubinInstalled=", start)
        end = native_text.index(chr(10), native_text.index(";", installed)) + 1
        native_cpp.write_text(native_text[:start] + ADA_CUBIN_LAUNCHER.lstrip(chr(10)) + native_text[end:],
                              encoding="utf-8")

    dx12_h = opti / "dlssnr/DlssNrFeature_Dx12.h"
    if dx12_h.exists():
        if "#include <nrfusion/AdaptiveExposure.hpp>" not in dx12_h.read_text(encoding="utf-8"):
            insert_after(dx12_h, '#include <string>\n', '#include <nrfusion/AdaptiveExposure.hpp>\n')
            insert_after(dx12_h, 'ExposureStatus GameExposureStatus();\n', 'nrfusion::ExposureDecision LastExposureDecision();\n')

        if "#include <d3d11.h>" not in dx12_h.read_text(encoding="utf-8"):
            insert_after(dx12_h, '#include <d3d12.h>\n', '#include <d3d11.h>\n')

        if "bool EvaluateSynthetic(" not in dx12_h.read_text(encoding="utf-8"):
            insert_after(dx12_h,
                'void EvaluateBeforeUpscale(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params,\n'
                '                           ID3D12CommandQueue* timingQueue = nullptr,\n'
                '                           unsigned long long submissionEpoch = 0, bool rayReconstruction = false);\n',
                '\n'
                '// NRFusion Synthetic route (D3D12, no native DLSS/FSR/XeSS contract): the game never built an\n'
                '// NVSDK_NGX_Parameter, so there is nothing to read Color/Depth/MotionVectors from. This manufactures\n'
                '// that parameter object itself, from the swapchain\'s own current back buffer, as a genuine 1:1 DLAA\n'
                '// contract, and hands it to the same EvaluateAfterUpscale above -- unmodified, so the native path is\n'
                '// provably untouched by this addition. queue is the command queue the caller will submit on.\n'
                '// Returns false if the pass could not run this frame (queue/resource missing, format unsupported,\n'
                '// NGX parameter allocation failed); the caller\'s own Present is unaffected either way.\n'
                'bool EvaluateSynthetic(ID3D12CommandQueue* queue, ID3D12Resource* backBuffer);\n'
                '\n'
                '// Fase 4: the same Synthetic route for a D3D11 game with no native DLSS/FSR/XeSS contract. There is\n'
                '// no D3D12 device at all here, so this rides OptiScaler\'s own, already-proven Dx11WithDx12 bridge --\n'
                '// the same private-D3D12-device/NT-shared-handle/fence machinery that already carries native D3D11\n'
                '// DLSS games through this exact model. dx11Device/dx11Context are the game\'s own; backBuffer is the\n'
                '// D3D11 resource to read from and write the answer back onto (the swapchain\'s current back buffer).\n'
                'bool EvaluateSyntheticDx11(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context,\n'
                '                          ID3D11Resource* backBuffer);\n')

    vk_h = opti / "dlssnr/DlssNrFeature_Vk.h"
    if vk_h.exists():
        if "#include <nrfusion/AdaptiveExposure.hpp>" not in vk_h.read_text(encoding="utf-8"):
            insert_after(vk_h, '#include <nvsdk_ngx_helpers_vk.h>\n', '#include <nrfusion/AdaptiveExposure.hpp>\n')
            insert_after(vk_h, 'bool ExposureOfferedVk();\n', 'nrfusion::ExposureDecision LastExposureDecisionVk();\n')

    exposure_scan = opti / "dlssnr/DlssNr_ExposureScan.cpp"
    if exposure_scan.exists():
        scan_wanted_old = (
            "bool Wanted()\n"
            "{\n"
            "    return Config::Instance()->DlssNrWhitePointSource.value_or_default() == 2 ||\n"
            "           Config::Instance()->DlssNrScanExposure.value_or_default();\n"
            "}\n"
        )
        scan_wanted_new = (
            "bool Wanted()\n"
            "{\n"
            "    return Config::Instance()->DlssNrWhitePointSource.value_or_default() == 2 ||\n"
            "           Config::Instance()->DlssNrWhitePointSource.value_or_default() == 3 ||\n"
            "           Config::Instance()->DlssNrScanExposure.value_or_default();\n"
            "}\n"
        )
        replace_once(exposure_scan, scan_wanted_old, scan_wanted_new)

        # Exposure discovery is needed every frame only while the candidate set is being learned.
        # Once a candidate has moved (or the existing patience window proves that none moves),
        # sample it once every four render frames (15 Hz at 60 FPS). This keeps the adaptive
        # exposure path responsive while removing most of the repeated copy/barrier traffic from
        # the NR critical path.
        exposure_text = exposure_scan.read_text(encoding="utf-8")
        if "#include <algorithm>\n" in exposure_text:
            insert_after(exposure_scan, "#include <algorithm>\n", "#include <atomic>\n")
        if "constexpr unsigned int kSlots = 4;\n" in exposure_text:
            insert_after(exposure_scan,
                         "constexpr unsigned int kSlots = 4;\n",
                         "constexpr unsigned int kStableTickInterval = 4;\n"
                         "constexpr unsigned int kStableAfterFrames = 1800;\n")
        if "ScanState g_scan;\n" in exposure_text:
            insert_after(exposure_scan,
                         "ScanState g_scan;\n",
                         "std::atomic_bool g_scanStable { false };\n")
        if "    g_scan.tracked.push_back(t);\n" in exposure_text:
            insert_after(exposure_scan,
                         "    g_scan.tracked.push_back(t);\n",
                         "    g_scanStable.store(false, std::memory_order_relaxed);\n")

        tick_anchor = (
            "    if (!Wanted())\n"
            "        return;\n"
            "\n"
            "    if (device == nullptr || cmdList == nullptr)\n"
        )
        tick_new = (
            "    if (!Wanted())\n"
            "        return;\n"
            "\n"
            "    static std::atomic<unsigned long long> tickCalls { 0 };\n"
            "    const unsigned long long call = tickCalls.fetch_add(1, std::memory_order_relaxed);\n"
            "    if (g_scanStable.load(std::memory_order_relaxed) &&\n"
            "        (call % kStableTickInterval) != 0)\n"
            "        return;\n"
            "\n"
            "    if (device == nullptr || cmdList == nullptr)\n"
        )
        if tick_anchor in exposure_scan.read_text(encoding="utf-8"):
            replace_once(exposure_scan, tick_anchor, tick_new)

        moving_anchor = (
            "                if (t.inRange > 1 && t.highest > t.lowest * 1.25f)\n"
            "                    t.moves = true;\n"
        )
        moving_new = (
            "                if (t.inRange > 1 && t.highest > t.lowest * 1.25f)\n"
            "                {\n"
            "                    t.moves = true;\n"
            "                    if (!g_scanStable.exchange(true, std::memory_order_relaxed))\n"
            "                        LOG_INFO(\"DLSS-NR exposure scan: stable candidate; sampling every {} frames\",\n"
            "                                 kStableTickInterval);\n"
            "                }\n"
        )
        if moving_anchor in exposure_scan.read_text(encoding="utf-8"):
            replace_once(exposure_scan, moving_anchor, moving_new)

        stable_anchor = (
            "    g_scan.frames++;\n"
            "    g_scan.status = \"\";\n"
        )
        stable_new = (
            "    g_scan.frames++;\n"
            "    if (g_scan.frames >= kStableAfterFrames &&\n"
            "        !g_scanStable.exchange(true, std::memory_order_relaxed))\n"
            "        LOG_INFO(\"DLSS-NR exposure scan: no movement after {} samples; sampling every {} frames\",\n"
            "                 kStableAfterFrames, kStableTickInterval);\n"
            "    g_scan.status = \"\";\n"
        )
        if stable_anchor in exposure_scan.read_text(encoding="utf-8"):
            replace_once(exposure_scan, stable_anchor, stable_new)

        release_anchor = (
            "    g_scan.tracked.clear();\n"
            "    g_scan.complained = false;\n"
        )
        release_new = (
            "    g_scan.tracked.clear();\n"
            "    g_scanStable.store(false, std::memory_order_relaxed);\n"
            "    g_scan.complained = false;\n"
        )
        if release_anchor in exposure_scan.read_text(encoding="utf-8"):
            replace_once(exposure_scan, release_anchor, release_new)

        shutdown_anchor = (
            "    g_scan.tracked.clear();\n"
            "\n"
            "    for (unsigned int i = 0; i < kSlots; ++i)\n"
        )
        shutdown_new = (
            "    g_scan.tracked.clear();\n"
            "    g_scanStable.store(false, std::memory_order_relaxed);\n"
            "\n"
            "    for (unsigned int i = 0; i < kSlots; ++i)\n"
        )
        if shutdown_anchor in exposure_scan.read_text(encoding="utf-8"):
            replace_once(exposure_scan, shutdown_anchor, shutdown_new)

    dx12 = opti / "shaders/dlssnr/DlssNr_Dx12.cpp"
    insert_after(dx12, '#include <gpu_time/GpuTime_Dx12.h>\n',
                 '#include <nrfusion/OptiScalerAdapter.hpp>\n#include <nrfusion/AdaptiveExposureController.hpp>\n')
    insert_after(dx12, '#include <State.h>\n', '#include <misc/IdentifyGpu.h>\n')

    dx12_text = dx12.read_text(encoding="utf-8")
    selected_model_anchor = (
        "const char* SelectedModelFile()\n"
        "{\n"
        "    return \"nvngx_dlssnr.dll\";\n"
        "}\n"
    )
    if selected_model_anchor in dx12_text:
        selected_model_impl = (
            "std::optional<std::filesystem::path> FindAdaRuntimeSidecar()\n"
            "{\n"
            "    if (g_dllDir.empty())\n"
            "        g_dllDir = Util::DllPath().remove_filename();\n"
            "    auto path = Util::FindFilePath(g_dllDir, \"nvngx_dlssnr_ada.dll\");\n"
            "    if (!path.has_value())\n"
            "        path = Util::FindFilePath(Util::ExePath().remove_filename(), \"nvngx_dlssnr_ada.dll\");\n"
            "    return path;\n"
            "}\n"
            "\n"
            "bool IsAdaHost()\n"
            "{\n"
            "    const auto& gpu = IdentifyGpu::getPrimaryGpu();\n"
            "    return gpu.vendorId == VendorId::Nvidia &&\n"
            "           (gpu.nvidiaArchInfo.architecture_id == NV_GPU_ARCHITECTURE_AD100 ||\n"
            "            gpu.name.find(\"RTX 40\") != std::string::npos);\n"
            "}\n"
            "\n"
            "const char* SelectedModelFile()\n"
            "{\n"
            "    const bool adaRequested = Config::Instance()->DlssNrAdaRuntime.value_or_default();\n"
            "    const bool adaSidecar = adaRequested && IsAdaHost() && FindAdaRuntimeSidecar().has_value();\n"
            "    static int loggedVariant = -1;\n"
            "    const int variant = adaSidecar ? 1 : 0;\n"
            "    if (loggedVariant != variant)\n"
            "    {\n"
            "        loggedVariant = variant;\n"
            "        LOG_INFO(\"NRFusion: model runtime variant = {}\",\n"
            "                 adaSidecar ? \"Ada FP8 sidecar (opt-in)\" :\n"
            "                 (adaRequested ? \"original FP8 (Ada sidecar unavailable)\" : \"original FP8\"));\n"
            "    }\n"
            "    return adaSidecar ? \"nvngx_dlssnr_ada.dll\" : \"nvngx_dlssnr.dll\";\n"
            "}\n"
        )
        replace_once(dx12, selected_model_anchor, selected_model_impl)

    dx12_text = dx12.read_text(encoding="utf-8")
    resolve_wp_anchor = "float ResolveWhitePoint(const Config& cfg, bool isHdrBuffer)\n"
    if resolve_wp_anchor in dx12_text:
        resolve_wp_helper = (
            "static nrfusion::ExposureDecision g_lastExposureDecision{};\n\n"
            "namespace DlssNr\n"
            "{\n"
            "nrfusion::ExposureDecision LastExposureDecision()\n"
            "{\n"
            "    return g_lastExposureDecision;\n"
            "}\n"
            "}\n\n"
        )
        if "\nnamespace\n{\n" in dx12_text:
            replace_once(dx12, "\nnamespace\n{\n", "\n" + resolve_wp_helper + "namespace\n{\n")
        else:
            replace_once(dx12, resolve_wp_anchor, resolve_wp_helper + resolve_wp_anchor)

        dx12_hdr_anchor = "    if (!isHdrBuffer)\n        return slider;\n"
        dx12_auto_exposure = """    if (!isHdrBuffer)
        return slider;

    if (cfg.DlssNrWhitePointSource.value_or_default() == 3)
    {
        std::optional<nrfusion::ExposureMeasurement> gameExp;
        if (g_nr.gameExposure > 1e-6f)
        {
            const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
            nrfusion::ExposureMeasurement m;
            m.source = nrfusion::ExposureSource::GameExposure;
            static std::uint64_t s_dx12ExpFrame = 0;
            m.frameId = ++s_dx12ExpFrame;
            m.whitePoint = std::clamp(g_nr.gamePreExposure / g_nr.gameExposure * trim, 0.01f, 4096.0f);
            m.confidence = g_nr.exposureOfferedNow ? 1.0f : 0.8f;
            m.valid = true;
            m.sameFrame = g_nr.exposureOfferedNow;
            gameExp = m;
        }

        std::optional<nrfusion::ExposureMeasurement> bufScan;
        const float scanVal = DlssNr::ExposureScan::BestValue();
        const float scanWp = DlssNr::ExposureScan::AnchoredWhitePoint(
            scanVal, cfg.DlssNrScanInverted.value_or_default(),
            cfg.DlssNrScanTrim.value_or_default());
        if (scanWp > 0.0f)
        {
            const auto verdict = DlssNr::ExposureScan::Where();
            nrfusion::ExposureMeasurement m;
            m.source = nrfusion::ExposureSource::BufferScan;
            static std::uint64_t s_dx12ScanFrame = 0;
            m.frameId = ++s_dx12ScanFrame;
            m.whitePoint = scanWp;
            m.valid = true;
            m.sameFrame = false;
            m.confidence = (verdict == DlssNr::ExposureScan::Verdict::Found) ? 0.9f : 0.6f;
            bufScan = m;
        }

        static nrfusion::AdaptiveExposureController s_exposureController;
        const double dtSeconds = State::Instance().lastFGFrameTime > 0.0
            ? State::Instance().lastFGFrameTime * 0.001
            : (State::Instance().presentFrameTime > 0.0 ? State::Instance().presentFrameTime * 0.001 : 1.0 / 60.0);
        g_lastExposureDecision = s_exposureController.Update(gameExp, bufScan, g_nr.reset, dtSeconds);

        if (g_lastExposureDecision.source == nrfusion::ExposureSource::Manual)
            return slider;
        return g_lastExposureDecision.whitePoint;
    }
"""
        replace_once(dx12, dx12_hdr_anchor, dx12_auto_exposure)

    dx12_exp_setting_anchor = "    const bool exposureSettingOn = cfg.DlssNrWhitePointSource.value_or_default() == 1;\n"
    if dx12_exp_setting_anchor in dx12.read_text(encoding="utf-8"):
        replace_once(dx12, dx12_exp_setting_anchor,
                     "    const bool exposureSettingOn = cfg.DlssNrWhitePointSource.value_or_default() == 1 ||\n"
                     "                                   cfg.DlssNrWhitePointSource.value_or_default() == 3;\n")

    dx12_zero_latency_anchor = "    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && frame.ExposureTexture != nullptr)\n"
    if dx12_zero_latency_anchor in dx12.read_text(encoding="utf-8"):
        replace_once(dx12, dx12_zero_latency_anchor,
                     "    if ((cfg.DlssNrWhitePointSource.value_or_default() == 1 ||\n"
                     "         (cfg.DlssNrWhitePointSource.value_or_default() == 3 &&\n"
                     "          g_lastExposureDecision.source == nrfusion::ExposureSource::GameExposure)) &&\n"
                     "        frame.ExposureTexture != nullptr)\n")

    replace_once(dx12,
        "    float workScale = cfg.DlssNrWorkingScale.value_or_default();\n",
        """    nrfusion::AdaptiveSettings fusionSettings;
    const int fusionStoredMode = std::clamp(cfg.DlssNrFusionMode.value_or_default(), 0, 4);
    // 0 Auto, 1 Performance, 3 Best quality, 4 Custom. The former Balanced (2) has no separate
    // contract any more and executes the Auto policy.
    fusionSettings.mode = fusionStoredMode == 3 ? nrfusion::UserMode::Quality
        : fusionStoredMode == 1 ? nrfusion::UserMode::MaxFps
        : fusionStoredMode == 4 ? nrfusion::UserMode::Custom : nrfusion::UserMode::Auto;
    fusionSettings.enabled = fusionSettings.mode != nrfusion::UserMode::Custom;
    fusionSettings.fixedScale = cfg.DlssNrWorkingScale.value_or_default();
    float fusionTargetFps = cfg.DlssNrTargetFps.value_or_default();
    if (!cfg.DlssNrTargetFps.has_value() || fusionTargetFps <= 0.0f)
    {
        const int hz = Util::GetActiveRefreshRate(Util::GetProcessWindow());
        fusionTargetFps = hz > 24 ? static_cast<float>(hz) : 60.0f;
    }
    fusionSettings.targetFps = fusionTargetFps;
    const double fusionPresentIntervalMs = State::Instance().lastFGFrameTime > 0.0
        ? State::Instance().lastFGFrameTime : State::Instance().presentFrameTime;
    const bool fusionFgActive = State::Instance().currentFG != nullptr &&
        State::Instance().currentFG->IsActive() && !State::Instance().currentFG->IsPaused();
    auto& fusionAdapter = nrfusion::OptiScalerAdapter::Instance();
    static unsigned int fusionShapeWidth = 0, fusionShapeHeight = 0;
    if (fusionShapeWidth != width || fusionShapeHeight != height)
    {
        fusionAdapter.ResetForShape(fusionSettings);
        fusionShapeWidth = width; fusionShapeHeight = height;
    }

    nrfusion::GameContext fusionGameCtx{};
    fusionGameCtx.api = nrfusion::GraphicsApi::D3D12;
    fusionGameCtx.is32Bit = false;
    fusionGameCtx.nativeDlss = true;
    fusionGameCtx.rayReconstruction = frame.RayReconstruction;
    fusionGameCtx.frameGeneration = fusionFgActive;

    nrfusion::FrameContext fusionFrameCtx{};
    fusionFrameCtx.frameId = frame.SubmissionEpoch > 0 ? frame.SubmissionEpoch : 1;
    fusionFrameCtx.api = nrfusion::GraphicsApi::D3D12;
    fusionFrameCtx.color.opaqueId = reinterpret_cast<std::uintptr_t>(colour);
    fusionFrameCtx.color.resolution = {width, height};
    fusionFrameCtx.color.format = frame.ColourIsLinearHdr ? nrfusion::ResourceFormat::Rgba16Float : nrfusion::ResourceFormat::Rgba32Float;
    fusionFrameCtx.depth.opaqueId = reinterpret_cast<std::uintptr_t>(depth);
    fusionFrameCtx.depth.resolution = {guideWidth, guideHeight};
    fusionFrameCtx.depth.format = nrfusion::ResourceFormat::D32Float;
    fusionFrameCtx.depthReliable = (depth != nullptr);
    fusionFrameCtx.motionVectors.opaqueId = reinterpret_cast<std::uintptr_t>(motion);
    fusionFrameCtx.motionVectors.resolution = {motionWidth, motionHeight};
    fusionFrameCtx.motionVectors.format = nrfusion::ResourceFormat::Rg16Float;
    fusionFrameCtx.motionVectorSource = nrfusion::MotionSource::Native;
    fusionFrameCtx.motionVectorsReliable = (motion != nullptr);
    if (frame.ExposureTexture != nullptr)
    {
        fusionFrameCtx.exposure.opaqueId = reinterpret_cast<std::uintptr_t>(frame.ExposureTexture);
        fusionFrameCtx.exposure.resolution = {1, 1};
        fusionFrameCtx.exposure.format = nrfusion::ResourceFormat::R32Float;
    }
    fusionFrameCtx.renderResolution = {width, height};
    fusionFrameCtx.outputResolution = {frame.OutputWidth ? frame.OutputWidth : width, frame.OutputHeight ? frame.OutputHeight : height};
    fusionFrameCtx.jitter = {0.0f, 0.0f};
    fusionFrameCtx.cameraCut = frame.Reset;
    fusionFrameCtx.hdr = frame.ColourIsLinearHdr;

    nrfusion::RuntimeCapabilities fusionCaps{};
    fusionCaps.nativeProvider = true;
    fusionCaps.preSr = frame.BeforeUpscale;
    fusionCaps.postSr = !frame.BeforeUpscale;
    fusionCaps.deferredResidual = frame.ResidualAcrossRr;
    fusionCaps.acrossRr = frame.ResidualAcrossRr;
    fusionCaps.nativeMotion = (motion != nullptr);
    fusionCaps.dlssContractMotion = true;
    // These are host facts, not preferences. This path submits NR on the game's graphics command
    // list and has no independent compute-queue or secondary-GPU executor. Advertising either
    // capability would let Auto select a route this host cannot execute.
    fusionCaps.fp8 = true;
    // Report the second precision only where the hardware has it. This is what lets the controller
    // trade precision before NR resolution on Blackwell, and what keeps that trade switched off on
    // Ada, where there is no second format to move to.
    // Hardware alone is not the capability. The hybrid path also needs its weight and kernel
    // assets, verified by hash, and without them the feature refuses to create. Reporting it as
    // available on that basis would let Auto pick a precision that cannot be built, and the
    // failure would land mid-game instead of never being offered.
    fusionCaps.hybridNvfp4 = [] {
        static const bool hardware = [] {
            const auto& gpu = IdentifyGpu::getPrimaryGpu();
            return gpu.vendorId == VendorId::Nvidia &&
                   gpu.nvidiaArchInfo.architecture_id > NV_GPU_ARCHITECTURE_AD100;
        }();
        return hardware && DlssNrNative::HybridAvailable();
    }();
    fusionCaps.asyncCompute = false;
    fusionCaps.secondaryGpu = false;
    fusionCaps.frameGeneration = fusionFgActive;
    if (fusionCaps.frameGeneration) fusionCaps.maxGenerationMultiplier = 2;

    const float fusionScaleBeforeDecision = fusionAdapter.LastDecision().workingScale;
    const auto fusionAutoDecision = fusionAdapter.ResolveAuto(
        fusionGameCtx, fusionFrameCtx, fusionCaps, fusionSettings,
        fusionAdapter.ConsumeTrackedNrGpuMs(), fusionPresentIntervalMs,
        fusionAdapter.TrackedSourceFps(), fusionAdapter.TrackedProcessedFps(), 0.0, 0.0,
        nrfusion::SchedulerMode::Auto, 1, nullptr,
        nrfusion::FrameTimingSource::PresentationInterval);

    if (fusionAutoDecision.performance.changedScale)
    {
        LOG_INFO("NRFusion: scale decision {:.0f}% -> {:.0f}%, NR {:.2f} ms, budget {:.2f} ms, "
                 "state {}, fresh timing {}",
                 fusionScaleBeforeDecision * 100.0f,
                 fusionAutoDecision.workingScale * 100.0f,
                 fusionAutoDecision.performance.effectiveNrCriticalMs,
                 fusionAutoDecision.performance.resolvedNrBudgetMs,
                 static_cast<unsigned>(fusionAutoDecision.performance.state),
                 fusionAutoDecision.performance.telemetryReady ? 1 : 0);
    }

    float workScale = fusionAutoDecision.supported
        ? fusionAutoDecision.workingScale
        : fusionAdapter.ResolveWorkingScale(
            fusionSettings, fusionAdapter.ConsumeTrackedNrGpuMs(), fusionPresentIntervalMs,
            fusionAdapter.TrackedSourceFps(), fusionAdapter.TrackedProcessedFps(), 0.0, 0.0,
            nrfusion::FrameTimingSource::PresentationInterval);
""")


    dx12_text = dx12.read_text(encoding="utf-8")
    runtime_policy_anchor = (
        "void EvaluateInternal(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params,\n"
        "                      bool beforeUpscale, ID3D12CommandQueue* timingQueue, bool rayReconstruction,\n"
        "                      unsigned long long submissionEpoch)\n"
        "{\n"
        "    std::lock_guard<std::recursive_mutex> nrLock(g_nrMutex);\n"
        "    const Config& cfg = *Config::Instance();\n"
    )
    if runtime_policy_anchor in dx12_text:
        runtime_policy = runtime_policy_anchor + """    // NRFusion simple modes are runtime policy, not menu side effects. The overlay never needs to
    // be opened for the performance path to be active; volatile writes preserve zero manual INI tuning.
    if (cfg.DlssNrFusionMode.value_or_default() != 4)
    {
        auto* fusionCfg = Config::Instance();
        // No upscaler means no "before SR" seam; forcing it true makes the seam gate drop every frame.
        fusionCfg->DlssNrRunBeforeSr.set_volatile_value(State::Instance().currentFeature != nullptr);
        fusionCfg->DlssNrPasses.set_volatile_value(1u);
        fusionCfg->DlssNrDeferredDlss.set_volatile_value(false);
        fusionCfg->DlssNrResidualFg.set_volatile_value(false);
        fusionCfg->DlssNrTransfer.set_volatile_value(1u);
        fusionCfg->DlssNrApplyModel.set_volatile_value(true);
        fusionCfg->DlssNrResidualAcrossRr.set_volatile_value(rayReconstruction);
        if (!fusionCfg->DlssNrWhitePointSource.has_value())
            fusionCfg->DlssNrWhitePointSource.set_volatile_value(3u);
    }
"""
        replace_once(dx12, runtime_policy_anchor, runtime_policy)

    dx12_text = dx12.read_text(encoding="utf-8")

    # Keep the existing whole-pass and model timers, and add one bounded interval for the resolve
    # shader. This distinguishes model cost from composition cost on the RTX 4050 without changing
    # the adaptive signal or adding a per-dispatch readback in the hot path.
    stage_globals_anchor = "std::unique_ptr<GpuTime_Dx12> g_gpuTime;\n"
    if stage_globals_anchor in dx12.read_text(encoding="utf-8") and "g_resolveTime" not in dx12.read_text(encoding="utf-8"):
        insert_after(dx12, stage_globals_anchor,
                     "\n// Isolates the final resolve shader for Ada profiling; Auto still uses the whole NR interval.\n"
                     "std::unique_ptr<GpuTime_Dx12> g_resolveTime;\n")
        replace_once(dx12,
                     "std::optional<double> g_lastNgxTime;\n",
                     "std::optional<double> g_lastNgxTime;\n"
                     "std::optional<double> g_lastResolveTime;\n")

    stage_init_anchor = "    if (g_ngxTime == nullptr)\n        g_ngxTime = std::make_unique<GpuTime_Dx12>(device);\n"
    if stage_init_anchor in dx12.read_text(encoding="utf-8") and "g_resolveTime = std::make_unique" not in dx12.read_text(encoding="utf-8"):
        insert_after(dx12, stage_init_anchor,
                     "\n    if (g_resolveTime == nullptr)\n"
                     "        g_resolveTime = std::make_unique<GpuTime_Dx12>(device);\n")

    precision_anchor = "    static unsigned lastPrecision=0;\n    const unsigned precision=cfg.DlssNrPrecision.value_or_default();\n"
    if precision_anchor in dx12_text:
        precision_block = """    auto& fusionPrecisionAdapter = nrfusion::OptiScalerAdapter::Instance();
    const int fusionPrecisionMode = std::clamp(cfg.DlssNrFusionMode.value_or_default(), 0, 4);
    // The portable tuner can qualify a lower-precision candidate, but this host is Ada-validated and
    // ships no such executor. Auto therefore remains FP8 here; a future backend must opt in through
    // an explicit capability and a complete same-workload measurement.
    const bool fusionAutoPrecision = fusionPrecisionMode != 3 && fusionPrecisionMode != 4;
    // Full hybrid validation hashes tens of MB. Keep the hot path free of that cost: only validate
    // once the FP8 baseline is complete and the tuner is actually ready to trial NVFP4. Unknown is
    // treated as available during the baseline because no hybrid code is selected in those phases.
    static int fusionHybridAvailable = -1; // -1 unchecked, 0 unavailable, 1 verified
    static const bool fusionHybridHardware = [] {
        const auto& gpu = IdentifyGpu::getPrimaryGpu();
        // The shipped hybrid bundle contains an sm120 cubin and is documented by the upstream as
        // RTX 50/Blackwell-only. This is retained as a diagnostic fact only: the backend gate below
        // is hard-disabled in the Ada-validated host, so an untested future GPU cannot opt in merely
        // because its architecture enum is newer than Ada.
        return gpu.vendorId == VendorId::Nvidia &&
               gpu.nvidiaArchInfo.architecture_id > NV_GPU_ARCHITECTURE_AD100;
    }();
    if (fusionAutoPrecision && fusionPrecisionAdapter.PrecisionCandidateRequested() &&
        fusionHybridHardware && fusionHybridAvailable < 0)
        fusionHybridAvailable = DlssNrNative::HybridAvailable() ? 1 : 0;
    // No Blackwell/NVFP4 executor is part of this Ada-validated host. Keep the capability hard
    // disabled even if a future GPU reports a matching architecture; enabling it requires a new
    // backend plus a same-workload benchmark on that GPU, neither of which is available here.
    static constexpr bool fusionHybridBackendIntegrated = false;
    const bool fusionCandidateAvailable = fusionHybridBackendIntegrated &&
        fusionHybridHardware && fusionHybridAvailable != 0;
    static bool fusionPrecisionAvailabilityLogged = false;
    if (fusionAutoPrecision && !fusionPrecisionAvailabilityLogged &&
        (!fusionHybridHardware || fusionHybridAvailable >= 0))
    {
        fusionPrecisionAvailabilityLogged = true;
        if (!fusionHybridHardware)
            LOG_INFO("NRFusion: NVFP4 Auto unavailable on this GPU; retaining FP8");
        else if (!fusionCandidateAvailable)
            LOG_INFO("NRFusion: NVFP4 Auto unavailable because hybrid assets failed verification; retaining FP8");
    }
    // The Ada W4A8 state used to live only in greyed menu text, which is why a path that never ran
    // still looked like it was working. It belongs in the log, where it can be read after the fact.
    {
        static std::string adaStatusLogged;
        static unsigned adaStatusLines = 0;
        const std::string adaStatusNow = nrfusion::GetAdaW4A8Status();
        if (adaStatusNow != adaStatusLogged && adaStatusLines < 12)
        {
            adaStatusLogged = adaStatusNow;
            ++adaStatusLines;
            LOG_INFO("NRFusion Ada W4A8: {} (launcher {})", adaStatusNow,
                     nrfusion::HasAdaW4A8CubinLauncher() ? "installed" : "absent");
            if (adaStatusLines == 2)
                LOG_INFO("NRFusion neural kernels registered: {}", nrfusion::NrFunctionNamesSummary());
            if (adaStatusLines >= 4 && (adaStatusLines % 4) == 0)
                LOG_INFO("NRFusion launch shapes: {}", nrfusion::NrLaunchShapeSummary());
        }
    }
    // Custom always treats precision as manual; any mode does once the user pins it in the normal
    // menu. Without a pin, Auto owns the value, which also stops an NVFP4 choice made by Auto from
    // leaking into Best quality.
    const bool fusionPinnedPrecision = cfg.DlssNrPrecisionManual.value_or_default() || fusionPrecisionMode == 4;
    const bool fusionManualHybridRequested = fusionPinnedPrecision &&
        cfg.DlssNrPrecision.value_or_default() == 4;
    const auto fusionManualPrecision = fusionManualHybridRequested && fusionCandidateAvailable
        ? nrfusion::NrPrecision::HybridNvfp4 : nrfusion::NrPrecision::Fp8;
    static bool fusionManualPrecisionFallbackLogged = false;
    if (fusionManualHybridRequested && !fusionCandidateAvailable &&
        !fusionManualPrecisionFallbackLogged)
    {
        fusionManualPrecisionFallbackLogged = true;
        LOG_WARN("NRFusion: manual NVFP4 is unavailable on this GPU/runtime; retaining FP8");
    }
    const auto fusionWantedPrecision = fusionPinnedPrecision
        ? fusionManualPrecision
        : fusionPrecisionAdapter.ResolvePrecision(fusionAutoPrecision, fusionCandidateAvailable,
                                                  fusionManualPrecision);
    if (fusionManualHybridRequested && !fusionCandidateAvailable)
        Config::Instance()->DlssNrPrecision.set_volatile_value(0u);
    else if (!fusionPinnedPrecision && cfg.DlssNrPrecision.value_or_default() != (unsigned) fusionWantedPrecision)
        Config::Instance()->DlssNrPrecision.set_volatile_value((unsigned) fusionWantedPrecision);

    static unsigned lastPrecision=0;
    const unsigned precision=cfg.DlssNrPrecision.value_or_default();
"""
        replace_once(dx12, precision_anchor, precision_block)

        # Precision is a create-time property of the intercepted neural model. Merely changing the
        # config would leave the existing FP8/NVFP4 feature alive and benchmark the same feature twice.
        # Park all model histories on a precision transition so the next creation is genuinely built
        # under the newly selected precision. Surfaces are retained; only feature state is rebuilt.
        precision_rebuild_anchor = """    if(lastPrecision!=precision)
    {
        RetryAfterFailure();
        DeferredSr::Cancel();
        lastPrecision = precision;
    }
"""
        if precision_rebuild_anchor in dx12.read_text(encoding="utf-8"):
            precision_rebuild = """    if(lastPrecision!=precision)
    {
        RetryAfterFailure();
        DeferredSr::Cancel();
        ParkNrFeature(g_nr.feature);
        g_nr.featurePendingSubmission = false;
        for (unsigned int i = 1; i < DlssNr::MaxPassCount; ++i)
        {
            ParkNrFeature(g_nr.passFeature[i]);
            g_nr.passNeedsReset[i] = false;
            g_nr.passCreateFailed[i] = false;
            g_nr.passPendingSubmission[i] = false;
        }
        g_nr.residualHistoryPrimed = false;
        g_nr.residualStoreValid = false;
        lastPrecision = precision;
        LOG_INFO("NRFusion: precision changed to {}; rebuilding the neural feature",
                 precision == 4 ? "NVFP4 hybrid" : "FP8");
    }
"""
            replace_once(dx12, precision_rebuild_anchor, precision_rebuild)

    stage_resolve_anchor = (
        "        const bool resolved = DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer,\n"
        "                                           resolveOriginal, motionIn, exposureTex, resolveTarget, nullptr);\n"
    )
    if stage_resolve_anchor in dx12.read_text(encoding="utf-8"):
        stage_resolve_new = (
            "        const bool fusionResolveTimingActive = g_resolveTime != nullptr;\n"
            "        if (fusionResolveTimingActive)\n"
            "            g_resolveTime->Start(cmdList);\n"
            + stage_resolve_anchor +
            "        if (fusionResolveTimingActive)\n"
            "            g_resolveTime->End(cmdList);\n"
        )
        replace_once(dx12, stage_resolve_anchor, stage_resolve_new)

    dx12_text = dx12.read_text(encoding="utf-8")
    work_anchor = "    if (g_ngxTime != nullptr)\n        g_ngxTime->Start(cmdList);\n"
    if work_anchor in dx12_text:
        insert_after(dx12, work_anchor, """
    const auto fusionWork = fusionAdapter.BeginNrWork(
        frame.SubmissionEpoch, 0, workScale,
        static_cast<std::uint8_t>(cfg.DlssNrPrecision.value_or_default()));
    const bool fusionWorkSubmitted = fusionAdapter.SubmitNrWork(fusionWork);
""")

        create_failure_anchor = """            LOG_ERROR("DLSS-NR create failed: init 0x{:X} ({}), create 0x{:X} ({})", initResult,
                      NgxResultName(initResult), createResult, NgxResultName(createResult));
            device->Release();
            return;
"""
        if create_failure_anchor in dx12.read_text(encoding="utf-8"):
            create_failure_new = """            LOG_ERROR("DLSS-NR create failed: init 0x{:X} ({}), create 0x{:X} ({})", initResult,
                      NgxResultName(initResult), createResult, NgxResultName(createResult));
            if (cfg.DlssNrFusionMode.value_or_default() != 4 && cfg.DlssNrPrecision.value_or_default() == 4)
            {
                fusionAdapter.ReportPrecisionCandidateFailure();
                Config::Instance()->DlssNrPrecision.set_volatile_value(0u);
                g_nr.failed = false; g_nr.reason = "";
                LOG_WARN("NRFusion: NVFP4 trial failed to create; returning to FP8 automatically");
            }
            device->Release();
            return;
"""
            replace_once(dx12, create_failure_anchor, create_failure_new)

        completion_anchor = "    if (result == NVSDK_NGX_Result_Success)\n        ++g_nr.successfulDispatches;\n"
        insert_after(dx12, completion_anchor, """
    if (fusionWorkSubmitted)
    {
        if (result == NVSDK_NGX_Result_Success) fusionAdapter.MapTimedWork(fusionWork);
        else { fusionAdapter.AbandonNrWork(fusionWork); fusionAdapter.MapInvalidTimedAttempt(); }
    }
    else
    {
        fusionAdapter.AbandonNrWork(fusionWork);
        fusionAdapter.MapInvalidTimedAttempt();
    }
""")

        read_anchor = "            if (auto ms = g_gpuTime->ReadGpuTime(queue); ms.has_value())\n                g_lastGpuTime = ms;\n"
        stage_read_extra = ""
        if "g_resolveTime" in dx12.read_text(encoding="utf-8"):
            stage_read_extra = (
                "\n            if (g_resolveTime != nullptr)\n"
                "            {\n"
                "                if (auto resolve = g_resolveTime->ReadGpuTime(queue); resolve.has_value())\n"
                "                    g_lastResolveTime = resolve;\n"
                "            }\n"
            )
        replace_once(dx12, read_anchor, """            // Start calibrating the graphics queue today; the future async compute queue feeds the
            // same adapter. Both timestamp domains are then comparable through the shared QPC clock.
            static ID3D12CommandQueue* fusionClockQueue = nullptr;
            static unsigned int fusionClockSamples = 0;
            static const double fusionQpcHz = [] {
                LARGE_INTEGER frequency {};
                return QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0
                    ? static_cast<double>(frequency.QuadPart) : 0.0;
            }();
            if (fusionClockQueue != queue) { fusionClockQueue = queue; fusionClockSamples = 0; }
            if (fusionQpcHz > 0.0 && (fusionClockSamples < 8 || (g_frames % 600ull) == 0))
            {
                if (fusionAdapter.UpdateD3D12QueueClock(queue, fusionQpcHz)) ++fusionClockSamples;
            }

            if (auto ms = g_gpuTime->ReadGpuTime(queue); ms.has_value())
            {
                g_lastGpuTime = ms;
                fusionAdapter.RetireTimedInterval(ms.value());
            }

""" + stage_read_extra)

        stage_log_anchor = (
            "                const double total = g_lastGpuTime.value();\n"
            "                const double ngx = g_lastNgxTime.value();\n"
            "                LOG_INFO(\"DLSS-NR cost: {:.2f} ms total = {:.2f} ms model + {:.2f} ms ours ({:.0f}% ours)\",\n"
            "                         total, ngx, total - ngx, total > 0.0 ? 100.0 * (total - ngx) / total : 0.0);\n"
        )
        if "g_resolveTime" in dx12.read_text(encoding="utf-8") and stage_log_anchor in dx12.read_text(encoding="utf-8"):
            replace_once(dx12, stage_log_anchor,
                         "                const double total = g_lastGpuTime.value();\n"
                         "                const double ngx = g_lastNgxTime.value();\n"
                         "                const double resolve = g_lastResolveTime.value_or(0.0);\n"
                         "                const double pre = std::max(0.0, total - ngx - resolve);\n"
                         "                LOG_INFO(\"DLSS-NR cost: {:.2f} ms total = {:.2f} ms pre + {:.2f} ms model + \"\n"
                         "                         \"{:.2f} ms resolve ({:.0f}% non-model)\",\n"
                         "                         total, pre, ngx, resolve,\n"
                         "                         total > 0.0 ? 100.0 * (pre + resolve) / total : 0.0);\n")
            stage_condition_old = (
                "            if (g_lastGpuTime.has_value() && g_lastNgxTime.has_value() && g_frames - lastSplitLog > 600)\n"
            )
            stage_condition_new = (
                "            if (g_lastGpuTime.has_value() && g_lastNgxTime.has_value() && g_lastResolveTime.has_value() &&\n"
                "                g_frames - lastSplitLog > 600)\n"
            )
            replace_once(dx12, stage_condition_old, stage_condition_new)

    eval_failure_anchor = """        LOG_ERROR("DLSS-NR evaluate returned 0x{:X} ({}), disabling for this session", (uint32_t) result,
                  NgxResultName((unsigned int) result));
"""
    if eval_failure_anchor in dx12.read_text(encoding="utf-8"):
        eval_failure_new = eval_failure_anchor + """        if (cfg.DlssNrFusionMode.value_or_default() != 4 && cfg.DlssNrPrecision.value_or_default() == 4)
        {
            fusionAdapter.ReportPrecisionCandidateFailure();
            Config::Instance()->DlssNrPrecision.set_volatile_value(0u);
            g_nr.failed = false; g_nr.reason = "";
            LOG_WARN("NRFusion: NVFP4 trial failed to evaluate; returning to FP8 automatically");
        }
"""
        replace_once(dx12, eval_failure_anchor, eval_failure_new)

    reduced_anchor = "    if (reduced && g_nr.colorSmall == nullptr)\n        g_nr.colorSmall = CreateScratch(device, desc.Format, workWidth, workHeight);\n"
    dx12_text = dx12.read_text(encoding="utf-8")
    if reduced_anchor in dx12_text:
        reduced_new = "    if (reduced && g_nr.colorSmall == nullptr)\n    {\n        g_nr.colorSmall = CreateScratch(device, desc.Format, workWidth, workHeight);\n        if (g_nr.colorSmall == nullptr && fusionSettings.enabled && workScale < 0.999f)\n        {\n            const auto fallback = fusionAdapter.ReportWorkingScaleBuildFailure(workScale);\n            if (fallback.has_value())\n            {\n                LOG_WARN(\"NRFusion: reduced NR resources failed at {:.0f}%; retrying at {:.0f}%\",\n                         workScale * 100.0f, fallback.value() * 100.0f);\n                ParkNrResource(g_nr.output); ParkNrResource(g_nr.colorCopy);\n                ParkNrResource(g_nr.hdrCopy); ParkNrResource(g_nr.colorSmall);\n                g_nr.workWidth = g_nr.workHeight = 0;\n                device->Release();\n                return;\n            }\n        }\n    }\n"
        replace_once(dx12, reduced_anchor, reduced_new)

    vk = opti / "dlssnr/DlssNrFeature_Vk.cpp"
    # pch normally covers project headers, but explicit include makes the dependency visible.
    first_include = '#include "pch.h"\n'
    vk_text_current = vk.read_text(encoding="utf-8")
    vk_state_include = "" if "#include <State.h>" in vk_text_current else "#include <State.h>\n"
    vk_helper = (
        "static nrfusion::ExposureDecision g_lastExposureDecisionVk{};\n\n"
        "namespace DlssNr\n"
        "{\n"
        "nrfusion::ExposureDecision LastExposureDecisionVk()\n"
        "{\n"
        "    return g_lastExposureDecisionVk;\n"
        "}\n"
        "}\n\n"
    )
    insert_after(vk, first_include,
                 '#include <nrfusion/OptiScalerAdapter.hpp>\n'
                 '#include <nrfusion/AdaptiveExposureController.hpp>\n' +
                 vk_state_include + vk_helper)

    vk_wp_anchor = """    float whitePoint = cfg.DlssNrWhitePointScale.value_or_default();

    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && g_vk.gameExposure > 1e-6f)
    {
        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
        whitePoint = std::clamp(g_vk.gamePreExposure / g_vk.gameExposure * trim, 0.01f, 4096.0f);
    }
"""
    if vk_wp_anchor in vk.read_text(encoding="utf-8"):
        vk_wp_new = """    float whitePoint = cfg.DlssNrWhitePointScale.value_or_default();

    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && g_vk.gameExposure > 1e-6f)
    {
        const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
        whitePoint = std::clamp(g_vk.gamePreExposure / g_vk.gameExposure * trim, 0.01f, 4096.0f);
    }
    else if (cfg.DlssNrWhitePointSource.value_or_default() == 3)
    {
        std::optional<nrfusion::ExposureMeasurement> gameExp;
        if (g_vk.gameExposure > 1e-6f)
        {
            const float trim = std::clamp(cfg.DlssNrWhitePointTrim.value_or_default(), 0.25f, 4.0f);
            nrfusion::ExposureMeasurement m;
            m.source = nrfusion::ExposureSource::GameExposure;
            static std::uint64_t s_vkExpFrame = 0;
            m.frameId = ++s_vkExpFrame;
            m.whitePoint = std::clamp(g_vk.gamePreExposure / g_vk.gameExposure * trim, 0.01f, 4096.0f);
            m.confidence = 1.0f;
            m.valid = true;
            m.sameFrame = false;
            gameExp = m;
        }

        const double dtSeconds = State::Instance().lastFGFrameTime > 0.0
            ? State::Instance().lastFGFrameTime * 0.001
            : (State::Instance().presentFrameTime > 0.0 ? State::Instance().presentFrameTime * 0.001 : 1.0 / 60.0);

        static nrfusion::AdaptiveExposureController s_exposureControllerVk;
        g_lastExposureDecisionVk = s_exposureControllerVk.Update(gameExp, std::nullopt, g_vk.reset, dtSeconds);

        if (g_lastExposureDecisionVk.source != nrfusion::ExposureSource::Manual)
            whitePoint = g_lastExposureDecisionVk.whitePoint;
    }
"""
        replace_once(vk, vk_wp_anchor, vk_wp_new)

    vk_meter_anchor = "    if (cfg.DlssNrWhitePointSource.value_or_default() == 1 && exposure != nullptr &&\n"
    if vk_meter_anchor in vk.read_text(encoding="utf-8"):
        replace_once(vk, vk_meter_anchor,
                     "    if ((cfg.DlssNrWhitePointSource.value_or_default() == 1 ||\n"
                     "         cfg.DlssNrWhitePointSource.value_or_default() == 3) && exposure != nullptr &&\n")

    replace_once(vk,
        "    float workScale = cfg.DlssNrWorkingScale.value_or_default();\n",
        """    nrfusion::AdaptiveSettings fusionSettings;
    const int fusionStoredMode = std::clamp(cfg.DlssNrFusionMode.value_or_default(), 0, 4);
    // 0 Auto, 1 Performance, 3 Best quality, 4 Custom. The former Balanced (2) has no separate
    // contract any more and executes the Auto policy.
    fusionSettings.mode = fusionStoredMode == 3 ? nrfusion::UserMode::Quality
        : fusionStoredMode == 1 ? nrfusion::UserMode::MaxFps
        : fusionStoredMode == 4 ? nrfusion::UserMode::Custom : nrfusion::UserMode::Auto;
    fusionSettings.enabled = fusionSettings.mode != nrfusion::UserMode::Custom;
    fusionSettings.fixedScale = cfg.DlssNrWorkingScale.value_or_default();
    float fusionVkTargetFps = cfg.DlssNrTargetFps.value_or_default();
    if (!cfg.DlssNrTargetFps.has_value() || fusionVkTargetFps <= 0.0f)
    {
        const int hz = Util::GetActiveRefreshRate(Util::GetProcessWindow());
        fusionVkTargetFps = hz > 24 ? static_cast<float>(hz) : 60.0f;
    }
    fusionSettings.targetFps = fusionVkTargetFps;
    const double fusionPresentIntervalMs = State::Instance().lastFGFrameTime > 0.0
        ? State::Instance().lastFGFrameTime : State::Instance().presentFrameTime;
    auto& fusionAdapter = nrfusion::OptiScalerAdapter::Instance();
    static unsigned int fusionVkShapeWidth = 0, fusionVkShapeHeight = 0;
    if (fusionVkShapeWidth != width || fusionVkShapeHeight != height)
    {
        fusionAdapter.ResetForShape(fusionSettings);
        fusionVkShapeWidth = width; fusionVkShapeHeight = height;
    }
    float workScale = fusionAdapter.ResolveWorkingScale(
        fusionSettings, fusionAdapter.ConsumeTrackedNrGpuMs(), fusionPresentIntervalMs,
        fusionAdapter.TrackedSourceFps(), fusionAdapter.TrackedProcessedFps(), 0.0, 0.0,
        nrfusion::FrameTimingSource::PresentationInterval);
""")

    vk_text = vk.read_text(encoding="utf-8")
    # Native Vulkan checks RunBeforeSR in EvaluateBeforeUpscaleVk before entering EvaluateAtSeamVk.
    # Apply the simple-mode placement policy at that wrapper boundary so Auto works even if the
    # overlay was never opened. Custom deliberately preserves the user's manual placement.
    vk_before_guard = """    handled = false;
    if (!Config::Instance()->DlssNrRunBeforeSr.value_or_default())
        return nullptr;
"""
    if vk_before_guard in vk_text:
        vk_before_guard_new = """    handled = false;
    if (Config::Instance()->DlssNrFusionMode.value_or_default() != 4)
        Config::Instance()->DlssNrRunBeforeSr.set_volatile_value(true);
    if (!Config::Instance()->DlssNrRunBeforeSr.value_or_default())
        return nullptr;
"""
        replace_once(vk, vk_before_guard, vk_before_guard_new)

    vk_text = vk.read_text(encoding="utf-8")
    vk_policy_anchor = "    auto& cfg = *Config::Instance();\n"
    if vk_policy_anchor in vk_text:
        vk_policy = vk_policy_anchor + """    // Same zero-INI performance policy as DX12. Native Vulkan intentionally keeps RR residual
    // disabled because that composition path is not implemented on this backend.
    if (cfg.DlssNrFusionMode.value_or_default() != 4)
    {
        cfg.DlssNrRunBeforeSr.set_volatile_value(true);
        cfg.DlssNrPasses.set_volatile_value(1u);
        cfg.DlssNrDeferredDlss.set_volatile_value(false);
        cfg.DlssNrResidualFg.set_volatile_value(false);
        cfg.DlssNrTransfer.set_volatile_value(1u);
        cfg.DlssNrApplyModel.set_volatile_value(true);
        cfg.DlssNrResidualAcrossRr.set_volatile_value(false);
    }
"""
        replace_once(vk, vk_policy_anchor, vk_policy)

    vk_text = vk.read_text(encoding="utf-8")
    vk_alloc_anchor = "        const bool ok = CreateImage(g_vk.output, workWidth, workHeight, working, true) &&\n                        (passes == 1 || CreateImage(g_vk.scratch, workWidth, workHeight, working, true)) &&\n                        CreateImage(g_vk.proxy, width, height, working, true) &&\n                        CreateImage(g_vk.keep, width, height, working, true) &&\n                        (!beforeSr || CreateImage(g_vk.preColor, width, height, working, false)) &&\n                        (!reduced || CreateImage(g_vk.proxySmall, workWidth, workHeight, working, true)) &&\n                        (workScale <= 1.0f || CreateImage(g_vk.outputNative, width, height, working, true));\n\n        if (!ok)\n        {\n            Fail(\"the pass could not allocate its own surfaces\");\n            return;\n        }\n"
    if vk_alloc_anchor in vk_text:
        vk_alloc_new = "        const bool baseOk = CreateImage(g_vk.output, workWidth, workHeight, working, true) &&\n                            (passes == 1 || CreateImage(g_vk.scratch, workWidth, workHeight, working, true)) &&\n                            CreateImage(g_vk.proxy, width, height, working, true) &&\n                            CreateImage(g_vk.keep, width, height, working, true) &&\n                            (!beforeSr || CreateImage(g_vk.preColor, width, height, working, false));\n        const bool reducedOk = !reduced || CreateImage(g_vk.proxySmall, workWidth, workHeight, working, true);\n        const bool superOk = workScale <= 1.0f || CreateImage(g_vk.outputNative, width, height, working, true);\n\n        if (baseOk && !reducedOk && fusionSettings.enabled && workScale < 0.999f)\n        {\n            const auto fallback = fusionAdapter.ReportWorkingScaleBuildFailure(workScale);\n            if (fallback.has_value())\n            {\n                LOG_WARN(\"NRFusion: Vulkan reduced resources failed at {:.0f}%; retrying at {:.0f}%\",\n                         workScale * 100.0f, fallback.value() * 100.0f);\n                DestroyImage(g_vk.output); DestroyImage(g_vk.scratch); DestroyImage(g_vk.proxy);\n                DestroyImage(g_vk.keep); DestroyImage(g_vk.preColor); DestroyImage(g_vk.proxySmall);\n                DestroyImage(g_vk.outputNative);\n                return;\n            }\n        }\n        if (!baseOk || !reducedOk || !superOk)\n        {\n            Fail(\"the pass could not allocate its own surfaces\");\n            return;\n        }\n"
        replace_once(vk, vk_alloc_anchor, vk_alloc_new)

    # Vulkan exposes the exact timestamp-ring slot, so map WorkId by slot rather than by FIFO.
    # This is required because the first retired slot is not necessarily the first slot recorded.
    vk_text = vk.read_text(encoding="utf-8")
    vk_timing_slot_anchor = "    const uint32_t timingSlot = (uint32_t) (g_vk.timedFrames % kTimingSlots);\n"
    vk_model_anchor = "    OwnedImage* answer = &g_vk.output;\n    OwnedImage* input = modelInput;\n    int evaluated = 1;\n"
    if vk_timing_slot_anchor in vk_text and vk_model_anchor in vk_text:
        insert_after(vk, vk_timing_slot_anchor,
                     "    if (g_vk.queryPool != VK_NULL_HANDLE)\n"
                     "        fusionAdapter.ClearTimedSlot(timingSlot);\n")
        insert_after(vk, vk_model_anchor,
                     "    static std::uint64_t fusionVkSourceWork = 0;\n"
                     "    const auto fusionVkWork = fusionAdapter.BeginNrWork(++fusionVkSourceWork,\n"
                     "        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(params)), workScale);\n"
                     "    const bool fusionVkSubmitted = fusionAdapter.SubmitNrWork(fusionVkWork);\n")

        vk_eval_fail = "    if (evaluated != 1)\n    {\n        LOG_ERROR(\"DLSS-NR Vulkan: evaluate returned {}\", evaluated);\n        Fail(\"the model refused to evaluate\");\n        return;\n    }\n"
        vk_eval_fail_new = "    if (evaluated != 1)\n    {\n        if (fusionVkSubmitted) fusionAdapter.AbandonNrWork(fusionVkWork);\n        LOG_ERROR(\"DLSS-NR Vulkan: evaluate returned {}\", evaluated);\n        Fail(\"the model refused to evaluate\");\n        return;\n    }\n"
        replace_once(vk, vk_eval_fail, vk_eval_fail_new)

        replace_once(vk,
            '        Fail("the resolve dispatch failed");\n        return;\n',
            '        if (fusionVkSubmitted) fusionAdapter.AbandonNrWork(fusionVkWork);\n'
            '        Fail("the resolve dispatch failed");\n        return;\n')

        insert_after(vk, "        g_vk.timedFrames++;\n",
                     "        if (fusionVkSubmitted) fusionAdapter.MapTimedSlot(timingSlot, fusionVkWork);\n")

        vk_read_old = """            if (vkGetQueryPoolResults(device, g_vk.queryPool, readSlot * 2, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT) == VK_SUCCESS &&
                ticks[1] > ticks[0])
            {
                const double ms = (double) (ticks[1] - ticks[0]) * (double) g_vk.timestampPeriod / 1e6;

                // A pass that appears to have taken over a second did not; the queue was reset under
                // it or the pair straddled a device change.
                if (ms > 0.0 && ms < 1000.0)
                    g_vk.lastGpuTime = ms;
            }
"""
        vk_read_new = """            const VkResult fusionTimingResult =
                vkGetQueryPoolResults(device, g_vk.queryPool, readSlot * 2, 2, sizeof(ticks), ticks, sizeof(uint64_t),
                                      VK_QUERY_RESULT_64_BIT);
            if (fusionTimingResult == VK_SUCCESS)
            {
                double fusionRetiredGpuMs = 0.0;
                if (ticks[1] > ticks[0])
                {
                    const double ms = (double) (ticks[1] - ticks[0]) * (double) g_vk.timestampPeriod / 1e6;
                    // A pass that appears to have taken over a second did not; the queue was reset under
                    // it or the pair straddled a device change.
                    if (ms > 0.0 && ms < 1000.0)
                    {
                        g_vk.lastGpuTime = ms;
                        fusionRetiredGpuMs = ms;
                    }
                }
                // Completion identity is independent of timing validity. A retired query proves this
                // exact slot finished even when its numeric timestamp pair is unusable.
                fusionAdapter.RetireTimedSlot(readSlot, fusionRetiredGpuMs);
            }
"""
        replace_once(vk, vk_read_old, vk_read_new)

        insert_after(vk, "    }\n\n    static bool reported = false;\n",
                     "    if (g_vk.queryPool == VK_NULL_HANDLE && fusionVkSubmitted)\n"
                     "        fusionAdapter.CompleteNrWork(fusionVkWork);\n\n")

    menu = opti / "dlssnr/DlssNr_Menu.cpp"
    insert_after(menu, '#include <Config.h>\n',
                 '#include <nrfusion/OptiScalerAdapter.hpp>\n'
                 '#include <nrfusion/Dlss5NeuralRendering.hpp>\n'
                 '#include <nrfusion/AdaptiveExposure.hpp>\n'
                 '#include <nrfusion/NrKernelProfile.hpp>\n'
                 '#include <nrfusion/AdaW4A8Interceptor.hpp>\n'
                 '#include <string>\n')
    # NRFusion normal UI: Mode + target FPS + one status line. All original knobs live under
    # Custom -> Advanced controls, so there is no manual INI workflow.
    simple_anchor = '        HelpMarker("Enhance lighting and material appearance with the NR model. Placement selects before or after upscaling.\\nRequires nvngx_dlssnr.dll plus the included nvngx.dll_dlssnr.dll helper.");\n'
    simple_ui = r"""

        const auto activeFeature = State::Instance().currentFeature;
        const bool rayReconstruction = activeFeature && activeFeature->GetUpscalerType() == Upscaler::DLSSD;
        const bool nativeVkNr = activeFeature && activeFeature->Api() == API::Vulkan && !activeFeature->IsWithDx12();

        // The three adaptive modes share one ceiling and differ only in the floor: how much neural
        // resolution each is willing to spend to hold the target. Custom is the manual escape and
        // owns the percentage directly, so it has no target to chase.
        static const char* fusionModes[] = { "Auto", "Best quality", "Performance", "Custom" };
        const int storedFusionMode = (int) std::clamp(config->DlssNrFusionMode.value_or_default(), 0, 4);
        // Stored values keep their old meaning so an existing OptiScaler.ini still reads correctly:
        // 0 Auto, 1 Performance (the former Max FPS), 3 Best quality, 4 Custom. The former Balanced
        // has no separate contract any more and reads as Auto.
        int fusionMode = storedFusionMode == 3 ? 1 : storedFusionMode == 1 ? 2 : storedFusionMode == 4 ? 3 : 0;
        const bool legacyBalancedMode = storedFusionMode == 2;
        if (ImGui::Combo("Mode", &fusionMode, fusionModes, IM_ARRAYSIZE(fusionModes)))
            config->DlssNrFusionMode = fusionMode == 1 ? 3 : fusionMode == 2 ? 1 : fusionMode == 3 ? 4 : 0;
        const bool fusionCustomMode = fusionMode == 3;

        static const char* fusionModeHelp[] = {
            "Recommended: uses the whole range. Climbs to 100% when there is headroom, down to 42% to hold the target.",
            "Never goes below 67%: prefers missing the target to spending neural detail.",
            "Goes down to 35% and reacts sooner, for when holding the frame rate matters more than detail.",
            "You choose the neural resolution below. Adaptation is off and the target FPS does not apply."
        };
        ImGui::TextDisabled("%s", fusionModeHelp[fusionMode]);
        if (legacyBalancedMode)
            ImGui::TextDisabled("Balanced is gone; this profile now behaves as Auto.");

        // Kernel timing, reachable from inside the game because that is the only place the
        // neural kernels run. Off until asked: the hook costs nothing while it is not installed.
        if (fusionCustomMode)
        {
            auto& fusionProfiler = nrfusion::NrKernelProfiler::Instance();
            const bool fusionProfiling = fusionProfiler.Running();
            if (ImGui::Button(fusionProfiling ? "Stop kernel timing" : "Measure NR kernels"))
            {
                if (fusionProfiling) fusionProfiler.Stop();
                else if (!fusionProfiler.Start())
                    LOG_WARN("NRFusion: kernel timing unavailable (no CUDA driver entry points)");
            }
            if (fusionProfiling)
            {
                ImGui::SameLine();
                if (ImGui::Button("Copy table"))
                    ImGui::SetClipboardText(fusionProfiler.FormatReport().c_str());
                const auto fusionReport = fusionProfiler.Report();
                ImGui::TextDisabled("%llu quadros | %.3f ms/quadro no passe neural | %zu kernels",
                                    (unsigned long long) fusionReport.frames,
                                    fusionReport.measuredMsPerFrame, fusionReport.kernels.size());
                for (size_t i = 0; i < fusionReport.kernels.size() && i < 8; ++i)
                {
                    const auto& k = fusionReport.kernels[i];
                    ImGui::TextDisabled("  %5.1f%%  %.4f ms/quadro  %s",
                                        fusionReport.shareOf(k) * 100.0,
                                        fusionReport.frames ? k.totalMs / (double) fusionReport.frames : 0.0,
                                        k.name.c_str());
                }
            }
        }

        if (fusionCustomMode)
        {
            // Custom is the percentage, not a frame-rate goal. Same commit-on-release discipline the
            // upstream slider uses: every distinct value rebuilds the model, and committing on each
            // pixel of a drag rebuilds it dozens of times a second.
            static int fusionPendingScale = -1;
            int fusionScalePercent = fusionPendingScale >= 0
                ? fusionPendingScale
                : (int) lroundf(config->DlssNrWorkingScale.value_or_default() * 100.0f);
            if (ImGui::SliderInt("Neural resolution", &fusionScalePercent, 25, 200, "%d%%"))
                fusionPendingScale = fusionScalePercent;
            if (ImGui::IsItemDeactivatedAfterEdit() && fusionPendingScale >= 0)
            {
                config->DlssNrWorkingScale = std::clamp(fusionPendingScale, 25, 200) / 100.0f;
                fusionPendingScale = -1;
            }
            ImGui::TextDisabled("Fixed neural resolution. 50%% halves width and height; above 100%% costs more.");
        }
        else
        {
            const int detectedDisplayHz = Util::GetActiveRefreshRate(Util::GetProcessWindow());
            const float autoDisplayFps = detectedDisplayHz > 24 ? static_cast<float>(detectedDisplayHz) : 60.0f;
            // A new target is a new budget and the controller restarts its ladder on it. Commit when
            // the handle is released, so a drag does not restart that ladder on every pixel.
            static float fusionPendingTargetFps = -1.0f;
            float targetFps = fusionPendingTargetFps > 0.0f
                ? fusionPendingTargetFps
                : (config->DlssNrTargetFps.has_value() ? config->DlssNrTargetFps.value() : autoDisplayFps);
            if (ImGui::SliderFloat("Target FPS", &targetFps, 30.0f, 360.0f, "%.0f"))
                fusionPendingTargetFps = targetFps;
            if (ImGui::IsItemDeactivatedAfterEdit() && fusionPendingTargetFps > 0.0f)
            {
                config->DlssNrTargetFps = std::clamp(fusionPendingTargetFps, 30.0f, 360.0f);
                fusionPendingTargetFps = -1.0f;
            }
            ImGui::SameLine();
            if (ImGui::Button("Auto (Display Hz)"))
            {
                config->DlssNrTargetFps = autoDisplayFps;
                fusionPendingTargetFps = -1.0f;
            }
        }

        // Precision, listed from what this GPU actually has. A format the hardware lacks is not a
        // slower option, it is a dead control, so a single-format GPU shows a plain line instead of
        // a menu. "Auto" lets the runtime spend precision before it spends NR resolution when the
        // pass goes over budget; dropping WorkingScale costs detail everywhere, dropping precision
        // costs fidelity in the neural pass alone.
        {
            // Ask the adapter rather than re-detecting the GPU here: it was told the device's
            // capabilities by the render path and is the single source of truth for them.
            const auto fusionPrecisions = nrfusion::OptiScalerAdapter::Instance().SupportedPrecisions();
            const bool isAdaGpu = nrfusion::IsAdaSm89Architecture(nullptr);
            if (fusionPrecisions.size() > 1)
            {
                static const char* precisionNames[] = { "Auto", "FP8", "Hybrid NVFP4" };
                const unsigned stored = config->DlssNrPrecision.value_or_default();
                int precisionChoice = config->DlssNrPrecisionManual.value_or_default() ? (stored == 4 ? 2 : 1) : 0;
                if (ImGui::Combo("Precision", &precisionChoice, precisionNames, IM_ARRAYSIZE(precisionNames)))
                {
                    config->DlssNrPrecisionManual = precisionChoice != 0;
                    if (precisionChoice != 0) config->DlssNrPrecision = (unsigned) (precisionChoice == 2 ? 4 : 0);
                }
                ImGui::TextDisabled("%s", precisionChoice == 0
                    ? "Trades precision before NR resolution when the pass runs over budget."
                    : (precisionChoice == 2 ? "Cheapest neural pass; fidelity baseline is FP8."
                                            : "Fidelity baseline."));
            }
            else if (isAdaGpu)
            {
                static bool s_adaW4A8Synced = false;
                if (!s_adaW4A8Synced && config->DlssNrAdaW4A8.has_value())
                {
                    nrfusion::SetAdaW4A8Enabled(config->DlssNrAdaW4A8.value());
                    s_adaW4A8Synced = true;
                }
                static const char* precisionNamesAda[] = { "Auto (W4A8)", "NVIDIA (FP8)", "W4A8 + FP8 correction" };
                int precisionChoiceAda = config->DlssNrPrecisionManual.value_or_default()
                    ? (nrfusion::IsAdaW4A8Enabled() ? 2 : 1)
                    : 0;
                if (ImGui::Combo("Precision", &precisionChoiceAda, precisionNamesAda, IM_ARRAYSIZE(precisionNamesAda)))
                {
                    config->DlssNrPrecisionManual = (precisionChoiceAda != 0);
                    config->DlssNrPrecision = 0u;
                    const bool w4a8On = (precisionChoiceAda != 1);
                    nrfusion::SetAdaW4A8Enabled(w4a8On);
                    config->DlssNrAdaW4A8 = w4a8On;
                }
                ImGui::TextDisabled("%s", precisionChoiceAda == 0
                    ? "Auto: runs optimized W4A8 with FP8 correction, auto-selecting fastest backend."
                    : (precisionChoiceAda == 2 ? "Forces W4A8 + FP8 correction."
                                                : "Forces standard NVIDIA FP8 baseline."));
            }
            else
            {
                ImGui::Text("Precision      FP8");
                // Two different reasons land here and they are not interchangeable: hardware
                // with one format, and hardware with two whose assets are missing. The host's
                // own status line distinguishes them, and asking it avoids re-detecting the GPU
                // in a file that does not include the vendor headers.
                const auto fusionHybridStatus = DlssNrNative::Status();
                if (fusionHybridStatus.find("Hybrid") != std::string::npos)
                {
                    ImGui::TextDisabled("Hybrid NVFP4 nao esta disponivel nesta instalacao.");
                    ImGui::TextDisabled("  %s", fusionHybridStatus.c_str());
                }
                else
                {
                    ImGui::TextDisabled("This GPU supports one precision, so there is nothing to trade.");
                }
            }
        }

        // DLSS 5 Neural Rendering visual tuning: independent of Mode/WorkingScale/scheduler/
        // precision/motion/provider/transport. Requested values are read from the same native
        // Style/Intensity/LocalStructure/SkinStructure/AutoMask config fields Pass 1 already uses
        // under Custom -> Advanced controls, so both views always agree and persistence is free.
        // Validated through nrfusion::ResolveDlss5NeuralRendering so an inactive feature or a
        // corrupted persisted value fails closed to Default/Auto instead of forcing an override
        // the runtime never confirmed it accepts. No internal model/NR preset is exposed here.
        nrfusion::Dlss5NrCapabilities dlss5NrCaps;
        dlss5NrCaps.style = enabled;
        dlss5NrCaps.intensity = enabled;
        dlss5NrCaps.localStructure = enabled;
        dlss5NrCaps.skinStructure = enabled;
        dlss5NrCaps.automaticMask = enabled;

        nrfusion::Dlss5NeuralRenderingSettings dlss5NrRequested;
        if (config->DlssNrStyle.has_value() && config->DlssNrStyle.value() == 1u)
            dlss5NrRequested.style = nrfusion::Dlss5Style::Natural;
        else if (config->DlssNrStyle.has_value() && config->DlssNrStyle.value() == 2u)
            dlss5NrRequested.style = nrfusion::Dlss5Style::Cinematic;
        else
            dlss5NrRequested.style = nrfusion::Dlss5Style::Default;
        dlss5NrRequested.intensity = config->DlssNrIntensity.value_or_default();
        dlss5NrRequested.localStructure = config->DlssNrLocalStructure.value_or_default();
        if (config->DlssNrSkinStructure.value_or_default() > -0.999f)
            dlss5NrRequested.skinStructure = config->DlssNrSkinStructure.value_or_default();
        if (config->DlssNrAutoMask.has_value())
            dlss5NrRequested.automaticMask =
                config->DlssNrAutoMask.value() ? nrfusion::TriState::On : nrfusion::TriState::Off;

        const auto dlss5NrApplied = nrfusion::ResolveDlss5NeuralRendering(dlss5NrRequested, dlss5NrCaps);

        ImGui::Spacing();
        ImGui::SeparatorText("DLSS 5 Neural Rendering");
        if (!enabled)
        {
            ImGui::TextDisabled("Enable Neural Rendering above to adjust these.");
        }
        else
        {
            // Each control is gated on its own reported capability (not just the umbrella
            // "enabled") and disabled with the same tooltip when that specific parameter is
            // not confirmed supported. Today all five capabilities mirror "enabled" because the
            // NGX parameter object exposes no per-key support query; per-field gating is kept
            // independent so a future runtime with finer-grained detection needs no UI change.
            ImGui::BeginDisabled(!dlss5NrCaps.style);
            static const char* dlss5NrStyles[] = { "Default", "Natural", "Cinematic" };
            int dlss5NrStyleIdx = dlss5NrApplied.applied.style == nrfusion::Dlss5Style::Natural ? 1
                : dlss5NrApplied.applied.style == nrfusion::Dlss5Style::Cinematic ? 2 : 0;
            if (ImGui::Combo("Style", &dlss5NrStyleIdx, dlss5NrStyles, IM_ARRAYSIZE(dlss5NrStyles)))
                config->DlssNrStyle = dlss5NrStyleIdx == 0 ? std::optional<uint32_t> {}
                                                            : std::optional<uint32_t>((uint32_t) dlss5NrStyleIdx);
            HelpMarker(dlss5NrCaps.style
                ? "Appearance profile. Default preserves the runtime's own behavior. Intensity controls the strength of Natural/Cinematic."
                : "Not supported by current DLSS runtime");
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!dlss5NrCaps.intensity);
            DeferredSlider("Intensity", &config->DlssNrIntensity, 0.0f, 2.0f, 1.0f);
            if (!dlss5NrCaps.intensity)
                HelpMarker("Not supported by current DLSS runtime");
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!dlss5NrCaps.localStructure);
            DeferredSlider("Local Structure", &config->DlssNrLocalStructure, 0.0f, 2.0f, 1.0f);
            if (!dlss5NrCaps.localStructure)
                HelpMarker("Not supported by current DLSS runtime");
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!dlss5NrCaps.skinStructure);
            static const char* skinModes[] = { "Auto", "Manual" };
            int skinModeIdx = dlss5NrApplied.applied.skinStructure.has_value() ? 1 : 0;
            if (ImGui::Combo("Skin Structure", &skinModeIdx, skinModes, IM_ARRAYSIZE(skinModes)))
                config->DlssNrSkinStructure =
                    skinModeIdx == 0 ? -1.0f : std::max(config->DlssNrSkinStructure.value_or_default(), 0.0f);
            HelpMarker(dlss5NrCaps.skinStructure
                ? "Auto follows Local Structure, the runtime's own default. Manual overrides fine detail on pixels the model identifies as skin."
                : "Not supported by current DLSS runtime");
            if (skinModeIdx == 1)
                DeferredSlider("Skin Structure value", &config->DlssNrSkinStructure, -1.0f, 2.0f, -1.0f);
            ImGui::EndDisabled();

            ImGui::BeginDisabled(!dlss5NrCaps.automaticMask);
            static const char* maskModes[] = { "Auto", "Off", "On" };
            int maskModeIdx = dlss5NrApplied.applied.automaticMask == nrfusion::TriState::Off ? 1
                : dlss5NrApplied.applied.automaticMask == nrfusion::TriState::On ? 2 : 0;
            if (ImGui::Combo("Automatic Mask", &maskModeIdx, maskModes, IM_ARRAYSIZE(maskModes)))
                config->DlssNrAutoMask =
                    maskModeIdx == 0 ? std::optional<bool> {} : std::optional<bool>(maskModeIdx == 2);
            HelpMarker(dlss5NrCaps.automaticMask
                ? "Auto preserves the runtime/game default. Off/On force the model's learned skin mask off or on."
                : "Not supported by current DLSS runtime");
            ImGui::EndDisabled();
        }
        ImGui::Spacing();

        const auto fusionDecision = nrfusion::OptiScalerAdapter::Instance().LastDecision();
        if (enabled && !fusionDecision.telemetryReady)
            ImGui::TextDisabled("Measuring fresh NR timing; scale held at %.0f%%",
                               fusionDecision.workingScale * 100.0f);
        else if (enabled && fusionDecision.effectiveNrCriticalMs > 0.0)
        {
            if (fusionDecision.recommendedSourceCapFps > 0.0)
                ImGui::Text("Pipeline protected: render cap %.0f FPS | NR %.0f%% | %.2f ms",
                            fusionDecision.recommendedSourceCapFps,
                            fusionDecision.workingScale * 100.0f,
                            fusionDecision.effectiveNrCriticalMs);
            else if (fusionDecision.changedScale || fusionDecision.overBudget)
                ImGui::Text("Adjusting: NR %.0f%% | %.2f ms",
                            fusionDecision.workingScale * 100.0f,
                            fusionDecision.effectiveNrCriticalMs);
            else if (fusionDecision.hasHeadroom && fusionDecision.workingScale < 0.999f)
                ImGui::Text("More quality available: NR %.0f%% | %.2f ms",
                            fusionDecision.workingScale * 100.0f,
                            fusionDecision.effectiveNrCriticalMs);
            else if (fusionDecision.state == nrfusion::AdaptiveState::ExternalPressure)
                ImGui::Text("External pressure: NR held at %.0f%% | %.2f ms",
                            fusionDecision.workingScale * 100.0f,
                            fusionDecision.effectiveNrCriticalMs);
            else
                ImGui::Text("Stable: NR %.0f%% | %.2f ms",
                            fusionDecision.workingScale * 100.0f,
                            fusionDecision.effectiveNrCriticalMs);
        }
        else if (enabled)
            ImGui::TextDisabled("Starting Neural Rendering...");

        if (ImGui::Button("Copy diagnostics"))
        {
            std::string fusionDiagnostics = nrfusion::OptiScalerAdapter::Instance().DiagnosticsText();
            const double fusionRenderedMs = State::Instance().lastFGFrameTime > 0.0
                ? State::Instance().lastFGFrameTime : State::Instance().presentFrameTime;
            const double fusionDisplayedMs = State::Instance().presentFrameTime;
            const double fusionRenderedFps = fusionRenderedMs > 0.0 ? 1000.0 / fusionRenderedMs : 0.0;
            const double fusionDisplayedFps = fusionDisplayedMs > 0.0 ? 1000.0 / fusionDisplayedMs : 0.0;
            fusionDiagnostics += "hostApi=" + std::string(nativeVkNr ? "vulkan" : "d3d12") +
                                 " provider=native\nrenderedFps=" + std::to_string(fusionRenderedFps) +
                                 " displayedFps=" + std::to_string(fusionDisplayedFps) + "\n";
            // Make the Ada-validated capability boundary visible in the copied report. A route
            // that is unavailable must not look like an Auto decision that merely preferred FP8.
            fusionDiagnostics += "capability.fp8=1 capability.hybridNvfp4=0 "
                                 "capability.asyncCompute=0 capability.secondaryGpu=0 "
                                 "precisionBackend=fp8-ada-validated schedulerBackend=serialized\n";
            fusionDiagnostics += "dlss5nr.style.requested=" +
                std::string(dlss5NrApplied.requested.style == nrfusion::Dlss5Style::Natural ? "natural"
                    : dlss5NrApplied.requested.style == nrfusion::Dlss5Style::Cinematic ? "cinematic" : "default") +
                " applied=" +
                std::string(dlss5NrApplied.applied.style == nrfusion::Dlss5Style::Natural ? "natural"
                    : dlss5NrApplied.applied.style == nrfusion::Dlss5Style::Cinematic ? "cinematic" : "default") +
                " supported=" + std::string(dlss5NrApplied.styleSupported ? "yes" : "no") + "\n";
            fusionDiagnostics += "dlss5nr.intensity.requested=" + std::to_string(dlss5NrApplied.requested.intensity) +
                " applied=" + std::to_string(dlss5NrApplied.applied.intensity) +
                " supported=" + std::string(dlss5NrApplied.intensitySupported ? "yes" : "no") + "\n";
            fusionDiagnostics += "dlss5nr.localStructure.requested=" + std::to_string(dlss5NrApplied.requested.localStructure) +
                " applied=" + std::to_string(dlss5NrApplied.applied.localStructure) +
                " supported=" + std::string(dlss5NrApplied.localStructureSupported ? "yes" : "no") + "\n";
            fusionDiagnostics += "dlss5nr.skinStructure.applied=" +
                (dlss5NrApplied.applied.skinStructure.has_value()
                    ? std::to_string(dlss5NrApplied.applied.skinStructure.value()) : std::string("auto")) +
                " supported=" + std::string(dlss5NrApplied.skinStructureSupported ? "yes" : "no") + "\n";
            fusionDiagnostics += "dlss5nr.automaticMask.applied=" +
                std::string(dlss5NrApplied.applied.automaticMask == nrfusion::TriState::On ? "on"
                    : dlss5NrApplied.applied.automaticMask == nrfusion::TriState::Off ? "off" : "auto") +
                " supported=" + std::string(dlss5NrApplied.automaticMaskSupported ? "yes" : "no") + "\n";
            ImGui::SetClipboardText(fusionDiagnostics.c_str());
        }

        if (fusionCustomMode && ImGui::TreeNode("Advanced controls"))
        {
"""
    insert_after(menu, simple_anchor, simple_ui)

    replace_once(menu,
        '        bool beforeSr = config->DlssNrRunBeforeSr.value_or_default();\n'
        '        const auto activeFeature = State::Instance().currentFeature;\n'
        '        const bool rayReconstruction = activeFeature && activeFeature->GetUpscalerType() == Upscaler::DLSSD;\n',
        '        bool beforeSr = config->DlssNrRunBeforeSr.value_or_default();\n')

    replace_once(menu,
        '        ImGui::PopItemWidth();\n',
        '        ImGui::PopItemWidth();\n        ImGui::TreePop();\n        }\n')

    # Matched-residual availability follows the resolved automatic scale in simple modes.
    # Custom disables NRFusion adaptation, so the upstream WorkingScale slider is authoritative.
    replace_once(menu,
        '            const bool reduced = config->DlssNrWorkingScale.value_or_default() < 0.999f;\n',
        '            const bool fusionAutomatic = config->DlssNrFusionMode.value_or_default() != 4;\n'
        '            const float effectiveScale = fusionAutomatic\n'
        '                ? nrfusion::OptiScalerAdapter::Instance().LastDecision().workingScale\n'
        '                : config->DlssNrWorkingScale.value_or_default();\n'
        '            const bool reduced = effectiveScale < 0.999f;\n')

    menu_sources_old = """            static const char* sourceNames[] = { "Manual paper white", "Game exposure",
                                                 "Scanned exposure (experimental)" };

            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 2)
                source = 0;
"""
    menu_sources_new = """            static const char* sourceNames[] = { "Manual paper white", "Game exposure",
                                                 "Scanned exposure (experimental)", "Auto" };

            int source = (int) config->DlssNrWhitePointSource.value_or_default();

            if (source < 0 || source > 3)
                source = 0;
"""
    if menu_sources_old in menu.read_text(encoding="utf-8"):
        replace_once(menu, menu_sources_old, menu_sources_new)

    menu_have_exposure_old = """            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   "Game exposure is available.");
            }
"""
    menu_have_exposure_new = """            else if (source == 3)
            {
                const auto decision = vk ? DlssNr::LastExposureDecisionVk() : DlssNr::LastExposureDecision();
                const char* activeName = "Manual paper white";
                if (decision.source == nrfusion::ExposureSource::GameExposure)
                    activeName = "Game exposure";
                else if (decision.source == nrfusion::ExposureSource::BufferScan)
                    activeName = "Scanned exposure";

                if (decision.source == nrfusion::ExposureSource::Manual)
                {
                    ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.25f, 1.0f),
                                       "Auto source: %s (white point %.2f). Fallback: %s",
                                       activeName, decision.whitePoint, decision.fallbackReason);
                }
                else
                {
                    ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                       "Auto source: %s (white point %.2f, confidence %.2f)",
                                       activeName, decision.whitePoint, decision.confidence);
                }
            }
            else if (haveExposure)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.8f, 0.45f, 1.0f),
                                   "Game exposure is available.");
            }
"""
    if menu_have_exposure_old in menu.read_text(encoding="utf-8"):
        replace_once(menu, menu_have_exposure_old, menu_have_exposure_new)

    menu_precision_adv_old = (
        '        bool deferredDlss = config->DlssNrDeferredDlss.value_or_default();\n'
        '        int precisionChoice = config->DlssNrPrecision.value_or_default() == 4 ? 1 : 0;\n'
        '        const char* precisions[] = { "NVIDIA (FP8)", "Experimental (FP8+NVFP4 hybrid)" };\n'
        '        if (ImGui::Combo("Model precision", &precisionChoice, precisions, IM_ARRAYSIZE(precisions)))\n'
        '            config->DlssNrPrecision = precisionChoice == 1 ? 4u : 0u;\n'
        '        HelpMarker("NVIDIA: original FP8 model (default), with some sensitive operations kept at higher precision.\\nExperimental: this fork\'s FP8+NVFP4 hybrid for RTX 50 GPUs; output may differ slightly.");\n'
        '        if (precisionChoice > 0)\n'
        '        {\n'
        '            ImGui::TextUnformatted(enabled && DlssNrNative::IsActive() ? "Hybrid: active" : "Hybrid: inactive");\n'
        '            ImGui::TextWrapped("Loading may pause the game and look like a freeze. Please wait.");\n'
        '        }\n'
        '        // Keep failure details in the log without displaying changing kernel counters in the menu.\n'
        '        auto hybridStatus = DlssNrNative::Status();\n'
        '        hybridStatus = hybridStatus.substr(0, hybridStatus.find(" |"));\n'
        '        static std::string lastHybridWarning;\n'
        '        if (hybridStatus.rfind("Restart required:", 0) == 0 || hybridStatus.find("fallback") != std::string::npos)\n'
        '        {\n'
        '            if (hybridStatus != lastHybridWarning)\n'
        '                LOG_WARN("Hybrid: {}", hybridStatus);\n'
        '            lastHybridWarning = hybridStatus;\n'
        '        }\n'
        '        else\n'
        '            lastHybridWarning.clear();\n'
    )
    menu_precision_adv_mid = (
        '        bool deferredDlss = config->DlssNrDeferredDlss.value_or_default();\n'
        '        const bool isAdaGpuAdv = nrfusion::IsAdaSm89Architecture(nullptr);\n'
        '        int precisionChoice = config->DlssNrPrecision.value_or_default() == 4 ? 1 : 0;\n'
        '        const char* precisionsAda[] = { "NVIDIA (FP8)", "Experimental (Ada W4A8 + FP8 correction)" };\n'
        '        const char* precisionsBlackwell[] = { "NVIDIA (FP8)", "Experimental (FP8+NVFP4 hybrid)" };\n'
        '        const char** precisions = isAdaGpuAdv ? precisionsAda : precisionsBlackwell;\n'
        '        if (ImGui::Combo("Model precision", &precisionChoice, precisions, 2))\n'
        '        {\n'
        '            config->DlssNrPrecision = precisionChoice == 1 ? 4u : 0u;\n'
        '            if (isAdaGpuAdv) nrfusion::SetAdaW4A8Enabled(precisionChoice == 1);\n'
        '        }\n'
        '        if (isAdaGpuAdv)\n'
        '        {\n'
        '            HelpMarker("NVIDIA: modelo FP8 original da NVIDIA.\\nExperimental: aceleracao W4A8 com correcao residual FP8 otimizada para arquitetura Ada Lovelace (RTX 40).");\n'
        '        }\n'
        '        else\n'
        '        {\n'
        '            HelpMarker("NVIDIA: original FP8 model (default), with some sensitive operations kept at higher precision.\\nExperimental: this fork\'s FP8+NVFP4 hybrid for RTX 50 GPUs; output may differ slightly.");\n'
        '        }\n'
        '\n'
        '        if (isAdaGpuAdv)\n'
        '        {\n'
        '            static const char* adaBackends[] = { "Auto (Recomendado)", "INT8 Tensor Core", "CUDA Core (DP4A)" };\n'
        '            int currentBackendIdx = 0;\n'
        '            std::string curBackend = config->DlssNrBackend.value_or_default();\n'
        '            for (char& c : curBackend) c = static_cast<char>(std::tolower(c));\n'
        '            if (curBackend.find("tensor") != std::string::npos) currentBackendIdx = 1;\n'
        '            else if (curBackend.find("dp4a") != std::string::npos || curBackend.find("cuda") != std::string::npos) currentBackendIdx = 2;\n'
        '            else currentBackendIdx = 0;\n'
        '\n'
        '            if (ImGui::Combo("Backend (Ada W4A8)", &currentBackendIdx, adaBackends, IM_ARRAYSIZE(adaBackends)))\n'
        '            {\n'
        '                if (currentBackendIdx == 1) {\n'
        '                    config->DlssNrBackend = std::string("TensorCore");\n'
        '                    nrfusion::SetAdaW4A8Backend("TensorCore");\n'
        '                } else if (currentBackendIdx == 2) {\n'
        '                    config->DlssNrBackend = std::string("DP4A");\n'
        '                    nrfusion::SetAdaW4A8Backend("DP4A");\n'
        '                } else {\n'
        '                    config->DlssNrBackend = std::string("Auto");\n'
        '                    nrfusion::SetAdaW4A8Backend("Auto");\n'
        '                }\n'
        '            }\n'
        '            HelpMarker("Auto: compara DP4A vs Tensor Core em tempo real na GPU e escolhe o mais rapido para este jogo/resolucao.\\nTensor Core: forca execucao via Tensor Cores INT8 (MMA).\\nCUDA Core: forca execucao via CUDA Cores ALU (__dp4a).");\n'
        '\n'
        '            if (currentBackendIdx == 0)\n'
        '            {\n'
        '                if (ImGui::Button("Reavaliar backend mais rapido"))\n'
        '                {\n'
        '                    nrfusion::RecalibrateAdaW4A8Backend();\n'
        '                }\n'
        '                HelpMarker("Reexecuta a comparacao de desempenho GPU (warmup + medicoes A/B) no workload real para atualizar a escolha.");\n'
        '            }\n'
        '\n'
        '            ImGui::TextDisabled("Status W4A8: %s", nrfusion::GetAdaW4A8Status());\n'
        '        }\n'
        '        else\n'
        '        {\n'
        '            if (precisionChoice > 0)\n'
        '            {\n'
        '                ImGui::TextUnformatted(enabled && DlssNrNative::IsActive() ? "Hybrid: active" : "Hybrid: inactive");\n'
        '                ImGui::TextWrapped("Loading may pause the game and look like a freeze. Please wait.");\n'
        '            }\n'
        '            // Keep failure details in the log without displaying changing kernel counters in the menu.\n'
        '            auto hybridStatus = DlssNrNative::Status();\n'
        '            hybridStatus = hybridStatus.substr(0, hybridStatus.find(" |"));\n'
        '            static std::string lastHybridWarning;\n'
        '            if (hybridStatus.rfind("Restart required:", 0) == 0 || hybridStatus.find("fallback") != std::string::npos)\n'
        '            {\n'
        '                if (hybridStatus != lastHybridWarning)\n'
        '                    LOG_WARN("Hybrid: {}", hybridStatus);\n'
        '                lastHybridWarning = hybridStatus;\n'
        '            }\n'
        '            else\n'
        '                lastHybridWarning.clear();\n'
        '        }\n'
    )
    menu_precision_adv_new = (
        '        bool deferredDlss = config->DlssNrDeferredDlss.value_or_default();\n'
        '        const bool isAdaGpuAdv = nrfusion::IsAdaSm89Architecture(nullptr);\n'
        '        const bool isBlackwellAdv = nrfusion::IsBlackwellArchitecture();\n'
        '\n'
        '        if (isAdaGpuAdv)\n'
        '        {\n'
        '            int precisionChoice = nrfusion::IsAdaW4A8Enabled() ? 1 : 0;\n'
        '            const char* precisionsAda[] = { "NVIDIA (FP8)", "W4A8 + FP8 correction" };\n'
        '            if (ImGui::Combo("Model precision", &precisionChoice, precisionsAda, IM_ARRAYSIZE(precisionsAda)))\n'
        '            {\n'
        '                const bool w4a8On = (precisionChoice == 1);\n'
        '                config->DlssNrPrecisionManual = true;\n'
        '                config->DlssNrPrecision = 0u;\n'
        '                nrfusion::SetAdaW4A8Enabled(w4a8On);\n'
        '                config->DlssNrAdaW4A8 = w4a8On;\n'
        '            }\n'
        '            HelpMarker("NVIDIA: original FP8 baseline model.\\nW4A8 + FP8 correction: W4A8 weights with FP8 residual correction optimized for Ada Lovelace (RTX 40).");\n'
        '\n'
        '            const bool w4a8Selected = (precisionChoice == 1);\n'
        '\n'
        '            ImGui::BeginDisabled(!w4a8Selected);\n'
        '            static const char* adaBackends[] = { "Auto (Optimal)", "INT8 Tensor Core", "CUDA Core (DP4A)" };\n'
        '            int currentBackendIdx = 0;\n'
        '            std::string curBackend = config->DlssNrBackend.value_or_default();\n'
        '            for (char& c : curBackend) c = static_cast<char>(std::tolower(c));\n'
        '            if (curBackend.find("tensor") != std::string::npos) currentBackendIdx = 1;\n'
        '            else if (curBackend.find("dp4a") != std::string::npos || curBackend.find("cuda") != std::string::npos) currentBackendIdx = 2;\n'
        '            else currentBackendIdx = 0;\n'
        '\n'
        '            if (ImGui::Combo("Compute backend", &currentBackendIdx, adaBackends, IM_ARRAYSIZE(adaBackends)))\n'
        '            {\n'
        '                if (currentBackendIdx == 1) {\n'
        '                    config->DlssNrBackend = std::string("TensorCore");\n'
        '                    nrfusion::SetAdaW4A8Backend("TensorCore");\n'
        '                } else if (currentBackendIdx == 2) {\n'
        '                    config->DlssNrBackend = std::string("DP4A");\n'
        '                    nrfusion::SetAdaW4A8Backend("DP4A");\n'
        '                } else {\n'
        '                    config->DlssNrBackend = std::string("Auto");\n'
        '                    nrfusion::SetAdaW4A8Backend("Auto");\n'
        '                }\n'
        '            }\n'
        '            HelpMarker("Auto (Optimal): dynamically benchmarks DP4A vs Tensor Core on GPU in real-time and selects the fastest backend for current game and resolution.\\nINT8 Tensor Core: forces execution via INT8 Tensor Cores (MMA/WMMA).\\nCUDA Core (DP4A): forces execution via CUDA Core ALUs (__dp4a).");\n'
        '\n'
        '            ImGui::BeginDisabled(!w4a8Selected || currentBackendIdx != 0);\n'
        '            if (ImGui::Button("Re-evaluate fastest backend"))\n'
        '            {\n'
        '                nrfusion::RecalibrateAdaW4A8Backend();\n'
        '            }\n'
        '            HelpMarker("Re-evaluates GPU execution performance (warmup + A/B real-time measurements) on the current game workload.");\n'
        '            ImGui::EndDisabled();\n'
        '\n'
        '            {\n'
        '                const bool adaLive = nrfusion::IsAdaW4A8Accelerating();\n'
        '                const unsigned long long adaReplaced = (unsigned long long)nrfusion::AdaW4A8ReplacedLaunches();\n'
        '                ImGui::TextColored(adaLive ? ImVec4(0.35f,0.85f,0.35f,1.0f) : ImVec4(0.90f,0.55f,0.25f,1.0f),\n'
        '                                   adaLive ? "Accelerating: YES -- %llu kernel launches replaced"\n'
        '                                           : "Accelerating: NO -- 0 replaced; the original FP8 kernels are running",\n'
        '                                   adaReplaced);\n'
        '                HelpMarker("Counts launches this path actually replaced. The selectors above are your choice, not a measurement; only this line reports what ran.");\n'
        '            }\n'
        '            ImGui::TextDisabled("Status: %s", nrfusion::GetAdaW4A8Status());\n'
        '            HelpMarker("Raw interceptor counters: launches seen, kernels matched by name, rejections.");\n'
        '            ImGui::EndDisabled();\n'
        '        }\n'
        '        else if (isBlackwellAdv)\n'
        '        {\n'
        '            int precisionChoice = config->DlssNrPrecision.value_or_default() == 4 ? 1 : 0;\n'
        '            const char* precisionsBlackwell[] = { "NVIDIA (FP8)", "FP8+NVFP4 hybrid" };\n'
        '            if (ImGui::Combo("Model precision", &precisionChoice, precisionsBlackwell, IM_ARRAYSIZE(precisionsBlackwell)))\n'
        '            {\n'
        '                config->DlssNrPrecision = precisionChoice == 1 ? 4u : 0u;\n'
        '            }\n'
        '            HelpMarker("NVIDIA: original FP8 model (default), with sensitive operations kept at higher precision.\\nFP8+NVFP4 hybrid: NVFP4 hybrid acceleration for RTX 50 (Blackwell) GPUs; output may differ slightly.");\n'
        '\n'
        '            if (precisionChoice > 0)\n'
        '            {\n'
        '                ImGui::TextUnformatted(enabled && DlssNrNative::IsActive() ? "Hybrid: active" : "Hybrid: inactive");\n'
        '                ImGui::TextWrapped("Loading may pause the game and look like a freeze. Please wait.");\n'
        '            }\n'
        '            auto hybridStatus = DlssNrNative::Status();\n'
        '            hybridStatus = hybridStatus.substr(0, hybridStatus.find(" |"));\n'
        '            static std::string lastHybridWarning;\n'
        '            if (hybridStatus.rfind("Restart required:", 0) == 0 || hybridStatus.find("fallback") != std::string::npos)\n'
        '            {\n'
        '                if (hybridStatus != lastHybridWarning)\n'
        '                    LOG_WARN("Hybrid: {}", hybridStatus);\n'
        '                lastHybridWarning = hybridStatus;\n'
        '            }\n'
        '            else\n'
        '                lastHybridWarning.clear();\n'
        '        }\n'
        '        else\n'
        '        {\n'
        '            static const char* precisionsBaseline[] = { "NVIDIA (FP8)" };\n'
        '            int baselineChoice = 0;\n'
        '            ImGui::BeginDisabled(true);\n'
        '            ImGui::Combo("Model precision", &baselineChoice, precisionsBaseline, 1);\n'
        '            ImGui::EndDisabled();\n'
        '            HelpMarker("NVIDIA: standard FP8 model executed via NVIDIA DLSS-NR pipeline.\\nAccelerated formats (NVFP4 / W4A8) require RTX 50 or RTX 40 hardware.");\n'
        '\n'
        '            ImGui::TextDisabled("Architecture: %s (Native DLSS-NR pipeline active)", nrfusion::GetGpuArchitectureName());\n'
        '        }\n'
    )
    if menu_precision_adv_old in menu.read_text(encoding="utf-8"):
        replace_once(menu, menu_precision_adv_old, menu_precision_adv_new)
    elif menu_precision_adv_mid in menu.read_text(encoding="utf-8"):
        replace_once(menu, menu_precision_adv_mid, menu_precision_adv_new)

    gpu_time = opti / "gpu_time/GpuTime_Dx12.cpp"
    if gpu_time.exists():
        trigger_anchor = "        _currentFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;\n\n        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, _currentFrameIndex * 2);\n"
        trigger_new = "        _currentFrameIndex = (_currentFrameIndex + 1) % QUERY_BUFFER_COUNT;\n        // Reusing a ring slot starts a new interval. If this command list returns before End(),\n        // the previous occupant must never remain readable as a fresh timing.\n        _trigger[_currentFrameIndex] = false;\n\n        cmdList->EndQuery(_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, _currentFrameIndex * 2);\n"
        replace_once(gpu_time, trigger_anchor, trigger_new)
        consume_anchor = "    if (!_trigger[previousFrameIndex])\n        return elapsedTimeMs;\n\n    UINT64* timestampData {};\n"
        consume_new = "    if (!_trigger[previousFrameIndex])\n        return elapsedTimeMs;\n\n    // Consume this ring entry exactly once. Invalid numeric data still belongs to this workload;\n    // returning 0 ms advances NRFusion's WorkId mapping without feeding a bogus timing to control.\n    _trigger[previousFrameIndex] = false;\n\n    UINT64* timestampData {};\n"
        replace_once(gpu_time, consume_anchor, consume_new)
        invalid_pair_anchor = "        if (endTime < startTime)\n        {\n            _readbackBuffer->Unmap(0, &writeRange);\n            return elapsedTimeMs;\n        }\n"
        invalid_pair_new = "        if (endTime < startTime)\n        {\n            _readbackBuffer->Unmap(0, &writeRange);\n            return 0.0; // consumed interval, unusable timing\n        }\n"
        replace_once(gpu_time, invalid_pair_anchor, invalid_pair_new)

    ampere_mfg = opti / "framegen/dlssg/AmpereMfgLoader.cpp"
    if ampere_mfg.exists():
        ampere_bilinear_anchor = "    int hwBilinear = cfg->FGDLSSGAmpereMfgHardwareBilinear.value_or_default() ? 1 : 0;\n"
        ampere_bilinear_new = """    // NRFusion Max FPS may opt into the upstream SM86 approximate sampler. The loader itself already
    // validates GPU family and only reaches this path when Ampere/Turing MFG is explicitly enabled.
    // Keep Auto/Balanced/Quality exact; Max FPS is the one mode whose contract permits this trade.
    const auto& fusionMfgGpu = IdentifyGpu::getPrimaryGpu();
    const uint32_t fusionMfgArch = static_cast<uint32_t>(fusionMfgGpu.nvidiaArchInfo.architecture_id);
    const bool fusionMfgAmpere = IsAmpereArch(fusionMfgArch) ||
                                 fusionMfgGpu.name.find("RTX 30") != std::string::npos;
    const bool fusionMaxFpsBilinear = cfg->DlssNrFusionMode.value_or_default() == 1 && fusionMfgAmpere;
    int hwBilinear = (cfg->FGDLSSGAmpereMfgHardwareBilinear.value_or_default() || fusionMaxFpsBilinear) ? 1 : 0;
"""
        replace_once(ampere_mfg, ampere_bilinear_anchor, ampere_bilinear_new)

        ampere_text = ampere_mfg.read_text(encoding="utf-8")
        if "void TrySetup()" in ampere_text:
            setup_anchor = "    auto* cfg = Config::Instance();\n    if (!cfg->FGDLSSGAmpereMfgUnlock.value_or_default())\n        return;\n"
            setup_new = """    auto* cfg = Config::Instance();

    // NRFusion Hardware-Aware MFG Auto-Detection:
    // If not manually specified in OptiScaler.ini, auto-detect GPU architecture and enable the appropriate unlocker.
    const auto& autoGpu = IdentifyGpu::getPrimaryGpu();
    if (autoGpu.vendorId == VendorId::Nvidia)
    {
        const uint32_t autoArch = static_cast<uint32_t>(autoGpu.nvidiaArchInfo.architecture_id);
        const bool isAda = (autoArch == NV_GPU_ARCHITECTURE_AD100) || (autoGpu.name.find("RTX 40") != std::string::npos);
        const bool isAmpere = IsAmpereArch(autoArch) || (autoGpu.name.find("RTX 30") != std::string::npos);
        const bool isTuring = IsTuringArch(autoArch) || (autoGpu.name.find("RTX 20") != std::string::npos || autoGpu.name.find("GTX 16") != std::string::npos);

        if (isAda && !cfg->FGDLSSGAdaMfgUnlock.has_value() && !cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
        {
            cfg->FGDLSSGAdaMfgUnlock.set_volatile_value(true);
            cfg->FGDLSSGAdaBlackwellKernels.set_volatile_value(true);
            cfg->FGDLSSGAmpereMfgUnlock.set_volatile_value(false);
            LOG_INFO("NRFusion: Auto-enabled Ada Multi-Frame Generation (MFG) Unlock for RTX 40 GPU.");
        }
        else if ((isAmpere || isTuring) && !cfg->FGDLSSGAmpereMfgUnlock.has_value() && !cfg->FGDLSSGAdaMfgUnlock.value_or_default())
        {
            auto basePath = Util::DllPath().parent_path();
            auto checkDll = basePath / L"OptiScaler" / L"dlssg_sm86" / L"dlssg_sm86.dll";
            std::error_code ec;
            if (std::filesystem::exists(checkDll, ec) ||
                std::filesystem::exists(basePath / L"dlssg_sm86" / L"dlssg_sm86.dll", ec) ||
                std::filesystem::exists(basePath / L"dlssg_sm86.dll", ec))
            {
                cfg->FGDLSSGAmpereMfgUnlock.set_volatile_value(true);
                cfg->ExternalFrameGeneration.set_volatile_value(true);
                cfg->FGDLSSGAdaMfgUnlock.set_volatile_value(false);
                LOG_INFO("NRFusion: Auto-enabled SM86/SM75 MFG Unlock for Turing/Ampere (RTX 20/30) GPU with dlssg_sm86 runtime.");
            }
        }
    }

    if (!cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
        return;
"""
            replace_once(ampere_mfg, setup_anchor, setup_new)

    mfg_unlock = opti / "framegen/dlssg/MfgUnlock.cpp"
    if mfg_unlock.exists():
        mfg_text = mfg_unlock.read_text(encoding="utf-8")
        if '#include <nrfusion/DlssgTransfusion.hpp>' not in mfg_text:
            insert_after(mfg_unlock, '#include "MfgUnlock.h"\n', '#include <nrfusion/DlssgTransfusion.hpp>\n')

        old_mfg_apply = "void MfgUnlock::TryApply(HMODULE requestedModule)\n{\n"
        new_mfg_apply = """void MfgUnlock::TryApply(HMODULE requestedModule)
{
    auto* cfg = Config::Instance();
    const auto& autoGpu = IdentifyGpu::getPrimaryGpu();
    if (autoGpu.vendorId == VendorId::Nvidia &&
        (autoGpu.nvidiaArchInfo.architecture_id == NV_GPU_ARCHITECTURE_AD100 || autoGpu.name.find("RTX 40") != std::string::npos))
    {
        if (!cfg->FGDLSSGAdaMfgUnlock.has_value() && !cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
        {
            cfg->FGDLSSGAdaMfgUnlock.set_volatile_value(true);
            cfg->FGDLSSGAdaBlackwellKernels.set_volatile_value(true);
        }
    }
    nrfusion::DlssgTransfusion::Instance().TryApply(requestedModule);
"""
        replace_once(mfg_unlock, old_mfg_apply, new_mfg_apply)

        old_mfg_unlocked_max = """unsigned int MfgUnlock::UnlockedMax()
{
    const auto& status = LastStatus();

    return status.AdvertiseMatched && status.ValidateMatched && status.KernelsRewritten > 0
               ? kMaxGeneratedFrames : 0;
}"""
        new_mfg_unlocked_max = """unsigned int MfgUnlock::UnlockedMax()
{
    return nrfusion::DlssgTransfusion::Instance().UnlockedMax();
}"""
        replace_once(mfg_unlock, old_mfg_unlocked_max, new_mfg_unlocked_max)

        old_mfg_pending = """bool MfgUnlock::Pending()
{
    if (!Config::Instance()->FGDLSSGAdaMfgUnlock.value_or_default() ||
        Config::Instance()->FGDLSSGAmpereMfgUnlock.value_or_default() ||
        State::Instance().externalFrameGeneration || g_status.ModuleFound)
        return false;
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return gpu.vendorId == VendorId::Nvidia && gpu.nvidiaArchInfo.architecture_id == NV_GPU_ARCHITECTURE_AD100;
}"""
        new_mfg_pending = """bool MfgUnlock::Pending()
{
    return nrfusion::DlssgTransfusion::Instance().IsPending();
}"""
        replace_once(mfg_unlock, old_mfg_pending, new_mfg_pending)

    streamline_hooks = opti / "hooks/Streamline_Hooks.cpp"
    if streamline_hooks.exists():
        sl_text = streamline_hooks.read_text(encoding="utf-8")
        if '#include <nrfusion/DlssgTransfusion.hpp>' not in sl_text:
            insert_after(streamline_hooks, '#include <framegen/dlssg/MfgUnlock.h>\n', '#include <nrfusion/DlssgTransfusion.hpp>\n')
            old_sl_mfg_apply = """        // Before the read, so the count this captures is the patched one. Five stays under the
        // sanity bound below.
        MfgUnlock::TryApply();

        // nvngx_dlssg.dll can load after this runs, and the ceiling read before it does is Ada's
        // 1. Caching that holds it for the session and clamps the override to it. ModuleFound
        // means the patches have been attempted, so from there the answer is final either way.
        const bool unlockPending = MfgUnlock::Pending();"""
            new_sl_mfg_apply = """        // DLSSG-Transfusion integration: module patch & FollowGame option processing
        auto* cfg = Config::Instance();
        auto& transfusion = nrfusion::DlssgTransfusion::Instance();

        std::string ctrlModeStr = cfg->FGDLSSGControlMode.value_or_default();
        for (char& c : ctrlModeStr) c = static_cast<char>(std::tolower(c));
        if (ctrlModeStr == "follow_game" || ctrlModeStr == "auto")
            transfusion.SetControlMode(nrfusion::MfgControlMode::FollowGame);
        else if (ctrlModeStr == "dynamic")
            transfusion.SetControlMode(nrfusion::MfgControlMode::Dynamic);
        else {
            transfusion.SetControlMode(nrfusion::MfgControlMode::OverrideFixed);
            if (ctrlModeStr == "3x" || ctrlModeStr == "3") transfusion.SetOverrideMultiplier(3);
            else if (ctrlModeStr == "4x" || ctrlModeStr == "4") transfusion.SetOverrideMultiplier(4);
            else if (ctrlModeStr == "5x" || ctrlModeStr == "5") transfusion.SetOverrideMultiplier(5);
            else if (ctrlModeStr == "6x" || ctrlModeStr == "6") transfusion.SetOverrideMultiplier(6);
            else transfusion.SetOverrideMultiplier(2);
        }

        std::string qualityStr = cfg->FGDLSSGQualityMode.value_or_default();
        for (char& c : qualityStr) c = static_cast<char>(std::tolower(c));
        transfusion.SetQualityMode((qualityStr.find("enhanced") != std::string::npos) ?
            nrfusion::MfgQualityMode::Enhanced : nrfusion::MfgQualityMode::Performance);

        std::string uiStr = cfg->FGDLSSGUiRecomposition.value_or_default();
        for (char& c : uiStr) c = static_cast<char>(std::tolower(c));
        transfusion.SetUiMode((uiStr == "off") ? nrfusion::MfgUiMode::Off : nrfusion::MfgUiMode::Auto);

        transfusion.TryApply();

        uint32_t transfMode = static_cast<uint32_t>(newOptions.mode);
        uint32_t transfFrames = newOptions.numFramesToGenerate;
        transfusion.ProcessSetOptions(transfMode, transfFrames);
        newOptions.mode = static_cast<sl::DLSSGMode>(transfMode);
        newOptions.numFramesToGenerate = transfFrames;

        const bool unlockPending = false;"""
            replace_once(streamline_hooks, old_sl_mfg_apply, new_sl_mfg_apply)

            old_sl_override = """        // Won't take effect with Dynamic
        if (Config::Instance()->FGDLSSGOverrideInterpolationCount.has_value())"""
            new_sl_override = """        // Won't take effect with Dynamic or FollowGame
        if (Config::Instance()->FGDLSSGOverrideInterpolationCount.has_value() &&
            cfg->FGDLSSGControlMode.value_or_default() != "follow_game")"""
            if old_sl_override in streamline_hooks.read_text(encoding="utf-8"):
                replace_once(streamline_hooks, old_sl_override, new_sl_override)

            old_sl_get_state = """    // Ahead of every read of numFramesToGenerateMax, which is the value the patch raises.
    MfgUnlock::TryApply();"""
            new_sl_get_state = """    // DLSSG-Transfusion state interrogation
    nrfusion::DlssgTransfusion::Instance().TryApply();"""
            replace_once(streamline_hooks, old_sl_get_state, new_sl_get_state)

            old_sl_dmfg_clamp = """    if (!State::Instance().dlssgGameDMFGSupported)
    {
        Config::Instance()->FGDLSSGOverrideForceDMFG.set_volatile_value(false);
    }"""
            new_sl_dmfg_clamp = """    if (!State::Instance().dlssgGameDMFGSupported)
    {
        Config::Instance()->FGDLSSGOverrideForceDMFG.set_volatile_value(false);
    }

    // DLSSG-Transfusion: publish capability ceiling to Game display menu (2X, 3X, 4X, Dynamic)
    state.numFramesToGenerateMax = nrfusion::DlssgTransfusion::Instance().UnlockedMax();
    state.bIsDynamicMFGSupported = sl::Boolean::eTrue;
    State::Instance().dlssgGameDMFGSupported = true;"""
            replace_once(streamline_hooks, old_sl_dmfg_clamp, new_sl_dmfg_clamp)

    lib_load_hooks = opti / "hooks/LibraryLoad_Hooks.cpp"
    if lib_load_hooks.exists():
        lib_text = lib_load_hooks.read_text(encoding="utf-8")
        if '#include <nrfusion/DlssgTransfusion.hpp>' not in lib_text:
            insert_after(lib_load_hooks, '#include <framegen/dlssg/MfgUnlock.h>\n', '#include <nrfusion/DlssgTransfusion.hpp>\n')
            old_lib_mfg = """    // Optional Ada unlock before NGX caches capabilities. External FG already returned above.
    if (std::filesystem::path(normalizedPath).filename() == L"nvngx_dlssg.dll" && MfgUnlock::Pending())
    {
        auto snippet = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);
        if (snippet)
            MfgUnlock::TryApply(snippet);
        return snippet;
    }"""
            new_lib_mfg = """    // DLSSG-Transfusion injection on nvngx_dlssg.dll load
    if (std::filesystem::path(normalizedPath).filename() == L"nvngx_dlssg.dll")
    {
        auto snippet = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);
        if (snippet)
            nrfusion::DlssgTransfusion::Instance().TryApply(snippet);
        return snippet;
    }"""
            replace_once(lib_load_hooks, old_lib_mfg, new_lib_mfg)

    menu_common = opti / "menu/menu_common.cpp"
    if menu_common.exists():
        menu_text = menu_common.read_text(encoding="utf-8")
        if '#include <nrfusion/DlssgTransfusion.hpp>' not in menu_text:
            insert_after(menu_common, '#include <framegen/dlssg/MfgUnlock.h>\n', '#include <nrfusion/DlssgTransfusion.hpp>\n')
            menu_text = menu_common.read_text(encoding="utf-8")
            fg_start_anchor = "    /// FG INPUTS\n    bool adaUnlock = config->FGDLSSGAdaMfgUnlock.value_or_default();"
            fg_end_anchor = "    if (state.externalFrameGeneration)\n    {\n        ImGui::TextWrapped(\"External FG is active. Set the multiplier in the game or unlocker, not OptiScaler.\");\n        return;\n    }"
            fg_start = menu_text.find(fg_start_anchor)
            fg_end = menu_text.find(fg_end_anchor, fg_start) if fg_start != -1 else -1
            if fg_start != -1 and fg_end != -1:
                new_fg_block = '''    /// FG INPUTS: DLSSG-Transfusion Integration
    if (ImGui::CollapsingHeader("NVIDIA Multi Frame Generation", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::Indent();
        auto& transfusion = nrfusion::DlssgTransfusion::Instance();
        const auto& tStatus = transfusion.Status();

        // 1. Control Mode combo
        static const char* controlModes[] = {
            "Game Controlled",
            "2X",
            "3X",
            "4X",
            "Dynamic",
            "5X (Experimental)",
            "6X (Experimental)"
        };
        int currentControlIdx = 0;
        const auto currentCtrl = transfusion.GetControlMode();
        if (currentCtrl == nrfusion::MfgControlMode::FollowGame)
            currentControlIdx = 0;
        else if (currentCtrl == nrfusion::MfgControlMode::Dynamic)
            currentControlIdx = 4;
        else {
            const uint32_t mult = transfusion.GetOverrideMultiplier();
            if (mult == 2) currentControlIdx = 1;
            else if (mult == 3) currentControlIdx = 2;
            else if (mult == 4) currentControlIdx = 3;
            else if (mult == 5) currentControlIdx = 5;
            else if (mult == 6) currentControlIdx = 6;
            else currentControlIdx = 1;
        }

        if (ImGui::Combo("Control##transfusion", &currentControlIdx, controlModes, IM_ARRAYSIZE(controlModes)))
        {
            if (currentControlIdx == 0) {
                transfusion.SetControlMode(nrfusion::MfgControlMode::FollowGame);
                config->FGDLSSGControlMode = std::string("follow_game");
            } else if (currentControlIdx == 4) {
                transfusion.SetControlMode(nrfusion::MfgControlMode::Dynamic);
                config->FGDLSSGControlMode = std::string("dynamic");
            } else {
                transfusion.SetControlMode(nrfusion::MfgControlMode::OverrideFixed);
                uint32_t mult = 2;
                if (currentControlIdx == 1) mult = 2;
                else if (currentControlIdx == 2) mult = 3;
                else if (currentControlIdx == 3) mult = 4;
                else if (currentControlIdx == 5) mult = 5;
                else if (currentControlIdx == 6) mult = 6;
                transfusion.SetOverrideMultiplier(mult);
                config->FGDLSSGControlMode = std::to_string(mult) + "x";
            }
        }
        ShowHelpMarker("Game Controlled: respects 2X/3X/4X selected in the in-game display settings menu (Default).\\n"
                       "2X / 3X / 4X: overrides frame multiplier from OptiScaler.\\n"
                       "Dynamic: dynamically modulates frame generation pacing.");

        // 2. MFG Quality Mode
        static const char* qualityModes[] = { "Performance", "Enhanced Quality" };
        int currentQualityIdx = (transfusion.GetQualityMode() == nrfusion::MfgQualityMode::Enhanced) ? 1 : 0;
        if (ImGui::Combo("Quality##transfusion", &currentQualityIdx, qualityModes, IM_ARRAYSIZE(qualityModes)))
        {
            const auto qMode = (currentQualityIdx == 1) ? nrfusion::MfgQualityMode::Enhanced : nrfusion::MfgQualityMode::Performance;
            transfusion.SetQualityMode(qMode);
            config->FGDLSSGQualityMode = (currentQualityIdx == 1) ? std::string("enhanced") : std::string("performance");
        }
        ShowHelpMarker("Performance: reduces additional MFG overhead, zero GPU latency cost (Default).\\n"
                       "Enhanced Quality: applies surgical subpixel warp filtering for tearing and fine wires.");

        // 3. UI Recomposition
        static const char* uiModes[] = { "Auto", "Off" };
        int currentUiIdx = (transfusion.GetUiMode() == nrfusion::MfgUiMode::Off) ? 1 : 0;
        if (ImGui::Combo("UI Recomposition##transfusion", &currentUiIdx, uiModes, IM_ARRAYSIZE(uiModes)))
        {
            const auto uMode = (currentUiIdx == 1) ? nrfusion::MfgUiMode::Off : nrfusion::MfgUiMode::Auto;
            transfusion.SetUiMode(uMode);
            config->FGDLSSGUiRecomposition = (currentUiIdx == 1) ? std::string("off") : std::string("auto");
        }
        ShowHelpMarker("Auto: uses HUDless textures and separate UI recomposition when provided by the game engine (Default).\\n"
                       "Off: forces standard composition.");

        // Status
        ImGui::Spacing();
        ImGui::TextDisabled("Status: %s", tStatus.moduleFound ? "Active" : "Awaiting Streamline");
        ImGui::TextDisabled("Requested by Game: %s | Effective: %uX",
            tStatus.requestedByGame == 0 ? "Off" : (std::to_string(tStatus.requestedByGame + 1) + "X").c_str(),
            tStatus.effectiveMultiplier);
        ImGui::TextDisabled("Backend: DLSSG-Transfusion | Blackwell Kernels: %s",
            tStatus.blackwellTransfusionActive ? "Active" : "Standard");

        ImGui::Unindent();
    }\n\n'''
                menu_common.write_text(menu_text[:fg_start] + new_fg_block + menu_text[fg_end:], encoding="utf-8")

    ini_path = checkout / "OptiScaler.ini"
    if ini_path.exists():
        ini_text = ini_path.read_text(encoding="utf-8")
        if "AdaMfgUnlock=false" in ini_text:
            ini_text = ini_text.replace("AdaMfgUnlock=false", "; Auto: automatically enables Ada MFG Unlock on RTX 40 series GPUs\nAdaMfgUnlock=auto")
        if "AmpereMfgUnlock=false" in ini_text:
            ini_text = ini_text.replace("AmpereMfgUnlock=false", "; Auto: automatically enables SM86/SM75 MFG Unlock on RTX 20 and RTX 30 series GPUs\nAmpereMfgUnlock=auto")
        if "[DLSSG]" in ini_text and "ControlMode=" not in ini_text:
            ini_text = ini_text.replace(
                "[DLSSG]",
                "[DLSSG]\n; DLSSG-Transfusion backend for Ada Lovelace (RTX 40 Series)\n; Auto-enabled for RTX 40 GPUs\nTransfusion=auto\n\n; ControlMode: follow_game (default, preserves 2X/3X/4X chosen in game menu), 2x, 3x, 4x, dynamic\nControlMode=follow_game\n\n; QualityMode: performance (lowest latency, zero GPU overhead) or enhanced (subpixel warp filter)\nQualityMode=performance\n\n; UiRecomposition: auto (activates when HUDless buffers are provided by game) or off\nUiRecomposition=auto\n"
            )
        if "[DlssNr]" in ini_text and "Backend=" not in ini_text:
            ini_text = ini_text.replace(
                "[DlssNr]",
                "[DlssNr]\n; Compute backend for Ada Lovelace (RTX 40 / SM89) W4A8 DLSS-NR acceleration:\n; Auto       - Benchmarks DP4A vs TensorCore on first launch, caches decision in nrfusion_ada_cache.ini (default)\n; DP4A       - Forces CUDA Core ALU (__dp4a) compute path\n; TensorCore - Forces INT8 Tensor Core MMA (WMMA) compute path\n; Default is Auto\nBackend=Auto\n\n; Re-evaluate fastest backend on launch instead of reading decision from nrfusion_ada_cache.ini\n; true or false - Default is false\nRecalibrateBackend=false\n"
            )
        if "CheckForUpdate=auto" in ini_text:
            ini_text = ini_text.replace(
                "CheckForUpdate=auto",
                "; Enables checking for latest version from Github\n; Disabled in NRFusion build to prevent false-positive upstream update popups\nCheckForUpdate=false"
            )
        ini_path.write_text(ini_text, encoding="utf-8")


    frame_limit = opti / "misc/FrameLimit.cpp"
    if frame_limit.exists():
        insert_after(frame_limit, '#include "FrameLimit.h"\n', '#include <nrfusion/FrameLimitPolicy.hpp>\n#include <nrfusion/OptiScalerAdapter.hpp>\n#include <State.h>\n')
        old_limit = "    if (auto fpsCap = Config::Instance()->FramerateLimit.value_or_default(); fpsCap != 0.0f)\n    {\n        uint64_t min_interval_us = std::clamp((uint64_t) (1\'000\'000 / fpsCap), 0ULL, 100\'000\'000ULL);\n\n        if (fgActive)\n            min_interval_us *= 2;\n"
        new_limit = "    const double manualCap = Config::Instance()->FramerateLimit.value_or_default();\n    const double fusionCap = Config::Instance()->DlssNrEnabled.value_or_default()\n        ? nrfusion::OptiScalerAdapter::Instance().LastDecision().recommendedSourceCapFps : 0.0;\n    static nrfusion::GenerationMultiplierTracker fusionFgMultiplierTracker;\n    const double fusionFgMultiplier = fusionFgMultiplierTracker.Update(\n        State::Instance().lastFGFrameTime, State::Instance().presentFrameTime, fgActive);\n    const auto fusionLimit = nrfusion::FrameLimitPolicy::Resolve(manualCap, fusionCap, fusionFgMultiplier);\n    if (fusionLimit.sourceCapFps > 0.0)\n    {\n        uint64_t min_interval_us = std::clamp((uint64_t) (1\'000\'000 / fusionLimit.sourceCapFps), 0ULL, 100\'000\'000ULL);\n"
        replace_once(frame_limit, old_limit, new_limit)

    vcx = opti / "OptiScaler.vcxproj"
    # Use known existing item-group entries as stable anchors.
    for h in headers:
        add_project_item(vcx, "ClInclude", f"nrfusion\\{h}", '    <ClInclude Include="gpu_time\\GpuTime_Dx12.h" />\n')
    for cpp in sources:
        add_project_item(vcx, "ClCompile", f"nrfusion\\{cpp}", '    <ClCompile Include="Config.cpp" />\n', not_using_pch=True)

    # The Win32 solution configuration builds today (Release|x86 maps to Release|Win32 with
    # Build.0 set), but a clean build of it fails before any NRFusion patch runs: pch.cpp itself
    # only has a Create directive for the three x64 configurations, so under Win32 no file ever
    # creates OptiScaler.pch while every other file still defaults to PrecompiledHeader=Use,
    # producing a wall of "Cannot open precompiled header file" errors. The five imgui.cpp/
    # imgui_draw.cpp/imgui_tables.cpp/imgui_widgets.cpp/imgui_freetype.cpp translation units (plus
    # imgui_impl_dx11/dx12/uwp/vulkan/win32.cpp and sl.param/parameters.cpp) already opt out of the
    # PCH for x64 for the same reason upstream added those overrides in the first place; Win32 was
    # simply never brought up to the same state. This mirrors the x64 pattern exactly -- it is a
    # confirmed, isolated build-configuration bug, verified by a clean Win32 build in a throwaway
    # worktree, and does not by itself make a Win32 build complete: several vendored prebuilt
    # libraries this project links (at least library/fsr2's ffx_fsr2_api_*_x64*.lib) exist only as
    # x64 binaries, so OptiScaler.vcxproj's Win32 IncludePath/LibraryPath/AdditionalDependencies
    # still need real work -- and possibly upstream vendor SDK releases that do not exist yet --
    # before a Win32 OptiScaler.dll can link. Fixing the PCH bug now means that work will not also
    # have to rediscover this one.
    for pch_target in (
        'include\\imgui\\imgui_impl_dx11.cpp', 'include\\imgui\\imgui_impl_dx12.cpp',
        'include\\imgui\\imgui_impl_uwp.cpp', 'include\\imgui\\imgui_impl_vulkan.cpp',
        'include\\imgui\\imgui_impl_win32.cpp', 'include\\imgui\\imgui.cpp',
        'include\\imgui\\imgui_draw.cpp', 'include\\imgui\\imgui_tables.cpp',
        'include\\imgui\\imgui_widgets.cpp', 'include\\imgui\\misc\\freetype\\imgui_freetype.cpp',
        'include\\sl.param\\parameters.cpp',
    ):
        x64_block = (
            f'    <ClCompile Include="{pch_target}">\n'
            f'      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Debug|x64\'">NotUsing</PrecompiledHeader>\n'
            f'      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'ReleaseDebug|x64\'">NotUsing</PrecompiledHeader>\n'
            f'      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Release|x64\'">NotUsing</PrecompiledHeader>\n'
        )
        win32_lines = (
            '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Debug|Win32\'">NotUsing</PrecompiledHeader>\n'
            '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'ReleaseDebug|Win32\'">NotUsing</PrecompiledHeader>\n'
            '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Release|Win32\'">NotUsing</PrecompiledHeader>\n'
        )
        insert_after_unique(vcx, x64_block, win32_lines)

    pch_create_block = (
        '    <ClCompile Include="pch.cpp">\n'
        '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Debug|x64\'">Create</PrecompiledHeader>\n'
        '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'ReleaseDebug|x64\'">Create</PrecompiledHeader>\n'
        '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Release|x64\'">Create</PrecompiledHeader>\n'
    )
    pch_create_win32 = (
        '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Debug|Win32\'">Create</PrecompiledHeader>\n'
        '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'ReleaseDebug|Win32\'">Create</PrecompiledHeader>\n'
        '      <PrecompiledHeader Condition="\'$(Configuration)|$(Platform)\'==\'Release|Win32\'">Create</PrecompiledHeader>\n'
    )
    insert_after_unique(vcx, pch_create_block, pch_create_win32)

    # Diagnostico opcional: acompanha a cadeia de lancamento real da NVAPI e mede o passe neural na
    # linha do tempo da GPU. O banco de testes le esses valores so depois que a sua propria cerca de
    # fila completa. Entra por ultimo, para nao consumir ancoras de que os ganchos acima dependem.
    native_text = native_cpp.read_text(encoding="utf-8")
    note_anchor = "unsigned kind=99;"
    start_anchor = "    if (g_gpuTime != nullptr)\n        g_gpuTime->Start(cmdList);\n"
    if note_anchor in native_text and start_anchor in dx12.read_text(encoding="utf-8"):
        for name in ("NrDiagnosticsApi.hpp", "NrD3D12Diagnostics.hpp"):
            shutil.copy2(ROOT / "include" / "nrfusion" / name, dest / name)
            add_project_item(vcx, "ClInclude", f"nrfusion\\{name}",
                             '    <ClInclude Include="gpu_time\\GpuTime_Dx12.h" />\n')
        host_source = (ROOT / "src" / "host" / "NrD3D12Diagnostics.cpp").read_text(encoding="utf-8")
        (dest / "NrD3D12Diagnostics.cpp").write_text(
            host_source.replace('#include "nrfusion/', '#include "'), encoding="utf-8")
        add_project_item(vcx, "ClCompile", "nrfusion\\NrD3D12Diagnostics.cpp",
                         '    <ClCompile Include="Config.cpp" />\n', not_using_pch=True)

        insert_after(native_cpp, '#include "DlssNrNative.h"\n',
                     '#include <nrfusion/NrD3D12Diagnostics.hpp>\n')
        replace_once(native_cpp, note_anchor,
                     "nrfusion::NoteNrFunction(*out,n);nrfusion::NoteAdaW4A8Kernel(*out,n);" + note_anchor)
        replace_once(native_cpp, "s.targets.erase(f);return s.destroyFunction(d,f);",
                     "nrfusion::ForgetNrFunction(f);s.targets.erase(f);return s.destroyFunction(d,f);")
        # Toda saida para a NVAPI original passa a ser cronometrada, inclusive as de retorno
        # antecipado. Por isso a substituicao e global e roda depois dos ganchos de tracado.
        native_text = native_cpp.read_text(encoding="utf-8")
        if "nrfusion::ProfileNrChain" not in native_text:
            if "s.launch(c,k,count)" not in native_text:
                raise RuntimeError("ancora de lancamento original da NVAPI ausente")
            native_cpp.write_text(native_text.replace(
                "s.launch(c,k,count)", "nrfusion::ProfileNrChain(c,k,count,s.launch)"), encoding="utf-8")

        insert_after(dx12, '#include <nrfusion/OptiScalerAdapter.hpp>\n',
                     '#include <nrfusion/NrD3D12Diagnostics.hpp>\n')
        if "#include <nrfusion/AdaW4A8Interceptor.hpp>" not in dx12.read_text(encoding="utf-8"):
            insert_after(dx12, '#include <nrfusion/NrD3D12Diagnostics.hpp>\n',
                         '#include <nrfusion/AdaW4A8Interceptor.hpp>\n'
                         '#include <nrfusion/NrDiagnosticsApi.hpp>\n'
                         '#include <filesystem>\n'
                         'extern "C" int NRFusion_BeginNrDiagnosticFrame(ID3D12Device*, std::uint64_t, int, const char*);\n'
                         'extern "C" int NRFusion_ReadNrDiagnosticFrame(ID3D12CommandQueue*, ID3D12Fence*, std::uint64_t, nrfusion::NrDiagnosticFrame*);\n')
        replace_once(dx12, start_anchor,
                     "    nrfusion::NrPassDiagnosticScope nrDiagnostic(cmdList);\n" + start_anchor)
        replace_once(dx12,
                     "    if (result == NVSDK_NGX_Result_Success)\n        ++g_nr.successfulDispatches;\n",
                     "    if (result == NVSDK_NGX_Result_Success)\n    {\n"
                     "        ++g_nr.successfulDispatches;\n        nrDiagnostic.Succeeded();\n    }\n")

    # Fase 1 of the compat architecture plan: a real, wired Synthetic route for D3D12 games with no
    # native DLSS/FSR/XeSS contract. Everything the neural pass itself needs (adaptive scale,
    # precision, telemetry) already lives in EvaluateInternal/EvaluateAfterUpscale; this only
    # manufactures the NVSDK_NGX_Parameter a native game would have built, from the swapchain's own
    # back buffer, and calls that existing function unmodified. RuntimeCapabilities::syntheticD3D12
    # must stay false in GameProbe until this has been proven against a real non-DLSS D3D12 game --
    # see the header comment on GameProbe::IntegratedCapabilities().
    if "bool EvaluateSynthetic(ID3D12CommandQueue" not in dx12.read_text(encoding="utf-8"):
        insert_after(dx12, "#include <mutex>\n", "#include <cstdint>\n")
        insert_after(dx12, "#include <algorithm>\n", "#include <vector>\n")
        insert_after(dx12, "#include <nrfusion/NrD3D12Diagnostics.hpp>\n",
                     "#include <nrfusion/SyntheticDlaaContract.hpp>\n"
                     "#include <nrfusion/SyntheticDx12Provider.hpp>\n"
                     "#include <with_dx12/dx11_with_dx12.h>\n")

        synthetic_anchor = (
            "void EvaluateBeforeUpscale(ID3D12GraphicsCommandList* cmdList, NVSDK_NGX_Parameter* params,\n"
            "                           ID3D12CommandQueue* timingQueue, unsigned long long submissionEpoch,\n"
            "                           bool rayReconstruction)\n"
            "{\n"
            "    EvaluateInternal(cmdList, params, true, timingQueue, rayReconstruction, submissionEpoch);\n"
            "}\n"
        )
        synthetic_impl = """
namespace
{
// Fase 1 of the NRFusion compat architecture plan: a game with no native DLSS/FSR/XeSS contract at
// all. There is no game-supplied Output the NGX contract could reuse in place -- a swapchain back
// buffer is essentially never created with D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, which the NGX
// Output parameter requires -- so this keeps one small UAV-capable scratch of its own, copies the
// frame in, evaluates in place on the scratch, and copies the answer back. Recreated only when the
// back buffer's size or format changes.
struct SyntheticScratch
{
    ID3D12Device* device = nullptr;
    ID3D12Resource* colour = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    D3D12_RESOURCE_STATES colourState = D3D12_RESOURCE_STATE_COMMON;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    void ReleaseAll()
    {
        if (colour != nullptr) { colour->Release(); colour = nullptr; }
        if (depth != nullptr) { depth->Release(); depth = nullptr; }
        if (motion != nullptr) { motion->Release(); motion = nullptr; }
        width = 0;
        height = 0;
    }
};

struct SyntheticCommandContext
{
    ID3D12Device* device = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
};

ID3D12Resource* CreateSyntheticTexture(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format,
                                       D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES initialState)
{
    D3D12_HEAP_PROPERTIES heap = {};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    ID3D12Resource* resource = nullptr;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, initialState, nullptr,
                                               IID_PPV_ARGS(&resource))))
        return nullptr;
    return resource;
}

// Zeroes a freshly created texture via an upload-heap copy: no descriptor heap needed, and the
// upload resource only has to outlive the one command list that reads it. depth=0/motion=0 is the
// same "far plane, nothing moving" placeholder every DLSS integration without real guides uses.
bool ZeroFillTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, ID3D12Resource* target,
                     const D3D12_RESOURCE_DESC& desc, D3D12_RESOURCE_STATES restoreState)
{
    UINT64 uploadSize = 0;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
    device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);
    if (uploadSize == 0)
        return false;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = uploadSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource* upload = nullptr;
    if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload))))
        return false;

    void* mapped = nullptr;
    if (FAILED(upload->Map(0, nullptr, &mapped)))
    {
        upload->Release();
        return false;
    }
    std::memset(mapped, 0, static_cast<size_t>(uploadSize));
    upload->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = target;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;
    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = restoreState;
    cmdList->ResourceBarrier(1, &barrier);

    // The command list has not executed yet, so the upload cannot be released until the GPU is done
    // with it. Kept alive on a small bounded list rather than fence-tracked: this only runs on
    // (re)creation, at most once per resolution/format change, never per frame.
    static std::vector<ID3D12Resource*> pendingUploads;
    pendingUploads.push_back(upload);
    while (pendingUploads.size() > 8)
    {
        pendingUploads.front()->Release();
        pendingUploads.erase(pendingUploads.begin());
    }
    return true;
}

void Barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before,
            D3D12_RESOURCE_STATES after)
{
    if (before == after)
        return;
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

bool EnsureSyntheticScratch(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width,
                            UINT height, DXGI_FORMAT format, SyntheticScratch& scratch)
{
    if (scratch.colour != nullptr && scratch.device == device && scratch.width == width &&
        scratch.height == height && scratch.format == format)
        return true;

    scratch.ReleaseAll();
    scratch.device = device;

    scratch.colour = CreateSyntheticTexture(device, width, height, format,
                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                            D3D12_RESOURCE_STATE_COPY_DEST);
    if (scratch.colour == nullptr)
    {
        // Most likely an sRGB or block-compressed swapchain format, neither of which allows a UAV.
        // Fail closed for this session's resolution/format instead of retrying every frame.
        return false;
    }
    scratch.colourState = D3D12_RESOURCE_STATE_COPY_DEST;

    scratch.depth = CreateSyntheticTexture(device, width, height, DXGI_FORMAT_R32_FLOAT,
                                           D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    scratch.motion = CreateSyntheticTexture(device, width, height, DXGI_FORMAT_R16G16_FLOAT,
                                            D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
    if (scratch.depth == nullptr || scratch.motion == nullptr)
    {
        scratch.ReleaseAll();
        return false;
    }

    D3D12_RESOURCE_DESC depthDesc = scratch.depth->GetDesc();
    D3D12_RESOURCE_DESC motionDesc = scratch.motion->GetDesc();
    if (!ZeroFillTexture(device, cmdList, scratch.depth, depthDesc,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !ZeroFillTexture(device, cmdList, scratch.motion, motionDesc,
                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
    {
        scratch.ReleaseAll();
        return false;
    }

    scratch.width = width;
    scratch.height = height;
    scratch.format = format;
    return true;
}

// Fase 2 (working-scale downsample) needs a second, smaller pair of dummy guides at the low-res
// working resolution -- separate from SyntheticScratch's own (native-res, Fase 1's 1:1 case), since
// the two are never both active in the same call.
struct SyntheticLowGuides
{
    ID3D12Device* device = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    UINT width = 0;
    UINT height = 0;

    void ReleaseAll()
    {
        if (depth != nullptr) { depth->Release(); depth = nullptr; }
        if (motion != nullptr) { motion->Release(); motion = nullptr; }
        width = 0;
        height = 0;
    }
};

bool EnsureSyntheticLowGuides(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height,
                              SyntheticLowGuides& guides)
{
    if (guides.depth != nullptr && guides.device == device && guides.width == width && guides.height == height)
        return true;

    guides.ReleaseAll();
    guides.device = device;

    guides.depth = CreateSyntheticTexture(device, width, height, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                                          D3D12_RESOURCE_STATE_COPY_DEST);
    guides.motion = CreateSyntheticTexture(device, width, height, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE,
                                           D3D12_RESOURCE_STATE_COPY_DEST);
    if (guides.depth == nullptr || guides.motion == nullptr)
    {
        guides.ReleaseAll();
        return false;
    }

    D3D12_RESOURCE_DESC depthDesc = guides.depth->GetDesc();
    D3D12_RESOURCE_DESC motionDesc = guides.motion->GetDesc();
    if (!ZeroFillTexture(device, cmdList, guides.depth, depthDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) ||
        !ZeroFillTexture(device, cmdList, guides.motion, motionDesc, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE))
    {
        guides.ReleaseAll();
        return false;
    }

    guides.width = width;
    guides.height = height;
    return true;
}

bool EnsureSyntheticCommandContext(ID3D12Device* device, SyntheticCommandContext& ctx)
{
    if (ctx.list != nullptr && ctx.device == device)
        return true;

    if (ctx.list != nullptr) ctx.list->Release();
    if (ctx.allocator != nullptr) ctx.allocator->Release();
    if (ctx.fence != nullptr) ctx.fence->Release();
    if (ctx.fenceEvent != nullptr) CloseHandle(ctx.fenceEvent);
    ctx = SyntheticCommandContext{};

    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ctx.allocator))))
        return false;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.allocator, nullptr,
                                         IID_PPV_ARGS(&ctx.list))))
        return false;
    ctx.list->Close();
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ctx.fence))))
        return false;
    ctx.fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (ctx.fenceEvent == nullptr)
        return false;
    ctx.device = device;
    return true;
}
} // namespace

bool EvaluateSynthetic(ID3D12CommandQueue* queue, ID3D12Resource* backBuffer)
{
    const Config& cfg = *Config::Instance();
    if (!cfg.DlssNrEnabled.value_or_default() || queue == nullptr || backBuffer == nullptr)
        return false;

    ID3D12Device* device = nullptr;
    if (FAILED(backBuffer->GetDevice(IID_PPV_ARGS(&device))) || device == nullptr)
        return false;
    device->Release();

    const D3D12_RESOURCE_DESC backDesc = backBuffer->GetDesc();
    const auto width = static_cast<UINT>(backDesc.Width);
    const auto height = backDesc.Height;
    if (width == 0 || height == 0)
        return false;

    nrfusion::SyntheticDlaaConfig contractConfig{};
    contractConfig.nativeResolution = {width, height};
    contractConfig.workingScale = 1.0f; // Fase 1: 1:1 DLAA only; downsample is Fase 2.
    contractConfig.isHdr = (backDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    contractConfig.depthInverted = true;
    const auto contract = nrfusion::SyntheticDlaaContract::CreateContract(contractConfig);
    if (contract.inWidth == 0 || contract.inHeight == 0)
        return false;

    static SyntheticCommandContext ctx;
    if (!EnsureSyntheticCommandContext(device, ctx))
        return false;

    if (FAILED(ctx.allocator->Reset()) || FAILED(ctx.list->Reset(ctx.allocator, nullptr)))
        return false;

    static SyntheticScratch scratch;
    if (!EnsureSyntheticScratch(device, ctx.list, contract.inWidth, contract.inHeight, backDesc.Format,
                                scratch))
    {
        ctx.list->Close();
        return false;
    }

    // Bring the scratch back to a copy-writable state (it may still be COPY_SOURCE from the previous
    // frame's read-back) and copy this frame's finished picture in.
    Barrier(ctx.list, scratch.colour, scratch.colourState, D3D12_RESOURCE_STATE_COPY_DEST);
    Barrier(ctx.list, backBuffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ctx.list->CopyResource(scratch.colour, backBuffer);
    Barrier(ctx.list, backBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    scratch.colourState = D3D12_RESOURCE_STATE_COPY_DEST;

    // Fase 2/3: SyntheticDx12Provider's downsample/residual math is hardcoded to
    // DXGI_FORMAT_R16G16B16A16_FLOAT (see EnsureSlotResources), which scratch.colour already is
    // whenever the swapchain itself is -- no separate format-converting pass needed. Anything else
    // (the common SDR case) keeps the Fase 1 1:1 in-place path unchanged; a working scale below 1.0
    // is a user/Auto choice (DlssNrWorkingScale), never a silent default-behaviour change.
    const float workingScale = std::clamp(cfg.DlssNrWorkingScale.value_or_default(), 0.5f, 1.0f);
    const bool useLowRes = (backDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT) && workingScale < 0.999f;

    NVSDK_NGX_Parameter* params = nullptr;
    bool lowResSucceeded = false;

    if (useLowRes)
    {
        static nrfusion::SyntheticDx12Provider provider;
        if (!provider.IsReady())
        {
            nrfusion::ProviderContext providerCtx{};
            providerCtx.api = nrfusion::GraphicsApi::D3D12;
            providerCtx.device = device;
            providerCtx.commandQueue = queue;
            providerCtx.preferSameDevice = true;
            provider.Initialize(providerCtx);
        }

        nrfusion::SyntheticFrameInputs inputs{};
        static std::uint64_t syntheticWorkCounter = 0;
        inputs.ticket.id = ++syntheticWorkCounter;
        inputs.frameId = inputs.ticket.id;
        inputs.renderResolution = { width, height };
        inputs.targetResolution = { width, height };
        inputs.workingScale = workingScale;
        inputs.color.opaqueId = reinterpret_cast<std::uint64_t>(scratch.colour);
        inputs.color.resolution = { width, height };
        inputs.color.format = nrfusion::ResourceFormat::Rgba16Float;

        Barrier(ctx.list, scratch.colour, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        const auto handle = provider.IsReady() ? provider.Submit(inputs, ctx.list) : nrfusion::SyntheticWorkHandle{};

        static SyntheticLowGuides lowGuides;
        if (handle.valid &&
            EnsureSyntheticLowGuides(device, ctx.list, handle.workResolution.width, handle.workResolution.height,
                                     lowGuides) &&
            NVSDK_NGX_D3D12_AllocateParameters(&params) == NVSDK_NGX_Result_Success && params != nullptr)
        {
            const uint32_t slot = provider.SlotForWork(handle.workId);
            ID3D12Resource* lowColor = provider.GetSlotLowColor(slot);
            ID3D12Resource* lowNeuralOut = provider.GetSlotLowNeuralOut(slot);

            params->Set(NVSDK_NGX_Parameter_Color, lowColor);
            params->Set(NVSDK_NGX_Parameter_Output, lowNeuralOut);
            params->Set(NVSDK_NGX_Parameter_Depth, lowGuides.depth);
            params->Set(NVSDK_NGX_Parameter_MotionVectors, lowGuides.motion);
            params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, handle.workResolution.width);
            params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, handle.workResolution.height);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0u);
            params->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
            params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
            params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, contract.createFlags);
            params->Set(NVSDK_NGX_Parameter_OutWidth, handle.workResolution.width);
            params->Set(NVSDK_NGX_Parameter_OutHeight, handle.workResolution.height);
            params->Set(NVSDK_NGX_Parameter_Reset, 0u);

            static unsigned long long syntheticEpochLow = 0;
            ++syntheticEpochLow;
            EvaluateAfterUpscale(ctx.list, params, queue, false, syntheticEpochLow);
            NVSDK_NGX_D3D12_DestroyParameters(params);
            params = nullptr;

            provider.ExtractResidual(handle, ctx.list);

            nrfusion::ResourceRef nativeRef{};
            nativeRef.opaqueId = reinterpret_cast<std::uint64_t>(scratch.colour);
            nativeRef.resolution = { width, height };
            nativeRef.format = nrfusion::ResourceFormat::Rgba16Float;
            provider.ComposeNative(handle, nativeRef, nativeRef, ctx.list, 1.0f);

            scratch.colourState = D3D12_RESOURCE_STATE_COMMON;
            lowResSucceeded = true;
        }
        else
        {
            // Submit/guides/allocate failed after the scratch copy already landed -- fall back to
            // the Fase 1 in-place path this frame instead of losing it; scratch is COMMON either way.
            ReportSkipOnce("the low-res synthetic route could not prepare this frame; using 1:1");
            scratch.colourState = D3D12_RESOURCE_STATE_COMMON;
        }
    }

    if (!useLowRes || !lowResSucceeded)
    {
        // Fase 1 path: 1:1, in place. Also the low-res path's fallback when any low-res step failed.
        Barrier(ctx.list, scratch.colour, scratch.colourState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        scratch.colourState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        if (NVSDK_NGX_D3D12_AllocateParameters(&params) != NVSDK_NGX_Result_Success || params == nullptr)
        {
            ReportSkipOnce("the NGX core refused to allocate synthetic parameters");
        }
        else
        {
            params->Set(NVSDK_NGX_Parameter_Color, scratch.colour);
            params->Set(NVSDK_NGX_Parameter_Output, scratch.colour);
            params->Set(NVSDK_NGX_Parameter_Depth, scratch.depth);
            params->Set(NVSDK_NGX_Parameter_MotionVectors, scratch.motion);
            params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, contract.inWidth);
            params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, contract.inHeight);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0u);
            params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0u);
            params->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
            params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
            params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, contract.createFlags);
            params->Set(NVSDK_NGX_Parameter_OutWidth, contract.inWidth);
            params->Set(NVSDK_NGX_Parameter_OutHeight, contract.inHeight);
            params->Set(NVSDK_NGX_Parameter_Reset, 0u);

            static unsigned long long syntheticEpoch = 0;
            ++syntheticEpoch;
            EvaluateAfterUpscale(ctx.list, params, queue, false, syntheticEpoch);
            NVSDK_NGX_D3D12_DestroyParameters(params);
        }
    }

    Barrier(ctx.list, scratch.colour, scratch.colourState, D3D12_RESOURCE_STATE_COPY_SOURCE);
    scratch.colourState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ctx.list->CopyResource(backBuffer, scratch.colour);
    Barrier(ctx.list, backBuffer, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);

    ctx.list->Close();
    ID3D12CommandList* lists[] = { ctx.list };
    queue->ExecuteCommandLists(1, lists);

    // Correctness first (Fase 1): block until this frame's synthetic pass has actually retired
    // before Present submits the same back buffer. Async/non-blocking is Fase 2+ once this route
    // has a real correctness baseline to optimize against.
    ctx.fenceValue++;
    if (SUCCEEDED(queue->Signal(ctx.fence, ctx.fenceValue)) &&
        ctx.fence->GetCompletedValue() < ctx.fenceValue)
    {
        ctx.fence->SetEventOnCompletion(ctx.fenceValue, ctx.fenceEvent);
        WaitForSingleObject(ctx.fenceEvent, INFINITE);
    }
    return true;
}

namespace
{
// Fase 4: same "no native contract" problem as Fase 1's EvaluateSynthetic, but for a D3D11 game --
// there is no D3D12 device to be same-device on at all. Rides Dx11WithDx12, the private-D3D12-
// device/NT-shared-handle/fence bridge OptiScaler's own native D3D11 DLSS path already uses (see
// IFeature_Dx11wDx12::Evaluate), instead of re-deriving that synchronization from scratch.
struct SyntheticDx11Guides
{
    ID3D11Device* device = nullptr;
    ID3D11Texture2D* depth = nullptr;
    ID3D11Texture2D* motion = nullptr;
    UINT width = 0;
    UINT height = 0;
    bool prepared = false;

    void ReleaseAll()
    {
        if (depth != nullptr) { depth->Release(); depth = nullptr; }
        if (motion != nullptr) { motion->Release(); motion = nullptr; }
        width = 0;
        height = 0;
        prepared = false;
    }
};

ID3D11Texture2D* CreateZeroTexture2D(ID3D11Device* device, UINT width, UINT height, DXGI_FORMAT format,
                                     size_t bytesPerPixel)
{
    // CreateTexture2D reads every row described by Height and SysMemPitch.
    std::vector<unsigned char> zeros(static_cast<size_t>(width) * bytesPerPixel * height, 0);

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial = {};
    initial.pSysMem = zeros.data();
    initial.SysMemPitch = static_cast<UINT>(width * bytesPerPixel);

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &initial, &texture)))
        return nullptr;
    return texture;
}

bool EnsureSyntheticDx11Guides(ID3D11Device* device, UINT width, UINT height, SyntheticDx11Guides& guides)
{
    if (guides.depth != nullptr && guides.device == device && guides.width == width && guides.height == height)
        return true;

    guides.ReleaseAll();
    guides.device = device;
    guides.depth = CreateZeroTexture2D(device, width, height, DXGI_FORMAT_R32_FLOAT, sizeof(float));
    guides.motion = CreateZeroTexture2D(device, width, height, DXGI_FORMAT_R16G16_FLOAT, 2 * sizeof(uint16_t));
    if (guides.depth == nullptr || guides.motion == nullptr)
    {
        guides.ReleaseAll();
        return false;
    }
    guides.width = width;
    guides.height = height;
    return true;
}

// NGX writes the model's answer through a UAV, which a back buffer and every mirror of it refuse.
struct SyntheticDx11Output
{
    ID3D12Device* device = nullptr;
    ID3D12Resource* texture = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
    UINT width = 0;
    UINT height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

bool EnsureSyntheticDx11Output(ID3D12Device* device, UINT width, UINT height, DXGI_FORMAT format,
                               SyntheticDx11Output& out)
{
    if (out.texture != nullptr && out.device == device && out.width == width && out.height == height &&
        out.format == format)
        return true;

    if (out.texture != nullptr)
    {
        out.texture->Release();
        out.texture = nullptr;
    }

    out.texture = CreateSyntheticTexture(device, width, height, format,
                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_COMMON);
    if (out.texture == nullptr)
        return false;

    out.device = device;
    out.state = D3D12_RESOURCE_STATE_COMMON;
    out.width = width;
    out.height = height;
    out.format = format;
    return true;
}

constexpr UINT kSyntheticDx11Slots = 3;

// One list is enough -- a list may be reset right after submission -- but an allocator may not.
struct SyntheticDx11Context
{
    ID3D12Device* device = nullptr;
    ID3D12CommandAllocator* allocators[kSyntheticDx11Slots] = {};
    UINT64 slotFence[kSyntheticDx11Slots] = {};
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    UINT slot = 0;
};

bool EnsureSyntheticDx11Context(ID3D12Device* device, SyntheticDx11Context& ctx)
{
    if (ctx.list != nullptr && ctx.device == device)
        return true;

    if (ctx.list != nullptr) ctx.list->Release();
    for (auto*& allocator : ctx.allocators)
    {
        if (allocator != nullptr) allocator->Release();
    }
    if (ctx.fence != nullptr) ctx.fence->Release();
    if (ctx.fenceEvent != nullptr) CloseHandle(ctx.fenceEvent);
    ctx = SyntheticDx11Context{};

    for (auto*& allocator : ctx.allocators)
    {
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
            return false;
    }
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx.allocators[0], nullptr,
                                         IID_PPV_ARGS(&ctx.list))))
        return false;
    ctx.list->Close();
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ctx.fence))))
        return false;
    ctx.fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    if (ctx.fenceEvent == nullptr)
        return false;
    ctx.device = device;
    return true;
}
} // namespace

bool EvaluateSyntheticDx11(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D11Resource* backBuffer)
{
    const Config& cfg = *Config::Instance();
    if (!cfg.DlssNrEnabled.value_or_default() || dx11Device == nullptr || dx11Context == nullptr ||
        backBuffer == nullptr)
        return false;

    ID3D11Texture2D* backBufferTexture = nullptr;
    if (FAILED(backBuffer->QueryInterface(IID_PPV_ARGS(&backBufferTexture))) || backBufferTexture == nullptr)
        return false;
    D3D11_TEXTURE2D_DESC backDesc = {};
    backBufferTexture->GetDesc(&backDesc);
    backBufferTexture->Release();
    if (backDesc.Width == 0 || backDesc.Height == 0)
        return false;

    static ID3D11Device* initedDevice = nullptr;
    if (initedDevice != dx11Device)
    {
        Dx11WithDx12::Init(dx11Device, dx11Context);
        initedDevice = dx11Device;
    }

    ID3D12Device* dx12Device = Dx11WithDx12::GetD3D12Device();
    ID3D12CommandQueue* dx12Queue = Dx11WithDx12::GetD3D12CommandQueue();
    if (dx12Device == nullptr || dx12Queue == nullptr)
    {
        ReportSkipOnce("the D3D11-with-D3D12 bridge has no resolved device/queue (synthetic D3D11 route)");
        return false;
    }

    static SyntheticDx11Guides guides;
    if (!EnsureSyntheticDx11Guides(dx11Device, backDesc.Width, backDesc.Height, guides))
    {
        ReportSkipOnce("the synthetic D3D11 dummy guides could not be built");
        return false;
    }

    NVSDK_NGX_Parameter* params = nullptr;
    // OptiScaler's own exported NVSDK_NGX_D3D11_AllocateParameters (not the raw NVNGXProxy pointer):
    // it falls back to a self-contained NVNGX_Parameters when the installed driver's own nvngx.dll
    // does not export this entry point -- confirmed to happen on real hardware (Far Cry Primal,
    // RTX 40 laptop GPU driver: NVNGXProxy::D3D11_AllocateParameters() resolved to nullptr while the
    // D3D12 entry point did not).
    if (NVSDK_NGX_D3D11_AllocateParameters(&params) != NVSDK_NGX_Result_Success || params == nullptr)
    {
        ReportSkipOnce("the NGX core refused to allocate synthetic D3D11 parameters");
        return false;
    }

    params->Set(NVSDK_NGX_Parameter_Color, backBuffer);
    params->Set(NVSDK_NGX_Parameter_Output, backBuffer);
    params->Set(NVSDK_NGX_Parameter_Depth, static_cast<ID3D11Resource*>(guides.depth));
    params->Set(NVSDK_NGX_Parameter_MotionVectors, static_cast<ID3D11Resource*>(guides.motion));

    static unsigned long long syntheticDx11FrameCounter = 0;
    ++syntheticDx11FrameCounter;
    const UINT frameIndex = static_cast<UINT>(syntheticDx11FrameCounter % DX11_WITH_DX12_CACHED_FRAMES);
    const auto frameId = Dx11WithDx12::NextUpscalerFrameId();

    // The guides are constant zeros, so carrying them across the bridge again every frame buys
    // nothing; their mirrors stay in the cache and only a resolution change rebuilds them.
    auto mask = Dx11WithDx12::ResourceMask::Color | Dx11WithDx12::ResourceMask::Output;
    if (!guides.prepared)
        mask = mask | Dx11WithDx12::ResourceMask::Mv | Dx11WithDx12::ResourceMask::Depth;
    const auto prepareResult = Dx11WithDx12::PrepareUpscalerResources(
        params, mask, frameIndex, frameId, Config::Instance()->DontUseNTShared.value_or_default(), false, true);
    if (!prepareResult.Success)
    {
        ReportSkipOnce("Dx11WithDx12::PrepareUpscalerResources failed (synthetic D3D11 route)");
        NVSDK_NGX_D3D11_DestroyParameters(params);
        return false;
    }
    guides.prepared = true;

    auto& cache = Dx11WithDx12::GetUpscalerResourceCache();
    params->Set(NVSDK_NGX_Parameter_Color, (void*) cache.Color.Dx12Resource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, (void*) cache.Mv.Dx12Resource);
    params->Set(NVSDK_NGX_Parameter_Depth, (void*) cache.Depth.Dx12Resource);
    static SyntheticDx11Output synthOut;
    const bool scratchReady = EnsureSyntheticDx11Output(dx12Device, backDesc.Width, backDesc.Height,
                                                        backDesc.Format, synthOut);
    ID3D12Resource* const modelOutput = scratchReady ? synthOut.texture
                                                     : cache.Output[frameIndex].Dx12Resource;
    params->Set(NVSDK_NGX_Parameter_Output, (void*) modelOutput);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, backDesc.Width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, backDesc.Height);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0u);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    nrfusion::SyntheticDlaaConfig contractConfig{};
    contractConfig.nativeResolution = {backDesc.Width, backDesc.Height};
    contractConfig.workingScale = 1.0f; // Fase 4: 1:1 DLAA only, same scope as Fase 1.
    contractConfig.isHdr = (backDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT);
    contractConfig.depthInverted = true;
    const auto contract = nrfusion::SyntheticDlaaContract::CreateContract(contractConfig);
    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, contract.createFlags);
    params->Set(NVSDK_NGX_Parameter_OutWidth, backDesc.Width);
    params->Set(NVSDK_NGX_Parameter_OutHeight, backDesc.Height);
    params->Set(NVSDK_NGX_Parameter_Reset, 0u);

    static SyntheticDx11Context ctx;
    if (!EnsureSyntheticDx11Context(dx12Device, ctx))
    {
        NVSDK_NGX_D3D11_DestroyParameters(params);
        return false;
    }

    // Which layer family costs what was never measured, so optimising one kernel was a guess.
    static std::string nrProfileCsv = [] {
        std::string path;
        wchar_t module[MAX_PATH]{};
        HMODULE self = nullptr;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR) &EvaluateSyntheticDx11, &self) &&
            GetModuleFileNameW(self, module, MAX_PATH))
        {
            std::filesystem::path dir = std::filesystem::path(module).parent_path();
            const std::string ini = (dir / "OptiScaler.ini").string();
            char value[16]{};
            if (GetPrivateProfileStringA("DlssNr", "ProfileKernels", "", value, sizeof(value), ini.c_str()) > 0)
            {
                std::string on(value);
                for (char& c : on) c = (char) std::tolower(c);
                if (on == "true" || on == "1" || on == "yes" || on == "on")
                    path = (dir / "nrfusion_kernels.csv").string();
            }
        }
        return path;
    }();
    static unsigned long long nrProfileFrame = 0;
    static unsigned long long nrProfilePending = 0;
    static bool nrProfileOpen = false;
    if (!nrProfileCsv.empty() && nrProfileFrame < 400)
    {
        if (nrProfileOpen && ctx.fence->GetCompletedValue() >= nrProfilePending)
        {
            nrfusion::NrDiagnosticFrame measured{};
            NRFusion_ReadNrDiagnosticFrame(dx12Queue, ctx.fence, nrProfilePending, &measured);
            nrProfileOpen = false;
        }
        if (!nrProfileOpen &&
            NRFusion_BeginNrDiagnosticFrame(dx12Device, ++nrProfileFrame, 1, nrProfileCsv.c_str()))
            nrProfileOpen = true;
    }

    const UINT ctxSlot = ctx.slot;
    ctx.slot = (ctx.slot + 1) % kSyntheticDx11Slots;

    // Resetting an allocator the GPU still reads corrupts driver state, so wait only when this slot
    // has not retired -- with three slots the GPU is normally far enough ahead that it never does.
    if (ctx.slotFence[ctxSlot] != 0 && ctx.fence->GetCompletedValue() < ctx.slotFence[ctxSlot])
    {
        ctx.fence->SetEventOnCompletion(ctx.slotFence[ctxSlot], ctx.fenceEvent);
        if (WaitForSingleObject(ctx.fenceEvent, 2000) != WAIT_OBJECT_0)
        {
            NVSDK_NGX_D3D11_DestroyParameters(params);
            return false;
        }
    }

    if (FAILED(ctx.allocators[ctxSlot]->Reset()) || FAILED(ctx.list->Reset(ctx.allocators[ctxSlot], nullptr)))
    {
        NVSDK_NGX_D3D11_DestroyParameters(params);
        return false;
    }

    // Unseeded, a model bail-out leaves the target zeroed and paints the whole frame black.
    if (cache.Color.Dx12Resource != nullptr && modelOutput != nullptr)
    {
        if (scratchReady)
        {
            Barrier(ctx.list, synthOut.texture, synthOut.state, D3D12_RESOURCE_STATE_COPY_DEST);
            synthOut.state = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        ctx.list->CopyResource(modelOutput, cache.Color.Dx12Resource);
    }

    EvaluateAfterUpscale(ctx.list, params, dx12Queue, false, syntheticDx11FrameCounter);
    NVSDK_NGX_D3D11_DestroyParameters(params);

    // The model wrote into the scratch; the bridge only knows how to hand back its own mirror.
    if (scratchReady && cache.Output[frameIndex].Dx12Resource != nullptr)
    {
        Barrier(ctx.list, synthOut.texture, synthOut.state, D3D12_RESOURCE_STATE_COPY_SOURCE);
        synthOut.state = D3D12_RESOURCE_STATE_COPY_SOURCE;
        ctx.list->CopyResource(cache.Output[frameIndex].Dx12Resource, synthOut.texture);
    }

    ctx.list->Close();
    ID3D12CommandList* lists[] = { ctx.list };
    dx12Queue->ExecuteCommandLists(1, lists);

    // No CPU block here: CopyUpscalerOutputToDx11 calls SyncDx12ToDx11, which signals this same queue
    // and makes the D3D11 context wait on the GPU timeline, so ordering already holds. Blocking the
    // game thread inside Present for the model to retire would just stop CPU and GPU overlapping.
    ctx.fenceValue++;
    dx12Queue->Signal(ctx.fence, ctx.fenceValue);
    ctx.slotFence[ctxSlot] = ctx.fenceValue;
    if (nrProfileOpen) nrProfilePending = ctx.fenceValue;

    // Dx11WithDx12::CopyUpscalerOutputToDx11 already knows where the answer belongs: PrepareUpscalerResources
    // cached the original D3D11 Output pointer (our own back buffer) the moment it read it from params.
    if (!Dx11WithDx12::CopyUpscalerOutputToDx11(frameIndex))
    {
        ReportSkipOnce("Dx11WithDx12::CopyUpscalerOutputToDx11 failed (synthetic D3D11 route)");
        return false;
    }
    return true;
}
"""
        replace_once(dx12, synthetic_anchor, synthetic_anchor + synthetic_impl)

        wrapped_sc = opti / "wrapped/wrapped_swapchain.cpp"
        insert_after(wrapped_sc, "#include <menu/menu_overlay_dx.h>\n",
                     "#include <dlssnr/DlssNrFeature_Dx12.h>\n")
        replace_once(wrapped_sc,
            '    if (willPresent)\n'
            '    {\n'
            '        // Tick feature to let it know if it\'s frozen\n'
            '        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)\n'
            '            currentFeature->TickFrozenCheck();\n'
            '\n'
            '        // Draw overlay\n'
            '        MenuOverlayDx::Present(pSwapChain, SyncInterval, Flags, pPresentParameters, pDevice, hWnd, isUWP);\n',
            '    if (willPresent)\n'
            '    {\n'
            '        // NRFusion Synthetic route (compat architecture plan): only when nothing native is already\n'
            '        // driving Neural Rendering on this swapchain. Vulkan/x86 land the same way in later fases;\n'
            '        // this call is a no-op (returns false) on any other API today.\n'
            '        if (State::Instance().currentFeature == nullptr && Config::Instance()->DlssNrEnabled.value_or_default())\n'
            '        {\n'
            '            if (cq != nullptr && State::Instance().swapchainApi == DX12) // Fase 1\n'
            '            {\n'
            '                IDXGISwapChain3* synthSwapChain3 = nullptr;\n'
            '                if (pSwapChain->QueryInterface(IID_PPV_ARGS(&synthSwapChain3)) == S_OK)\n'
            '                {\n'
            '                    ID3D12Resource* synthBackBuffer = nullptr;\n'
            '                    auto synthBufferIndex = synthSwapChain3->GetCurrentBackBufferIndex();\n'
            '                    if (synthSwapChain3->GetBuffer(synthBufferIndex, IID_PPV_ARGS(&synthBackBuffer)) == S_OK)\n'
            '                    {\n'
            '                        DlssNr::EvaluateSynthetic(cq, synthBackBuffer);\n'
            '                        synthBackBuffer->Release();\n'
            '                    }\n'
            '                    synthSwapChain3->Release();\n'
            '                }\n'
            '            }\n'
            '            else if (isD3D11 && device != nullptr && State::Instance().swapchainApi == DX11) // Fase 4\n'
            '            {\n'
            '                ID3D11Resource* synthBackBuffer11 = nullptr;\n'
            '                if (pSwapChain->GetBuffer(0, IID_PPV_ARGS(&synthBackBuffer11)) == S_OK)\n'
            '                {\n'
            '                    ID3D11DeviceContext* synthContext = nullptr;\n'
            '                    device->GetImmediateContext(&synthContext);\n'
            '                    if (synthContext != nullptr)\n'
            '                    {\n'
            '                        DlssNr::EvaluateSyntheticDx11(device, synthContext, synthBackBuffer11);\n'
            '                        synthContext->Release();\n'
            '                    }\n'
            '                    synthBackBuffer11->Release();\n'
            '                }\n'
            '            }\n'
            '        }\n'
            '\n'
            '        // Tick feature to let it know if it\'s frozen\n'
            '        if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)\n'
            '            currentFeature->TickFrozenCheck();\n'
            '\n'
            '        // Draw overlay\n'
            '        MenuOverlayDx::Present(pSwapChain, SyncInterval, Flags, pPresentParameters, pDevice, hWnd, isUWP);\n')

    # Fase 5 of the compat architecture plan: a real, wired Synthetic route for a Vulkan game with no
    # native DLSS/FSR/XeSS contract. DLSS-NR's Vulkan path needs no D3D12 bridge (see the header
    # comment on DlssNrFeature_Vk.h): the model ships a complete native Vulkan surface. This only
    # manufactures the NVSDK_NGX_Parameter a native game would have built, from the swapchain's own
    # current image, and hands it to the existing, unmodified EvaluateAfterUpscaleVk.
    # RuntimeCapabilities::syntheticVulkan must stay false in GameProbe until this has been proven
    # against a real non-DLSS Vulkan game -- see the header comment on GameProbe::IntegratedCapabilities().
    vk_cpp = opti / "dlssnr/DlssNrFeature_Vk.cpp"
    if vk_cpp.exists() and "bool EvaluateSyntheticVk(" not in vk_h.read_text(encoding="utf-8"):
        insert_after(vk_h,
            'void ShutdownVk(bool deviceAlive = true);\n',
            '// Fase 5 of the compat architecture plan: a Vulkan game with no native DLSS/FSR/XeSS contract at\n'
            '// all. DLSS-NR\'s Vulkan path needs no D3D12 bridge (see the file header above), so this manufactures\n'
            '// the NVSDK_NGX_Parameter directly from the swapchain\'s own current image and hands it to\n'
            '// EvaluateAfterUpscaleVk above, unmodified. queueFamily is the family a command pool for this queue\n'
            '// must be created against (Vulkan has no query to recover it from a VkQueue after the fact).\n'
            'bool EvaluateSyntheticVk(VkQueue queue, uint32_t queueFamily, VkInstance instance, VkPhysicalDevice physicalDevice,\n'
            '                        VkDevice device, VkImage colourImage, VkFormat colourFormat, uint32_t width,\n'
            '                        uint32_t height);\n\n')

        insert_after(vk_cpp, "#include <cstring>\n",
                     "#include <cstdint>\n\n#include <nrfusion/SyntheticDlaaContract.hpp>\n")

        vk_anchor = (
            "    if (ranBefore)\n"
            "        return; // per-evaluate result, not a global frame counter that can suppress a different feature\n"
            "    bool applied = false;\n"
            "    EvaluateAtSeamVk(cmd, params, instance, pd, device, false, rayReconstruction, applied);\n"
            "}\n"
        )
        vk_impl = """
namespace
{
uint32_t FindMemoryTypeVk(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps {};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }
    return UINT32_MAX;
}

// Creates a small placeholder image, zero-filled via vkCmdClearColorImage, left in GENERAL so it can
// stand in for either a sampled read or a storage write without changing layout again -- same
// convention DlssNr_Vk's own _dummyImage already uses for an unrelated purpose.
bool CreateSyntheticZeroImageVk(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandBuffer cmd,
                                uint32_t width, uint32_t height, VkFormat format, VkImage& outImage,
                                VkDeviceMemory& outMemory, VkImageView& outView)
{
    VkImageCreateInfo info {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { width, height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &info, nullptr, &outImage) != VK_SUCCESS)
        return false;

    VkMemoryRequirements req {};
    vkGetImageMemoryRequirements(device, outImage, &req);
    VkMemoryAllocateInfo alloc {};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = FindMemoryTypeVk(physicalDevice, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (alloc.memoryTypeIndex == UINT32_MAX)
        return false;
    if (vkAllocateMemory(device, &alloc, nullptr, &outMemory) != VK_SUCCESS ||
        vkBindImageMemory(device, outImage, outMemory, 0) != VK_SUCCESS)
        return false;

    VkImageViewCreateInfo view {};
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = outImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device, &view, nullptr, &outView) != VK_SUCCESS)
        return false;

    VkImageMemoryBarrier toDst {};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = outImage;
    toDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);

    VkClearColorValue clear {};
    vkCmdClearColorImage(cmd, outImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &toDst.subresourceRange);

    VkImageMemoryBarrier toGeneral = toDst;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toGeneral);
    return true;
}

struct SyntheticVkGuides
{
    VkDevice device = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkImage motionImage = VK_NULL_HANDLE;
    VkDeviceMemory motionMemory = VK_NULL_HANDLE;
    VkImageView motionView = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;

    void ReleaseAll(VkDevice dev)
    {
        if (depthView != VK_NULL_HANDLE) vkDestroyImageView(dev, depthView, nullptr);
        if (depthImage != VK_NULL_HANDLE) vkDestroyImage(dev, depthImage, nullptr);
        if (depthMemory != VK_NULL_HANDLE) vkFreeMemory(dev, depthMemory, nullptr);
        if (motionView != VK_NULL_HANDLE) vkDestroyImageView(dev, motionView, nullptr);
        if (motionImage != VK_NULL_HANDLE) vkDestroyImage(dev, motionImage, nullptr);
        if (motionMemory != VK_NULL_HANDLE) vkFreeMemory(dev, motionMemory, nullptr);
        *this = SyntheticVkGuides {};
    }
};

bool EnsureSyntheticVkGuides(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandBuffer cmd, uint32_t width,
                             uint32_t height, SyntheticVkGuides& guides)
{
    if (guides.depthImage != VK_NULL_HANDLE && guides.device == device && guides.width == width &&
        guides.height == height)
        return true;

    guides.ReleaseAll(guides.device != VK_NULL_HANDLE ? guides.device : device);
    guides.device = device;

    if (!CreateSyntheticZeroImageVk(device, physicalDevice, cmd, width, height, VK_FORMAT_R32_SFLOAT, guides.depthImage,
                                    guides.depthMemory, guides.depthView) ||
        !CreateSyntheticZeroImageVk(device, physicalDevice, cmd, width, height, VK_FORMAT_R16G16_SFLOAT,
                                    guides.motionImage, guides.motionMemory, guides.motionView))
    {
        guides.ReleaseAll(device);
        return false;
    }

    guides.width = width;
    guides.height = height;
    return true;
}

struct SyntheticVkContext
{
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

bool EnsureSyntheticVkContext(VkDevice device, uint32_t queueFamily, SyntheticVkContext& ctx)
{
    if (ctx.cmd != VK_NULL_HANDLE && ctx.device == device && ctx.queueFamily == queueFamily)
        return true;

    if (ctx.pool != VK_NULL_HANDLE) vkDestroyCommandPool(ctx.device, ctx.pool, nullptr); // frees cmd too
    if (ctx.fence != VK_NULL_HANDLE) vkDestroyFence(ctx.device, ctx.fence, nullptr);
    ctx = SyntheticVkContext {};

    VkCommandPoolCreateInfo poolInfo {};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &ctx.pool) != VK_SUCCESS)
        return false;

    VkCommandBufferAllocateInfo cmdInfo {};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.commandPool = ctx.pool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device, &cmdInfo, &ctx.cmd) != VK_SUCCESS)
        return false;

    VkFenceCreateInfo fenceInfo {};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(device, &fenceInfo, nullptr, &ctx.fence) != VK_SUCCESS)
        return false;

    ctx.device = device;
    ctx.queueFamily = queueFamily;
    return true;
}
} // namespace

bool EvaluateSyntheticVk(VkQueue queue, uint32_t queueFamily, VkInstance instance, VkPhysicalDevice physicalDevice,
                        VkDevice device, VkImage colourImage, VkFormat colourFormat, uint32_t width,
                        uint32_t height)
{
    if (!Config::Instance()->DlssNrEnabled.value_or_default() || queue == VK_NULL_HANDLE ||
        device == VK_NULL_HANDLE || colourImage == VK_NULL_HANDLE || width == 0 || height == 0)
        return false;

    NVSDK_NGX_Parameter* params = nullptr;
    // OptiScaler's own exported shim (fallback-capable), matching the D3D11/D3D12 fix in
    // shaders/dlssnr/DlssNr_Dx12.cpp -- the same driver-export gap may exist here too.
    if (NVSDK_NGX_VULKAN_AllocateParameters(&params) != NVSDK_NGX_Result_Success || params == nullptr)
        return false;

    static SyntheticVkContext ctx;
    if (!EnsureSyntheticVkContext(device, queueFamily, ctx))
    {
        NVSDK_NGX_VULKAN_DestroyParameters(params);
        return false;
    }

    vkWaitForFences(device, 1, &ctx.fence, VK_TRUE, 0); // no-op the first time (unsignaled is fine to skip past)
    vkResetCommandBuffer(ctx.cmd, 0);
    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(ctx.cmd, &begin) != VK_SUCCESS)
    {
        NVSDK_NGX_VULKAN_DestroyParameters(params);
        return false;
    }

    static SyntheticVkGuides guides;
    if (!EnsureSyntheticVkGuides(device, physicalDevice, ctx.cmd, width, height, guides))
    {
        vkEndCommandBuffer(ctx.cmd);
        NVSDK_NGX_VULKAN_DestroyParameters(params);
        return false;
    }

    static VkImageView colourView = VK_NULL_HANDLE;
    static VkImage colourViewImage = VK_NULL_HANDLE;
    static VkDevice colourViewDevice = VK_NULL_HANDLE;
    if (colourView == VK_NULL_HANDLE || colourViewImage != colourImage || colourViewDevice != device)
    {
        if (colourView != VK_NULL_HANDLE)
            vkDestroyImageView(colourViewDevice, colourView, nullptr);
        VkImageViewCreateInfo view {};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = colourImage;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = colourFormat;
        view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        if (vkCreateImageView(device, &view, nullptr, &colourView) != VK_SUCCESS)
        {
            vkEndCommandBuffer(ctx.cmd);
            NVSDK_NGX_VULKAN_DestroyParameters(params);
            return false;
        }
        colourViewImage = colourImage;
        colourViewDevice = device;
    }

    const VkImageSubresourceRange colourRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    VkImageMemoryBarrier toGeneral {};
    toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = colourImage;
    toGeneral.subresourceRange = colourRange;
    toGeneral.srcAccessMask = 0;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(ctx.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toGeneral);

    NVSDK_NGX_Resource_VK colourResource = NVSDK_NGX_Create_ImageView_Resource_VK(
        colourView, colourImage, colourRange, colourFormat, width, height, true);
    const VkImageSubresourceRange guideRange { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    NVSDK_NGX_Resource_VK depthResource = NVSDK_NGX_Create_ImageView_Resource_VK(
        guides.depthView, guides.depthImage, guideRange, VK_FORMAT_R32_SFLOAT, width, height, false);
    NVSDK_NGX_Resource_VK motionResource = NVSDK_NGX_Create_ImageView_Resource_VK(
        guides.motionView, guides.motionImage, guideRange, VK_FORMAT_R16G16_SFLOAT, width, height, false);

    params->Set(NVSDK_NGX_Parameter_Color, (void*) &colourResource);
    params->Set(NVSDK_NGX_Parameter_Output, (void*) &colourResource); // In place: same image, per the D3D12 route's contract.
    params->Set(NVSDK_NGX_Parameter_Depth, (void*) &depthResource);
    params->Set(NVSDK_NGX_Parameter_MotionVectors, (void*) &motionResource);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, width);
    params->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, height);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X, 0u);
    params->Set(NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y, 0u);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    params->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);

    nrfusion::SyntheticDlaaConfig contractConfig {};
    contractConfig.nativeResolution = { width, height };
    contractConfig.workingScale = 1.0f; // Fase 5: 1:1 DLAA only, same scope as Fases 1 and 4.
    contractConfig.isHdr = (colourFormat == VK_FORMAT_R16G16B16A16_SFLOAT);
    contractConfig.depthInverted = true;
    const auto contract = nrfusion::SyntheticDlaaContract::CreateContract(contractConfig);
    params->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, contract.createFlags);
    params->Set(NVSDK_NGX_Parameter_OutWidth, width);
    params->Set(NVSDK_NGX_Parameter_OutHeight, height);
    params->Set(NVSDK_NGX_Parameter_Reset, 0u);

    EvaluateAfterUpscaleVk(ctx.cmd, params, instance, physicalDevice, device, false, false);

    NVSDK_NGX_VULKAN_DestroyParameters(params);

    VkImageMemoryBarrier toPresent = toGeneral;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(ctx.cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toPresent);

    if (vkEndCommandBuffer(ctx.cmd) != VK_SUCCESS)
        return false;

    if (vkResetFences(device, 1, &ctx.fence) != VK_SUCCESS)
        return false;

    VkSubmitInfo submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &ctx.cmd;
    if (vkQueueSubmit(queue, 1, &submit, ctx.fence) != VK_SUCCESS)
        return false;

    // Correctness first (same as Fases 1 and 4): block until this frame's synthetic pass has
    // actually retired before Present submits the same swapchain image.
    vkWaitForFences(device, 1, &ctx.fence, VK_TRUE, UINT64_MAX);
    return true;
}
"""
        replace_once(vk_cpp, vk_anchor, vk_anchor + vk_impl)

        vulkan_hooks = opti / "hooks/Vulkan_Hooks.cpp"
        insert_after(vulkan_hooks, "#include <dlssnr/DlssNr_VkExtensions.h>\n",
                     "#include <dlssnr/DlssNrFeature_Vk.h>\n")
        insert_after(vulkan_hooks, "#include <detours/detours.h>\n", "#include <vector>\n")
        insert_after(vulkan_hooks,
            "static VkDevice _device = VK_NULL_HANDLE;\n"
            "static VkInstance _instance = VK_NULL_HANDLE;\n"
            "static VkPhysicalDevice _PD = VK_NULL_HANDLE;\n"
            "static HWND _hwnd = nullptr;\n",
            "// The game's first requested queue family, captured at device creation -- Vulkan has no \"which\n"
            "// family does this VkQueue belong to\" query, so this is the only place to learn it. Used only to\n"
            "// build a command pool compatible with the queue the Synthetic route submits its own work to.\n"
            "static uint32_t _queueFamily = UINT32_MAX;\n"
            "// Vulkan has no query for an existing swapchain's format/extent -- only the CreateInfo that made it\n"
            "// has that, so it is captured there and matched back up here by handle for the Synthetic route.\n"
            "static VkSwapchainKHR _synthSwapchain = VK_NULL_HANDLE;\n"
            "static VkFormat _synthSwapchainFormat = VK_FORMAT_UNDEFINED;\n"
            "static uint32_t _synthSwapchainWidth = 0;\n"
            "static uint32_t _synthSwapchainHeight = 0;\n"
            "static std::vector<VkImage> _synthSwapchainImages;\n")

        replace_once(vulkan_hooks,
            "    auto result = o_vkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);\n"
            "\n"
            "    if (Config::Instance()->DlssNrEnabled.value_or_default())\n"
            "        LOG_INFO(\"DLSS-NR Vulkan: vkCreateDevice returned {} with {} extensions requested\", (int) result,\n"
            "                 localCreteInfo.enabledExtensionCount);\n",
            "    auto result = o_vkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);\n"
            "\n"
            "    if (result == VK_SUCCESS && pCreateInfo->queueCreateInfoCount > 0 && pCreateInfo->pQueueCreateInfos != nullptr)\n"
            "        _queueFamily = pCreateInfo->pQueueCreateInfos[0].queueFamilyIndex;\n"
            "\n"
            "    if (Config::Instance()->DlssNrEnabled.value_or_default())\n"
            "        LOG_INFO(\"DLSS-NR Vulkan: vkCreateDevice returned {} with {} extensions requested\", (int) result,\n"
            "                 localCreteInfo.enabledExtensionCount);\n")

        replace_once(vulkan_hooks,
            "    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&\n"
            "        !State::Instance().vulkanSkipHooks)\n"
            "    {\n",
            "    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE)\n"
            "    {\n"
            "        // NRFusion Synthetic route: only the format/extent/handle bookkeeping, independent of the\n"
            "        // menu-overlay gate below so the route works even with OverlayMenu disabled.\n"
            "        _synthSwapchain = *pSwapchain;\n"
            "        _synthSwapchainFormat = pCreateInfo->imageFormat;\n"
            "        _synthSwapchainWidth = pCreateInfo->imageExtent.width;\n"
            "        _synthSwapchainHeight = pCreateInfo->imageExtent.height;\n"
            "        _synthSwapchainImages.clear();\n"
            "    }\n"
            "\n"
            "    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&\n"
            "        !State::Instance().vulkanSkipHooks)\n"
            "    {\n")

        replace_once(vulkan_hooks,
            "    // Tick feature to let it know if it's frozen\n"
            "    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)\n"
            "        currentFeature->TickFrozenCheck();\n"
            "\n"
            "    VkPresentInfoKHR localPresentInfo {};\n"
            "    memcpy(&localPresentInfo, pPresentInfo, sizeof(VkPresentInfoKHR));\n"
            "\n"
            "    // render menu if needed\n"
            "    if (!MenuOverlayVk::QueuePresent(queue, &localPresentInfo))\n",
            "    // Tick feature to let it know if it's frozen\n"
            "    if (auto currentFeature = State::Instance().currentFeature; currentFeature != nullptr)\n"
            "        currentFeature->TickFrozenCheck();\n"
            "\n"
            "    // NRFusion Synthetic route (Fase 5 of the compat architecture plan): only when nothing native is\n"
            "    // already driving Neural Rendering on this swapchain. DLSS-NR's Vulkan path is fully native (no\n"
            "    // D3D12 bridge needed -- see DlssNrFeature_Vk.h), so this runs directly on the game's own device.\n"
            "    if (State::Instance().currentFeature == nullptr && Config::Instance()->DlssNrEnabled.value_or_default() &&\n"
            "        _device != VK_NULL_HANDLE && _PD != VK_NULL_HANDLE && _instance != VK_NULL_HANDLE &&\n"
            "        _queueFamily != UINT32_MAX && pPresentInfo->swapchainCount > 0 &&\n"
            "        pPresentInfo->pSwapchains[0] == _synthSwapchain && _synthSwapchainWidth > 0 && _synthSwapchainHeight > 0)\n"
            "    {\n"
            "        if (_synthSwapchainImages.empty())\n"
            "        {\n"
            "            uint32_t count = 0;\n"
            "            if (vkGetSwapchainImagesKHR(_device, _synthSwapchain, &count, nullptr) == VK_SUCCESS && count > 0)\n"
            "            {\n"
            "                _synthSwapchainImages.resize(count);\n"
            "                if (vkGetSwapchainImagesKHR(_device, _synthSwapchain, &count, _synthSwapchainImages.data()) !=\n"
            "                    VK_SUCCESS)\n"
            "                    _synthSwapchainImages.clear();\n"
            "            }\n"
            "        }\n"
            "        const auto synthImageIndex = pPresentInfo->pImageIndices[0];\n"
            "        if (synthImageIndex < _synthSwapchainImages.size())\n"
            "        {\n"
            "            DlssNr::EvaluateSyntheticVk(queue, _queueFamily, _instance, _PD, _device,\n"
            "                                        _synthSwapchainImages[synthImageIndex], _synthSwapchainFormat,\n"
            "                                        _synthSwapchainWidth, _synthSwapchainHeight);\n"
            "        }\n"
            "    }\n"
            "\n"
            "    VkPresentInfoKHR localPresentInfo {};\n"
            "    memcpy(&localPresentInfo, pPresentInfo, sizeof(VkPresentInfoKHR));\n"
            "\n"
            "    // render menu if needed\n"
            "    if (!MenuOverlayVk::QueuePresent(queue, &localPresentInfo))\n")

    print("NRFusion adaptive WorkingScale is wired into DX12 and Vulkan.")
    print("Current feed: existing NR GPU timer. Queue/frame telemetry can be added incrementally without changing the controller API.")

if __name__ == "__main__":
    main()
