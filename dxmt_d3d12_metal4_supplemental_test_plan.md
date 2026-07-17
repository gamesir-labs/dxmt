# DXMT D3D12 → Metal 4 补充测试计划

审阅日期：2026-07-17

基线文档：`/Users/xiaoyi12/Downloads/dxmt_d3d12_metal4_test_plan.md`

审阅范围：原测试计划、当前 `tests/d3d12`、`tests/coverage/d3d12_coverage.json`，以及 `src/d3d12` 中公开接口和明确的 unsupported/error 分支。

## 1. 审阅结论

原计划对 command list、legacy barrier、descriptor、resource/copy、shader、graphics、query、sparse 和 fault injection 的框架已经很完整。当前仓库也已经从原计划所述的约 118 个测试增长到：

- 112 个已注册的 D3D12 `_spec.cpp` 文件；
- 700+ 个静态 GoogleTest 声明，另有参数矩阵生成的逻辑 case；
- 21 个 D3D12 测试目录；
- public API coverage manifest 审阅时只列出 85 个方法；第一轮扩展到
  96 个，第二轮扩展到 110 个，第三轮扩展到 137 个，但距离完整
  COM surface 仍有缺口；第五轮补入已有行为测试支撑的 9 个漏项，
  当前为 146 个。

下一批不应继续优先堆叠已有的 descriptor/copy 基础 case，而应补以下三类空洞：

1. 原计划没有覆盖的公开接口和实际游戏链路；
2. 原计划只按“unsupported”处理、但当前代码已经开始支持的能力；
3. 已有基础测试，但缺少状态、失败、跨进程或端到端 oracle 的能力。

## 2. P0：必须先补

### P0-1. 建立完整 Public API Surface Manifest

`tests/coverage/d3d12_coverage.json` 审阅时只登记 85 个 API，未覆盖大量已经出现在当前 COM vtable 中的方法。第一轮补入 11 个 versioned device/resource API；第二轮补入 custom/existing heap、device control 和 marker/event API；第三轮补入 protected session、state/meta/raytracing device 面和 versioned optional command 面，当前共 137 个。后续继续把 manifest 扩展为当前 headers/实现的完整清单，再允许以“API coverage 100%”作为门槛。

每个公开方法必须归入原计划定义的 A/B/C 之一：

- A：positive + negative + lifecycle/state；
- B：明确 unsupported + capability coherence + output/no-mutation/recovery；
- C：规范允许 no-op + no-op 不改变前后状态。

建议新增：

```text
tests/d3d12/device/versioned_api_spec.cpp
tests/d3d12/device/device_control_spec.cpp
tests/d3d12/resource/versioned_resource_spec.cpp
tests/d3d12/resource/existing_heap_spec.cpp
tests/d3d12/command/marker_event_spec.cpp
```

首批补齐的方法：

```text
CreateCommandQueue1
CreateCommittedResource1
CreateCommittedResource2
CreateHeap1
CreatePlacedResource1
CreateReservedResource1
GetResourceAllocationInfo2
GetCustomHeapProperties
SetStablePowerState
SetBackgroundProcessingMode
CreateShaderCacheSession
ShaderCacheControl
CheckDriverMatchingIdentifier
CreateLifetimeTracker
RemoveDevice
GetProtectedResourceSession
SetViewInstanceMask
SetProtectedResourceSession
BeginEvent / EndEvent / SetMarker（list 和 queue）
```

核心 case：

```text
VersionedResourceSpec.CreateCommittedResource1WithoutSessionMatchesBaseApi
VersionedResourceSpec.CreateCommittedResource2PreservesDesc1Fields
VersionedResourceSpec.CreatePlacedResource1MatchesBasePlacementAndGpuVa
VersionedResourceSpec.GetResourceAllocationInfo2MatchesV1Layout
VersionedDeviceSpec.CreateCommandQueue1MatchesBaseQueueContract
DeviceControlSpec.BackgroundProcessingSignalsEventAndClearsDesireFlag
DeviceControlSpec.StablePowerStateMatchesReferencePolicy
ShaderCacheSessionSpec.UnsupportedCreationClearsOutputAndControlIsCoherent
MarkerEventSpec.MarkersDoNotChangeExecutionOrCommandListState
```

完成标准：manifest 中当前公开方法 100% 被分类；不能再用“测试源码中出现过方法名”代替行为覆盖。

截至第二轮已完成：

```text
第一轮：11 个 versioned device/resource API case，manifest 85 -> 96
第二轮：12 个 custom/existing heap、device control、marker/event case，
        manifest 96 -> 110
第三轮：9 个 raytracing/VRS/protected/state-object fail-closed case，
        manifest 110 -> 137
第四轮：7 个 swapchain frame/lifecycle/contract case；DXGI 不计入 D3D12
        public API manifest，manifest 保持 137
第五轮：4 个 sampler-feedback/atomic-copy/stream-output case，并强化
        AtomicCopy UINT64 lifecycle；manifest 137 -> 146
GetCustomHeapProperties：修正并覆盖 CUSTOM type、UMA page/pool 和 NodeMask
OpenExistingHeapFromAddress：覆盖有效 VirtualAlloc -> placed buffer -> GPU copy
OpenExistingHeapFromFileMapping / CreateLifetimeTracker：覆盖 fail-closed 和输出清空
CheckDriverMatchingIdentifier：与未宣称 Raytracing capability 保持一致
SetMarker / BeginEvent / EndEvent：list 和 queue 均覆盖执行与 fence 不变
SetViewInstanceMask / SetProtectedResourceSession(nullptr)：覆盖合法执行不变
Present：覆盖 prior queue rendering 顺序与 3 个在途 back buffer 内容隔离
ResizeBuffers：覆盖失败原子性及 GPU 完成、引用释放后的完整重建
Swapchain state：覆盖 source/background/rotation/matrix/fullscreen round-trip
Occlusion/creation：用测试注入覆盖遮挡不推进，以及 device/非法 descriptor
fail-closed；真实 SW_MINIMIZE/窗口脱离仅允许在隔离图形会话或 VM 执行
Sampler Feedback：覆盖旧 UAV 被明确替换为 inert/null binding，GPU 写入无效
AtomicCopy UINT/UINT64：覆盖 E_NOTIMPL 锁存、Reset 拒绝及同 device 新 list 恢复
Stream Output：覆盖非空 target fail-close/recovery 与空 target 合法 no-op
```

第四轮运行安全说明：首次 16 条 swapchain 定向运行中，除
`ResizeAfterGpuCompletionRecreatesAllBuffers` 暴露 trailing barrier 内部引用
误判外，其余 15 条通过；修复后该失败 case 单独通过。随后聚合重跑在宿主
WindowServer 的 detached Metal layer/shared-event 路径触发系统组件断言，因此
不再在当前桌面会话复现。真实窗口最小化已经改为测试注入；最终聚合运行门槛
保留给隔离登录会话或 VM，宿主机只执行编译、静态检查和非 presentation 测试。

`RemoveDevice` 不能按当前 no-op 行为固化；规范要求进入真正的 device-removed 状态并唤醒 monitored fence，因此保留到 P0-4 的完整状态机一并实现和测试。

### P0-2. Enhanced Barrier Promotion Gate 与后续完整生成矩阵

第三轮复核修正：当前 `D3D12_FEATURE_D3D12_OPTIONS12` 明确报告 `EnhancedBarriersSupported == FALSE`，command list 最高只暴露到 `ID3D12GraphicsCommandList6`；现有三个 enhanced barrier 正向 case 会按能力 gate 跳过，`D3D12OptionalFeatureGateSpec` 则验证 `ID3D12GraphicsCommandList7` 返回 `E_NOINTERFACE`。因此本节当前是 promotion gate，不能把跳过的正向文件计入执行覆盖。

只有当同一实现变更同时启用 capability 和 `ID3D12GraphicsCommandList7::Barrier` 后，才必须立即启用以下完整矩阵：

建议新增：

```text
tests/d3d12/sync/enhanced_barrier_matrix_spec.cpp
tests/d3d12/sync/enhanced_barrier_validation_spec.cpp
tests/d3d12/sync/enhanced_legacy_parity_spec.cpp
```

矩阵至少覆盖：

```text
Barrier type: Global / Buffer / Texture
Queue: Direct / Compute / Copy
Scope: same list / cross list / cross Execute / cross queue + fence
Resource: buffer / 1D / 2D / 3D / array / mip / depth-stencil plane
Subresource: all / single mip / mip range / array range / plane range
Sync/access/layout: 每个被接受或宣称的值
Flags: NONE / DISCARD，以及非法未知位
Batch: 0 / 1 / 31 / 32 / 33 / 255 / 256 / 257
Path: native / fallback / mixed segment
```

必须加入 metamorphic oracle：

```text
Enhanced transition == equivalent legacy transition
One mixed barrier group == equivalent separated groups
Whole-resource barrier == complete per-subresource barrier set
Same-list ordering == split-list ordering with equivalent fence
```

负向 case 要验证 `Close()` 锁存、资源不被半更新、下一条合法 list 可恢复。

### P0-3. Pipeline Library Promotion Gate 与后续正向测试

原计划把 Pipeline Library 放在“不支持能力一致性”中。当前源码虽然包含 `StorePipeline`、`LoadGraphicsPipeline`、`LoadComputePipeline`、`LoadPipeline`、`Serialize` 的内部实现，但 `D3D12_FEATURE_SHADER_CACHE` 明确报告 NONE，`CreatePipelineLibrary` 也返回 `DXGI_ERROR_UNSUPPORTED`。现阶段保留现有 capability coherence 测试；只有在入口真正启用时，才把以下正向测试作为同一变更的 promotion gate。

建议新增：

```text
tests/d3d12/pipeline/library_contract_spec.cpp
tests/d3d12/pipeline/library_execution_spec.cpp
tests/d3d12/pipeline/library_concurrency_spec.cpp
```

核心 case：

```text
PipelineLibrarySpec.CreatesEmptyLibraryAndExposesStableComIdentity
PipelineLibrarySpec.StoreAndLoadGraphicsPipelineExecutesSamePixels
PipelineLibrarySpec.StoreAndLoadComputePipelineExecutesSameBufferResult
PipelineLibrarySpec.LoadPipelineStreamMatchesCreatePipelineState
PipelineLibrarySpec.DuplicateNameBehaviorMatchesReference
PipelineLibrarySpec.GraphicsComputeTypeMismatchFailsAndClearsOutput
PipelineLibrarySpec.NullNameNullPipelineAndUnsupportedIidAreRejected
PipelineLibrarySpec.CrossDevicePipelineIsRejected
PipelineLibrarySpec.SerializeSizeAndBufferValidationAreCoherent
PipelineLibrarySpec.SerializedBlobReopensInFreshDeviceOrFailsClosed
PipelineLibrarySpec.ConcurrentStoreAndLoadAreRaceFree
```

其中 duplicate-name、空序列化 blob 和错误 HRESULT 使用 WARP differential packet，不能直接把当前实现行为当作规范。

### P0-4. Device Removal、Metal Command Buffer Error 与 DRED

当前已有若干 fault injection 和 queue error case，但原计划没有形成完整的 device-lost 状态机。Metal command buffer/encoder 的异步失败是翻译层高风险路径，应有可注入、确定性的测试。

建议新增：

```text
tests/d3d12/device/device_removed_spec.cpp
tests/d3d12/device/dred_contract_spec.cpp
tests/d3d12/queue/async_failure_spec.cpp
```

状态机：

```text
Healthy
  -> submit
  -> asynchronous Metal failure
  -> error latched exactly once
  -> pending fence/event completed or cancelled according to policy
  -> later submissions rejected without replay
  -> objects can be released without hang
  -> fresh device/process can recover
```

核心 case：

```text
DeviceRemovedSpec.InitialReasonIsSuccess
DeviceRemovedSpec.FirstAsynchronousFailureIsSticky
DeviceRemovedSpec.ConcurrentFailuresDoNotReplaceFirstReason
DeviceRemovedSpec.PendingFenceWaitDoesNotHangAfterRemoval
DeviceRemovedSpec.FailedSubmissionDoesNotExecuteCommandsTwice
DeviceRemovedSpec.LaterSubmissionIsRejectedWithoutStateMutation
DeviceRemovedSpec.DeviceAndQueueDestructionDoNotDeadlock
DredSpec.SettingsInterfacesShareIdentityAndAcceptEveryEnablementValue
DredSpec.SettingsObjectLifetimeIsIndependentFromFactory
DredSpec.UnsupportedBreadcrumbOrPageFaultDataFailsClosed
DredSpec.FreshDeviceAfterInjectedFailureExecutesNormally
```

禁止用真实 GPU hang 做 PR 测试；使用内部 fault point 注入 command-buffer status、encoder creation failure 和 completion callback failure。

### P0-5. DXGI Swapchain / Present 端到端链路

原计划完全没有 Swapchain/Present。当前已有创建、基础 Present、ResizeBuffers、tearing、frame latency、color space/HDR 的 contract 测试，但仍缺少“渲染结果真正进入 present 链路”的 oracle 和复杂生命周期。

建议拆分现有文件并新增：

```text
tests/d3d12/presentation/swapchain_contract_spec.cpp
tests/d3d12/presentation/swapchain_frame_semantics_spec.cpp
tests/d3d12/presentation/swapchain_lifecycle_spec.cpp
tests/d3d12/presentation/swapchain_failure_spec.cpp
```

核心 case：

```text
SwapchainFrameSpec.RenderTransitionPresentAdvancesCorrectBackBuffer
SwapchainFrameSpec.PresentWaitsForPriorQueueRendering
SwapchainFrameSpec.MultipleFramesInFlightPreservePerBufferContents
SwapchainFrameSpec.PresentTestDoesNotConsumeOrAdvanceFrame
SwapchainLifecycleSpec.ResizeAfterGpuCompletionRecreatesAllBuffers
SwapchainLifecycleSpec.ResizeWithOutstandingReferencesFailsAtomically
SwapchainLifecycleSpec.PresentResizeDestroyRaceDoesNotDeadlock
SwapchainContractSpec.CreationRejectsNonQueueObjectAndInvalidDescriptor
SwapchainFrameSpec.BackBufferCannotBeUsedByForeignDeviceCommands
SwapchainContractSpec.MinimizedOrOccludedWindowHasStablePolicy
SwapchainContractSpec.FullscreenSourceSizeRotationAndBackgroundRoundTrip
SwapchainFailureSpec.DeviceRemovalUnblocksFrameLatencyWaiters
```

若无法稳定读取最终 drawable，增加仅测试构建启用的 native presentation hook，记录 drawable index、提交序号和最终 texture hash；不能只用 Present 返回 `S_OK` 作为 oracle。

### P0-6. Unsupported Optional Feature 全命令面一致性

当前已经覆盖部分 Raytracing、Mesh、VRS、Sampler Feedback、Protected Session、Meta Command 和 Sample Position，但每个能力通常只测了一个入口。需要把“capability 未宣称”扩展为完整方法矩阵。

建议生成以下方法族：

```text
Raytracing:
  CreateStateObject
  AddToStateObject
  GetRaytracingAccelerationStructurePrebuildInfo
  Build / Copy / EmitPostbuildInfo
  SetPipelineState1 / DispatchRays

VRS:
  RSSetShadingRate
  RSSetShadingRateImage

Sampler feedback:
  CreateSamplerFeedbackUnorderedAccessView

Protected resource:
  CreateProtectedResourceSession / 1
  CreateCommittedResource1 / CreateHeap1 / CreateReservedResource1 with session
  Resource::GetProtectedResourceSession
  CommandList::SetProtectedResourceSession

Other:
  Stream output PSO + SOSetTargets
  AtomicCopyBufferUINT / UINT64
  non-default OMSetDepthBounds
  non-reset SetSamplePositions
  suspended/resumed RenderPass
  MetaCommand initialization/execution
```

每个 void command 统一断言：

```text
Close returns explicit failure
failure is sticky
no prior or later command is replayed twice
no hidden state mutation
fresh legal list succeeds
device removed reason remains coherent
```

截至第五轮，`optional_command_contract_spec.cpp` 已覆盖 raytracing prebuild
输出清零、Build/Emit/Copy/SetPipelineState1、VRS image、Protected Session
v1/type query、state-object growth、Sampler Feedback inert descriptor、
AtomicCopy UINT/UINT64 和 Stream Output fail-close/no-op。带真实 protected
session/state object 的正向参数路径只有在能力启用后才可成为 must-pass。

## 3. P1：高价值语义补充

### P1-1. Shader System Value 与 Stage I/O 矩阵

原计划有 signature/stage I/O，但未明确列出 D3D12 应用常用的 system-value 语义。当前 shader matrix 主要覆盖算术、控制流、基础 resource、wave、GS/HS/DS。

建议新增：

```text
tests/d3d12/shader/system_value_spec.cpp
tests/d3d12/shader/stage_io_matrix_spec.cpp
tests/d3d12/shader/dxbc_dxil_parity_spec.cpp
```

覆盖：

```text
VS: SV_VertexID / SV_InstanceID
GS/DS/PS: SV_PrimitiveID
PS: SV_Position / SV_IsFrontFace / SV_SampleIndex / SV_Coverage
PS output: SV_Depth / SV_DepthGreaterEqual / SV_DepthLessEqual / SV_Coverage
Clip/cull distance arrays
NoInterpolation / linear / centroid / sample interpolation
Missing component、producer wider than consumer、register packing boundary
DXBC 与 DXIL 同语义结果
native 与 fallback 同语义结果
```

### P1-2. UAV Counter、Append/Consume 与 Atomic 完整矩阵

建议新增：

```text
tests/d3d12/shader/uav_counter_spec.cpp
tests/d3d12/shader/atomic_matrix_spec.cpp
```

覆盖：

```text
AppendStructuredBuffer / ConsumeStructuredBuffer
CounterOffsetInBytes: 0 / aligned non-zero / last valid / invalid
counter resource 与 data resource 相同或独立时的合法组合
groupshared / raw buffer / structured buffer / typed UAV atomic
Add / And / Or / Xor / Min / Max / Exchange / CompareExchange
返回 original value
单线程、单 group contention、多 group contention
UAV barrier、cross-list、cross-queue 可见性
32-bit；64-bit 仅在 capability 宣称时执行
```

### P1-3. Texture Sampling 与 Numeric Edge Matrix

当前只有少量 texture load/gather/sample 和基础浮点边界。补充 Metal 翻译最容易出现差异的组合：

```text
Texture1D / 2D / 3D / Cube / array / MSAA
Load / Sample / SampleLevel / SampleBias / SampleGrad
Gather RGBA / GatherCmp / offset variants
comparison sampling
mip、array slice、cube face、边界坐标、negative zero
UNORM / SNORM / UINT / SINT / FLOAT / sRGB
NaN、Inf、signed zero、subnormal、round-to-even、saturate
precise/no-contraction；16-bit、64-bit 和 min precision 按 capability gate
```

oracle 采用 exact integer、ULP、分类结果和 DXBC/DXIL metamorphic，不能对所有浮点格式统一使用固定 epsilon。

### P1-4. Graphics Fixed-Function 边界

建议新增：

```text
tests/d3d12/graphics/topology_spec.cpp
tests/d3d12/graphics/sample_frequency_spec.cpp
tests/d3d12/graphics/blend_logic_spec.cpp
tests/d3d12/graphics/optional_raster_state_spec.cpp
```

覆盖：

```text
point/line/triangle list 与 strip、adjacency、strip cut/restart
vertex slot gap、per-instance step rate、zero-size/null view、multi-slot boundary
front-face winding、cull mode、viewport array、negative viewport height policy
dual-source blend、logic op、alpha-to-coverage、independent blend interaction
sample mask、sample-frequency shader、SV_Coverage round-trip
conservative raster、depth bounds、programmable sample positions、view instancing：
  宣称则做 framebuffer oracle；未宣称则做完整 fail-closed matrix
render pass suspend/resume、MRT resolve、depth/stencil resolve 的能力一致性
```

### P1-5. Versioned Resource、Custom Heap 与 Shared Contract

建议新增：

```text
tests/d3d12/resource/custom_heap_spec.cpp
tests/d3d12/resource/existing_heap_spec.cpp
tests/d3d12/object/shared_contract_spec.cpp
```

覆盖：

```text
GetCustomHeapProperties 与 UMA/cache-coherent capability 一致
OpenExistingHeapFromAddress / OpenExistingHeapFromFileMapping 正向或明确失败
Create*Resource1/2 与 base API 的 desc、allocation、GPU VA、内容等价
GetDesc1 与 GetDesc 一致，受支持的 layout/flags 不丢失
GetHeapProperties null output、committed/placed/reserved 矩阵
shared resource/heap/fence flags 与 Create/OpenSharedHandle 能力一致
ALLOW_CROSS_ADAPTER、SHARED、SHARED_CROSS_ADAPTER 不得出现半支持状态
跨进程 packet：仅在 shared handle 真正支持后加入 must-pass
```

### P1-6. 合法多线程、对象生命周期与 Shutdown Race

原计划主要覆盖 descriptor concurrency，缺少跨对象的合法并发。

建议新增：

```text
tests/d3d12/concurrency/queue_submission_spec.cpp
tests/d3d12/concurrency/pipeline_creation_spec.cpp
tests/d3d12/concurrency/fence_event_spec.cpp
tests/d3d12/concurrency/device_shutdown_spec.cpp
```

覆盖：

```text
不同 command list/allocator 的并行录制
同一 device 上并行创建 resource、PSO、root signature、descriptor heap
多个 queue 的并行提交和 signal/wait
并行 pipeline library store/load、AIR cache miss/hit
SetEventOnCompletion 注册与 Signal/Release 竞争
queue destruction 与 callback arming 竞争
device teardown 时仍有完成回调、cache writer、residency entry
descriptor/resource client ref 已释放但 GPU submission 仍在飞行
```

只把规范允许的并发列为 conformance；同对象非法并发归 robustness，断言不崩溃而不是固定结果。

### P1-7. Cache 跨进程、文件系统错误与失效键

原计划覆盖 cold/warm/corruption，但未充分覆盖真实文件系统和进程边界。

建议新增：

```text
tests/d3d12/cache/process_replay_spec.cpp
tests/d3d12/cache/filesystem_failure_spec.cpp
tests/d3d12/cache/invalidation_key_spec.cpp
```

覆盖：

```text
两个进程同时首次写同一 cache/archive
writer 被终止后不留下可被误读的半文件
read-only directory、permission denied、disk-full fault、rename failure
corrupt entry 隔离，不污染其他 key
GPU family、OS/Metal version、compiler options、descriptor ABI 变化会 miss
debug name、pointer、padding、无关 PSO state 不会 miss
cold/warm、cache disabled、corrupt fallback 的最终 GPU 输出一致
```

### P1-8. Agility Core、Global Configuration 与 DRED Settings

当前已有基础 Agility tests，但原计划未覆盖这部分。继续补：

```text
SDK version/path 选择的进程级隔离
InitializeFromGlobalState / ApplyToGlobalState 顺序和重复调用
多个 factory 的 flags 与 experimental-feature 状态隔离
并发 GetInterface / CreateDeviceFactory
错误 SDK path/version 清空输出并允许后续正确创建
DRED settings object 的 COM contract 和 factory/global 隔离；将来接入
  device-removed 数据后再增加创建前后的生效边界
```

## 4. P2：兼容性、可观测性与长期压力

### P2-1. Marker/Event、Capture 与 Apitrace Replay

`SetMarker`、`BeginEvent`、`EndEvent` 即使是 no-op，也需要验证命令执行结果和状态完全不变；如果接入 Metal capture/signpost，再验证嵌套、空 payload、Unicode/PIX payload 和错误嵌套不会破坏提交。

增加小型 apitrace corpus：

```text
descriptor streaming frame
render-pass MRT frame
multi-queue upload/compute/render frame
sparse texture streaming frame
resize/present frame
```

原始执行与 replay 比较最终 resource hash、提交次数和 native/fallback segment trace。

### P2-2. Multi-adapter / NodeMask / Cross-adapter Fail-closed

当前通常是单 node。即使没有多 GPU，也应系统验证：

```text
GetNodeCount == 1 时 NodeMask 0/1 的规范行为
NodeMask 2、多 bit、visible/creation mask 不一致
cross-adapter heap/resource flags 不得被错误接受
跨 device queue/list/allocator/resource/PSO 的所有组合
adapter LUID、DXGI adapter、Metal device identity 一致
```

真实 linked-adapter 正向测试仅在硬件和实现支持时启用。

### P2-3. Residency、Budget 与内存压力

补充：

```text
QueryResourceResidency / OfferResources / ReclaimResources 的支持策略
大量 MakeResident/Evict/SetResidencyPriority 交错
budget reservation 改变与资源创建压力
allocation failure 后 residency set 不泄漏
进程内存压力下 cache、descriptor table、sparse mapping 的回收
```

压力测试不使用固定“必须 OOM 于第 N 次”的 oracle；使用可注入预算保证确定性。

### P2-4. 新能力启用前的 Promotion Gate

以下能力一旦从 NOT_SUPPORTED 改为支持，必须在同一变更中加入正向执行 suite，不能只修改 capability bit：

```text
Stream Output
Programmable Sample Positions
Depth Bounds
View Instancing
Variable Rate Shading
Sampler Feedback
Mesh Shader
Raytracing
Protected Resource Session
Meta Command
Shader Cache Session
```

## 5. 建议实施顺序

### Phase 0：1 个短迭代

1. 自动生成/校验 Public API manifest；
2. 把当前所有方法分类为 positive、unsupported 或 no-op；
3. 增加缺口报告，禁止 manifest 漏项仍显示 100%。

交付物：API 清单、coverage checker、第一批 versioned API contract tests。

### Phase 1：2–3 个迭代

1. 完成剩余 Public API/versioned API contract；
2. Enhanced Barrier 完整矩阵；
3. Device removal/async failure；
4. Swapchain frame semantics；
5. Optional feature 全命令面 fail-closed。

这些项目优先级最高，因为它们对应“已经暴露/已经开始支持，但行为矩阵还不完整”的路径。

### Phase 2：3–5 个迭代

1. Shader system value、UAV counter、texture/numeric matrix；
2. fixed-function 边界；
3. versioned memory/shared contract；
4. legal concurrency 和 cache process tests。

### Phase 3：持续建设

1. apitrace/真实帧 replay；
2. residency/budget 压力；
3. multi-adapter 和未来能力 promotion suite；
4. 长时间 stress 与多 GPU/OS 配置。

## 6. 新增完成标准

每个新增测试族必须同时满足：

```text
1. 明确 Class：Conformance / Differential / Robustness
2. 有稳定 CaseId 和单 case replay 参数
3. capability gate 不允许把“宣称但执行失败”变成 skip
4. GPU 可观察行为同时跑 native/fallback，或记录为何只有单路径
5. HRESULT 未被规范固定时保留 WARP reference packet
6. 负向 void command 检查 Close、no mutation、no replay、recovery
7. 异步测试有有界超时和明确的 pending/completed oracle
8. 批量矩阵失败能打印第一个 logical case 和完整参数
9. 新公开 API 同步进入 coverage manifest
10. 新 capability 同步增加 advertised-positive 或 unadvertised-negative suite
```

## 7. 不建议作为下一批主投入的区域

以下区域当前已经有较多基础与边界测试，除非 coverage、mutation 或真实 bug 指向具体缺口，否则下一批不应继续无差别扩张：

```text
Command list 基础生命周期
Legacy barrier 的 31/32/33 等 entry boundary
Descriptor 基础 shape/copy/table binding
基础 CopyBuffer/CopyTexture/Clear
基础 timestamp/occlusion/predication
Sparse buffer/texture 基础 mapping
基础 format capability query
```

这些模块后续应以 mutation survivor、未覆盖分支、游戏 trace 或明确 bug 为入口补 case，而不是继续按 API 名称堆测试。
