module;
#include <d3d12.h>
#include <string>
#include <cstring>
#include <optional>
#include <DirectXCollision.h>
#include <entt/entt.hpp>
#include <memory>
#include <unordered_map>

#include <DirectXMath.h>
export module App.Luminous.OrbOcclusionFeature;

import Engine.Common.Hash;
import Engine.Graphics.RenderGraph;
import Engine.Graphics.Systems.MeshRender;
import Engine.Graphics.Pipeline;
import App.Graphics.SceneData;
import Engine.Graphics.RenderPipeline;
import App.Luminous.OrbOcclusionData;
import Engine.Graphics.Components.Mesh;
import Engine.Graphics.Components.Material;
import Engine.Core.Components.Transform;
import Engine.Graphics.MaterialManager;
import Engine.Graphics.Assets.Material;
import Engine.Core.Services;

export namespace App::Luminous {

    using namespace App::Graphics;

    using namespace Engine::Graphics;
    using namespace Engine::Common;
    using namespace DirectX;

    // ================================================================
    // 宝玉遮蔽専用の深度アトラス生成パス (Phase 2)
    //
    // ・OrbOccluderTag を持つサブメッシュ (通常壁の石材・通常床スラブ) だけを描画。
    //   格子壁・窓の鉄格子・反転マテリアル・台座・小物は描画対象外＝光を透過。
    // ・ピクセルシェーダー無し (ForceDepthOnly) の深度書き込みのみ。
    // ・宝玉ごとに 6 面ぶんのスロットを持ち、座標が変わったスロットだけ描き直す。
    //   同時に持ち運べる宝玉は 1 個までで他は台座に固定されるため、定常状態で
    //   更新されるのは手持ちの 1 個ぶん (6 面) だけになる。
    // ================================================================
    class OrbOcclusionPass : public IRenderPass {
    public:
        OrbOcclusionPass(Hash64 passHash, const PassBaseDesc& passDesc)
            : passHash_(passHash), passDesc_(passDesc) {
        }

        std::string GetName() const override { return "OrbOcclusionPass"; }

        void Setup(RenderPassSetupContext& ctx) override {
            ctx.ProduceData<OrbOcclusionInfo>();
            MeshRenderSystem::DeclareUsage(ctx);
            ctx.CreateTexture("OrbOcclusionAtlas", {
                ORB_OCCLUSION_ATLAS_W, ORB_OCCLUSION_ATLAS_H, 1,
                DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                std::nullopt, true });
            atlasDSV_ = ctx.WriteDepthStencil("OrbOcclusionAtlas");
        }

        void Execute(RenderPassExecuteContext& ctx) override {
            auto& registry = ctx.GetRegistry();
            auto* cmdList = ctx.GetNativeCommandList();

            const auto* request = registry.ctx().find<OrbOcclusionRequest>();

            // 初回はアトラス全面を最遠 (1.0) で初期化しておく。
            // このフレームはメッシュのワールド AABB がまだ確定していないので描かない。
            if (firstExecution_) {
                firstExecution_ = false;
                ctx.BindRenderTargets({}, atlasDSV_);
                cmdList->ClearDepthStencilView(ctx.GetDSV(atlasDSV_), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
                InvalidateAll();
                PublishInfo(ctx, OrbOcclusionRequest{}, false);
                return;
            }

            if (request == nullptr || request->Count == 0) {
                InvalidateAll();
                PublishInfo(ctx, OrbOcclusionRequest{}, false);
                return;
            }

            // 遮蔽ジオメトリの総数。シーン構築直後はエンティティが順次生成されるため、
            // 数が変化している間はアトラスを描き直し続ける。これを見ていないと
            // 「まだ壁が 1 枚も存在しないフレーム」に描いた空のアトラスがキャッシュされ、
            // 宝玉を動かすまで影が反映されない、という症状になる。
            size_t occluderCount = 0;
            for (auto e : registry.view<OrbOccluderTag>()) { (void)e; ++occluderCount; }

            if (request->GeometryRevision != cachedRevision_ || occluderCount != cachedOccluderCount_) {
                InvalidateAll();
                cachedRevision_ = request->GeometryRevision;
                cachedOccluderCount_ = occluderCount;
                // 数が揃った直後でもワールド AABB の更新が 1 フレーム遅れる場合が
                // あるため、数フレームは無条件に描き直して取りこぼしを防ぐ。
                warmupFrames_ = 3;
            }

            // マテリアルの PSO は非同期コンパイルされる。まだ 1 つも描ける状態に
            // なっていないうちに焼くと空のアトラスが確定キャッシュされてしまうため、
            // 描画可能になるまでキャッシュを確定させず毎フレーム再挑戦する。
            // (ステージ開始直後に影が反映されない不具合の根本原因)
            if (!HasDrawableOccluder(registry)) {
                InvalidateAll();
                PublishInfo(ctx, *request, false);
                return;
            }

            const uint32_t orbCount = (request->Count < ORB_OCCLUSION_MAX_ORBS)
                ? request->Count : ORB_OCCLUSION_MAX_ORBS;

            // 描き直しが必要なスロット (= 座標が動いた宝玉) を洗い出す
            bool needsRedraw[ORB_OCCLUSION_MAX_ORBS] = {};
            bool anyRedraw = false;
            for (uint32_t i = 0; i < orbCount; ++i) {
                if (warmupFrames_ > 0 || !slotValid_[i] || !IsSameOrbEntry(slotOrb_[i], request->Orbs[i])) {
                    needsRedraw[i] = true;
                    anyRedraw = true;
                }
            }
            for (uint32_t i = orbCount; i < ORB_OCCLUSION_MAX_ORBS; ++i) slotValid_[i] = false;

            if (anyRedraw) {
                ctx.BindRenderTargets({}, atlasDSV_);

                const float atlasW = static_cast<float>(ORB_OCCLUSION_ATLAS_W);
                const float atlasH = static_cast<float>(ORB_OCCLUSION_ATLAS_H);

                static const XMVECTOR kTargets[6] = {
                    XMVectorSet(1, 0, 0, 0), XMVectorSet(-1, 0, 0, 0),
                    XMVectorSet(0, 1, 0, 0), XMVectorSet(0, -1, 0, 0),
                    XMVectorSet(0, 0, 1, 0), XMVectorSet(0, 0, -1, 0)
                };
                static const XMVECTOR kUps[6] = {
                    XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 1, 0, 0),
                    XMVectorSet(0, 0, -1, 0), XMVectorSet(0, 0, 1, 0),
                    XMVectorSet(0, 1, 0, 0), XMVectorSet(0, 1, 0, 0)
                };

                for (uint32_t orbIdx = 0; orbIdx < orbCount; ++orbIdx) {
                    if (!needsRedraw[orbIdx]) continue;

                    const XMFLOAT4 orb = request->Orbs[orbIdx];
                    const XMVECTOR orbPos = XMVectorSet(orb.x, orb.y, orb.z, 1.0f);
                    const float farZ = (orb.w > ORB_OCCLUSION_NEAR_Z * 4.0f) ? orb.w : 1.0f;

                    const XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, ORB_OCCLUSION_NEAR_Z, farZ);
                    const BoundingSphere orbSphere(XMFLOAT3(orb.x, orb.y, orb.z), farZ);

                    for (uint32_t face = 0; face < 6; ++face) {
                        const uint32_t slot = orbIdx * 6 + face;
                        const XMFLOAT4 rect = GetOrbOcclusionFaceRect(slot);

                        const float px = rect.x * atlasW;
                        const float py = rect.y * atlasH;
                        const float pw = rect.z * atlasW;
                        const float ph = rect.w * atlasH;

                        ctx.SetViewportAndScissor(pw, ph, px, py);

                        D3D12_RECT clearRect = {
                            static_cast<LONG>(px), static_cast<LONG>(py),
                            static_cast<LONG>(px + pw), static_cast<LONG>(py + ph)
                        };
                        cmdList->ClearDepthStencilView(ctx.GetDSV(atlasDSV_), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, &clearRect);

                        const XMMATRIX view = XMMatrixLookAtLH(orbPos, XMVectorAdd(orbPos, kTargets[face]), kUps[face]);
                        const XMMATRIX vp = XMMatrixMultiply(view, proj);

                        SceneGlobalsCB faceGlobals = {};
                        XMStoreFloat4x4(&faceGlobals.View, XMMatrixTranspose(view));
                        XMStoreFloat4x4(&faceGlobals.Projection, XMMatrixTranspose(proj));
                        XMStoreFloat4x4(&faceGlobals.ViewProjection, XMMatrixTranspose(vp));
                        XMStoreFloat4x4(&faceGlobals.UnjitteredViewProjection, XMMatrixTranspose(vp));
                        faceGlobals.CameraPosition = XMFLOAT3(orb.x, orb.y, orb.z);
                        faceGlobals.ZBufferParams = XMFLOAT4(farZ, ORB_OCCLUSION_NEAR_Z, 0.0f, 0.0f);

                        DynamicAllocation alloc = ctx.AllocateDynamicSpace(sizeof(SceneGlobalsCB));
                        std::memcpy(alloc.CPUAddress, &faceGlobals, sizeof(SceneGlobalsCB));

                        // 光半径の球だけでカリングする。各面は 90 度視錐台なので厳密には
                        // 6 面ぶん重複して投入されるが、対象は宝玉半径内の壁・床スラブのみで
                        // 数十メッシュに過ぎず、視錐台カリングを重ねるより安全で安定している。
                        auto cull = [&](const BoundingBox& aabb) {
                            return orbSphere.Contains(aabb) != ContainmentType::DISJOINT;
                            };

                        MeshRenderSystem::Render<OrbOccluderTag>(ctx, passDesc_, alloc.GPUAddress, passHash_, cull);
                    }

                    slotOrb_[orbIdx] = orb;
                    slotValid_[orbIdx] = true;
                }
            }

            if (warmupFrames_ > 0) --warmupFrames_;

            PublishInfo(ctx, *request, true);
        }

    private:
        // OrbOccluderTag を持つメッシュのうち、このパス用の PSO が
        // コンパイル済みのものが 1 つでもあるか (先頭で打ち切るため実質 O(1))
        bool HasDrawableOccluder(entt::registry& registry) const {
            auto* matMgr = Engine::Core::FindService<MaterialManager>(registry);
            if (matMgr == nullptr) return false;
            auto view = registry.view<MeshComponent, MaterialComponent, OrbOccluderTag>();
            for (auto [e, mesh, mat] : view.each()) {
                if (!mesh.Handle.IsValid() || !mat.Handle.IsValid()) continue;
                MaterialInstance* inst = matMgr->GetInstance(mat.Handle);
                if (inst == nullptr) continue;
                MaterialAsset* asset = matMgr->GetAsset(inst->AssetHandle);
                if (asset != nullptr && asset->GetPSO(passHash_) != nullptr) return true;
            }
            return false;
        }

        void InvalidateAll() {
            for (uint32_t i = 0; i < ORB_OCCLUSION_MAX_ORBS; ++i) slotValid_[i] = false;
        }

        void PublishInfo(RenderPassExecuteContext& ctx, const OrbOcclusionRequest& req, bool valid) {
            OrbOcclusionInfo info = {};
            info.AtlasIndex = 0;
            info.Count = valid ? req.Count : 0u;
            info.NearZ = ORB_OCCLUSION_NEAR_Z;
            info.FarZ = (req.Count > 0 && req.Orbs[0].w > 0.0f) ? req.Orbs[0].w : 1.0f;
            for (uint32_t i = 0; i < ORB_OCCLUSION_MAX_ORBS; ++i) info.Orbs[i] = req.Orbs[i];
            info.Valid = valid;
            ctx.GetBlackboard().Set(info);
        }

        Hash64 passHash_;
        PassBaseDesc passDesc_;
        ViewHandle atlasDSV_;
        bool firstExecution_ = true;

        XMFLOAT4 slotOrb_[ORB_OCCLUSION_MAX_ORBS] = {};
        bool slotValid_[ORB_OCCLUSION_MAX_ORBS] = {};
        uint32_t cachedRevision_ = 0xFFFFFFFFu;
        size_t cachedOccluderCount_ = static_cast<size_t>(-1);
        int warmupFrames_ = 0;
    };

    // アトラスのバインドレス番号を結果に書き込み、シーン側 (次フレームの Luminous) へ公開する。
    // アトラスを読むことで、GBuffer でマテリアルがサンプリングする前に読み取りの状態へ遷移させる
    class OrbOcclusionPublishPass : public IRenderPass {
    public:
        std::string GetName() const override { return "OrbOcclusionPublishPass"; }

        void Setup(RenderPassSetupContext& ctx) override {
            ctx.ConsumeData<OrbOcclusionInfo>();
            atlasSRV_ = ctx.Read("OrbOcclusionAtlas");
        }

        void Execute(RenderPassExecuteContext& ctx) override {
            if (auto* info = ctx.GetBlackboard().Find<OrbOcclusionInfo>()) {
                info->AtlasIndex = ctx.GetSRVIndex(atlasSRV_);
                ctx.GetRegistry().ctx().insert_or_assign(*info);
            }
        }

    private:
        ViewHandle atlasSRV_;
    };

    // DeferredRenderPipeline に宝玉遮蔽を追加する
    //   pipeline->AddFeature<App::Luminous::OrbOcclusionFeature>();
    //   シーン: registry.ctx() に OrbOcclusionRequest を置き、OrbOcclusionInfo (前フレームの結果) を読む
    class OrbOcclusionFeature : public IRenderFeature {
    public:
        std::string GetName() const override { return "OrbOcclusion"; }

        void DefinePasses(std::unordered_map<Hash64, PassBaseDesc>& passes) override {
            // 深度のみ。PCF もバイアスも使わない (遮蔽体までの正しい距離が必要)。
            // メッシュの巻き方向に関係なく最も近い面を記録するため、カリングしない
            PassBaseDesc desc = {};
            desc.RootSignatureHash = HashString("Common");
            desc.Domain = MaterialDomain::Custom;
            desc.VSFilePath = L"Shaders/DefaultVSBase.hlsl";
            desc.PSFilePath = L"";
            desc.NumRenderTargets = 0;
            desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
            desc.ForceDepthOnly = true;
            desc.CullModeOverride = D3D12_CULL_MODE_NONE;
            // 傾きのバイアスだけを使う (宝玉から見て真横に近い床の自己遮蔽を防ぐ。正面の壁の深度はほぼ正確なまま)
            desc.DepthBias = 0;
            desc.SlopeScaledDepthBias = 2.5f;
            desc.DepthBiasClamp = 0.05f;
            passes[HashString("OrbOcclusionPass")] = desc;
        }

        void AddPasses(RenderStage stage, RenderFeatureContext& ctx) override {
            if (stage != RenderStage::AfterShadows) return;
            const Hash64 hash = HashString("OrbOcclusionPass");
            ctx.Graph.AddPass(std::make_shared<OrbOcclusionPass>(hash, ctx.PassDescs.at(hash)));
            ctx.Graph.AddPass(std::make_shared<OrbOcclusionPublishPass>());
        }
    };

} // namespace App::Luminous
