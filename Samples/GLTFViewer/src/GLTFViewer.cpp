/*
 *  Copyright 2019-2023 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#include <cmath>
#include <array>

#include "GLTFViewer.hpp"
#include "MapHelper.hpp"
#include "BasicMath.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "CommonlyUsedStates.h"
#include "ShaderMacroHelper.hpp"
#include "FileSystem.hpp"
#include "imgui.h"
#include "imGuIZMO.h"
#include "ImGuiUtils.hpp"
#include "CallbackWrapper.hpp"
#include "CommandLineParser.hpp"
#include "GraphicsAccessories.hpp"
#include "EnvMapRenderer.hpp"

namespace Diligent
{

namespace HLSL
{

#include "Shaders/Common/public/BasicStructures.fxh"
#include "Shaders/PBR/public/PBR_Structures.fxh"
#include "Shaders/PBR/private/RenderPBR_Structures.fxh"

} // namespace HLSL

SampleBase* CreateSample()
{
    return new GLTFViewer();
}

// clang-format off
const std::pair<const char*, const char*> GLTFViewer::GLTFModels[] =
{
    {"Damaged Helmet",      "models/DamagedHelmet/DamagedHelmet.gltf"},
    {"Metal Rough Spheres", "models/MetalRoughSpheres/MetalRoughSpheres.gltf"},
    {"Flight Helmet",       "models/FlightHelmet/FlightHelmet.gltf"},
    {"Cesium Man",          "models/CesiumMan/CesiumMan.gltf"},
    {"Boom Box",            "models/BoomBoxWithAxes/BoomBoxWithAxes.gltf"},
    {"Normal Tangent Test", "models/NormalTangentTest/NormalTangentTest.gltf"}
};
// clang-format on

GLTFViewer::GLTFViewer()
{
    m_Camera.SetDefaultSecondaryRotation(QuaternionF::RotationFromAxisAngle(float3{0.f, 1.0f, 0.0f}, -PI_F / 2.f));
    m_Camera.SetDistRange(0.1f, 5.f);
    m_Camera.SetDefaultDistance(0.9f);
    m_Camera.ResetDefaults();
    // Apply extra rotation to adjust the view to match Khronos GLTF viewer
    m_Camera.SetExtraRotation(QuaternionF::RotationFromAxisAngle(float3{0.75, 0.0, 0.75}, PI_F));
}

void GLTFViewer::LoadModel(const char* Path)
{
    if (m_Model)
    {
        m_PlayAnimation  = false;
        m_AnimationIndex = 0;
        m_AnimationTimers.clear();
    }

    GLTF::ModelCreateInfo ModelCI;
    ModelCI.FileName             = Path;
    ModelCI.pResourceManager     = m_bUseResourceCache ? m_pResourceMgr.RawPtr() : nullptr;
    ModelCI.ComputeBoundingBoxes = m_bComputeBoundingBoxes;

    m_Model.reset(new GLTF::Model{m_pDevice, m_pImmediateContext, ModelCI});

    m_ModelResourceBindings = m_GLTFRenderer->CreateResourceBindings(*m_Model, m_FrameAttribsCB);

    m_RenderParams.SceneIndex = m_Model->DefaultSceneId;
    UpdateScene();

    if (!m_Model->Animations.empty())
    {
        m_AnimationTimers.resize(m_Model->Animations.size());
        m_AnimationIndex = 0;
        m_PlayAnimation  = true;
    }

    m_CameraId = 0;
    m_CameraNodes.clear();
    for (const auto* node : m_Model->Scenes[m_RenderParams.SceneIndex].LinearNodes)
    {
        if (node->pCamera != nullptr && node->pCamera->Type == GLTF::Camera::Projection::Perspective)
            m_CameraNodes.push_back(node);
    }
}

void GLTFViewer::UpdateScene()
{
    m_Model->ComputeTransforms(m_RenderParams.SceneIndex, m_Transforms);
    m_ModelAABB = m_Model->ComputeBoundingBox(m_RenderParams.SceneIndex, m_Transforms);

    // Center and scale model
    float  MaxDim = 0;
    float3 ModelDim{m_ModelAABB.Max - m_ModelAABB.Min};
    MaxDim = std::max(MaxDim, ModelDim.x);
    MaxDim = std::max(MaxDim, ModelDim.y);
    MaxDim = std::max(MaxDim, ModelDim.z);

    float    Scale     = (1.0f / std::max(MaxDim, 0.01f)) * 0.5f;
    auto     Translate = -m_ModelAABB.Min - 0.5f * ModelDim;
    float4x4 InvYAxis  = float4x4::Identity();
    InvYAxis._22       = -1;

    m_ModelTransform = float4x4::Translation(Translate) * float4x4::Scale(Scale) * InvYAxis;
    m_Model->ComputeTransforms(m_RenderParams.SceneIndex, m_Transforms, m_ModelTransform);
    m_ModelAABB = m_Model->ComputeBoundingBox(m_RenderParams.SceneIndex, m_Transforms);
}

GLTFViewer::CommandLineStatus GLTFViewer::ProcessCommandLine(int argc, const char* const* argv)
{
    CommandLineParser ArgsParser{argc, argv};
    ArgsParser.Parse("use_cache", m_bUseResourceCache);
    ArgsParser.Parse("model", m_InitialModelPath);
    ArgsParser.Parse("compute_bounds", m_bComputeBoundingBoxes);

    return CommandLineStatus::OK;
}

void GLTFViewer::CreateGLTFResourceCache()
{
    auto InputLayout = GLTF::VertexAttributesToInputLayout(GLTF::DefaultVertexAttributes.data(), static_cast<Uint32>(GLTF::DefaultVertexAttributes.size()));
    auto Strides     = InputLayout.ResolveAutoOffsetsAndStrides();

    std::vector<VertexPoolElementDesc> VtxPoolElems;
    VtxPoolElems.reserve(Strides.size());
    m_CacheUseInfo.VtxLayoutKey.Elements.reserve(Strides.size());
    for (const auto& Stride : Strides)
    {
        VtxPoolElems.emplace_back(Stride, BIND_VERTEX_BUFFER);
        m_CacheUseInfo.VtxLayoutKey.Elements.emplace_back(Stride, BIND_VERTEX_BUFFER);
    }

    VertexPoolCreateInfo VtxPoolCI;
    VtxPoolCI.Desc.Name        = "GLTF vertex pool";
    VtxPoolCI.Desc.VertexCount = 32768;
    VtxPoolCI.Desc.pElements   = VtxPoolElems.data();
    VtxPoolCI.Desc.NumElements = static_cast<Uint32>(VtxPoolElems.size());

    std::array<DynamicTextureAtlasCreateInfo, 1> Atlases;
    Atlases[0].Desc.Name      = "GLTF texture atlas";
    Atlases[0].Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    Atlases[0].Desc.Usage     = USAGE_DEFAULT;
    Atlases[0].Desc.BindFlags = BIND_SHADER_RESOURCE;
    Atlases[0].Desc.Format    = TEX_FORMAT_RGBA8_UNORM;
    Atlases[0].Desc.Width     = 4096;
    Atlases[0].Desc.Height    = 4096;
    Atlases[0].Desc.MipLevels = 6;

    GLTF::ResourceManager::CreateInfo ResourceMgrCI;

    ResourceMgrCI.IndexAllocatorCI.Desc.Name      = "GLTF index buffer";
    ResourceMgrCI.IndexAllocatorCI.Desc.BindFlags = BIND_INDEX_BUFFER;
    ResourceMgrCI.IndexAllocatorCI.Desc.Usage     = USAGE_DEFAULT;
    ResourceMgrCI.IndexAllocatorCI.Desc.Size      = sizeof(Uint32) * 8 << 10;

    ResourceMgrCI.NumVertexPools = 1;
    ResourceMgrCI.pVertexPoolCIs = &VtxPoolCI;

    ResourceMgrCI.DefaultAtlasDesc.Desc.Type      = RESOURCE_DIM_TEX_2D_ARRAY;
    ResourceMgrCI.DefaultAtlasDesc.Desc.Usage     = USAGE_DEFAULT;
    ResourceMgrCI.DefaultAtlasDesc.Desc.BindFlags = BIND_SHADER_RESOURCE;
    ResourceMgrCI.DefaultAtlasDesc.Desc.Width     = 4096;
    ResourceMgrCI.DefaultAtlasDesc.Desc.Height    = 4096;
    ResourceMgrCI.DefaultAtlasDesc.Desc.MipLevels = 6;

    m_pResourceMgr = GLTF::ResourceManager::Create(m_pDevice, ResourceMgrCI);

    m_CacheUseInfo.pResourceMgr = m_pResourceMgr;

    m_CacheUseInfo.BaseColorFormat    = TEX_FORMAT_RGBA8_UNORM;
    m_CacheUseInfo.PhysicalDescFormat = TEX_FORMAT_RGBA8_UNORM;
    m_CacheUseInfo.NormalFormat       = TEX_FORMAT_RGBA8_UNORM;
    m_CacheUseInfo.OcclusionFormat    = TEX_FORMAT_RGBA8_UNORM;
    m_CacheUseInfo.EmissiveFormat     = TEX_FORMAT_RGBA8_UNORM;
}

void GLTFViewer::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    m_bWireframeSupported = m_pDevice->GetDeviceInfo().Features.WireframeFill;

    RefCntAutoPtr<ITexture> EnvironmentMap;
    CreateTextureFromFile("textures/papermill.ktx", TextureLoadInfo{"Environment map"}, m_pDevice, &EnvironmentMap);
    m_EnvironmentMapSRV = EnvironmentMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    auto BackBufferFmt  = m_pSwapChain->GetDesc().ColorBufferFormat;
    auto DepthBufferFmt = m_pSwapChain->GetDesc().DepthBufferFormat;

    GLTF_PBR_Renderer::CreateInfo RendererCI;
    RendererCI.RTVFmt                = BackBufferFmt;
    RendererCI.DSVFmt                = DepthBufferFmt;
    RendererCI.FrontCounterClockwise = true;

    if (m_bUseResourceCache)
        m_RenderParams.Flags |= GLTF_PBR_Renderer::PSO_FLAG_USE_TEXTURE_ATLAS;
    if (BackBufferFmt == TEX_FORMAT_RGBA8_UNORM || BackBufferFmt == TEX_FORMAT_BGRA8_UNORM)
        m_RenderParams.Flags |= GLTF_PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB;

    m_GLTFRenderer.reset(new GLTF_PBR_Renderer{m_pDevice, nullptr, m_pImmediateContext, RendererCI});

    CreateUniformBuffer(m_pDevice, sizeof(HLSL::PBRFrameAttribs), "PBR frame attribs buffer", &m_FrameAttribsCB);
    // clang-format off
    StateTransitionDesc Barriers [] =
    {
        {m_FrameAttribsCB, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_CONSTANT_BUFFER, STATE_TRANSITION_FLAG_UPDATE_STATE},
        {EnvironmentMap,   RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, STATE_TRANSITION_FLAG_UPDATE_STATE}
    };
    // clang-format on
    m_pImmediateContext->TransitionResourceStates(_countof(Barriers), Barriers);

    m_GLTFRenderer->PrecomputeCubemaps(m_pImmediateContext, m_EnvironmentMapSRV);

    RefCntAutoPtr<IRenderStateNotationParser> pRSNParser;
    {
        RefCntAutoPtr<IShaderSourceInputStreamFactory> pStreamFactory;
        m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("render_states", &pStreamFactory);

        CreateRenderStateNotationParser({}, &pRSNParser);
        pRSNParser->ParseFile("RenderStates.json", pStreamFactory);
    }

    RefCntAutoPtr<IRenderStateNotationLoader> pRSNLoader;
    {
        RefCntAutoPtr<IShaderSourceInputStreamFactory> pStreamFactory;
        m_pEngineFactory->CreateDefaultShaderSourceStreamFactory("shaders", &pStreamFactory);
        CreateRenderStateNotationLoader({m_pDevice, pRSNParser, pStreamFactory}, &pRSNLoader);
    }


    {
        EnvMapRenderer::CreateInfo EnvMapRendererCI;
        EnvMapRendererCI.pDevice          = m_pDevice;
        EnvMapRendererCI.pCameraAttribsCB = m_FrameAttribsCB;
        EnvMapRendererCI.NumRenderTargets = 1;
        EnvMapRendererCI.RTVFormats[0]    = BackBufferFmt;
        EnvMapRendererCI.DSVFormat        = DepthBufferFmt;

        m_EnvMapRenderer = std::make_unique<EnvMapRenderer>(EnvMapRendererCI);
    }

    CreateBoundBoxPSO(pRSNLoader);

    m_LightDirection = normalize(float3(0.5f, 0.6f, -0.2f));

    if (m_bUseResourceCache)
        CreateGLTFResourceCache();

    LoadModel(!m_InitialModelPath.empty() ? m_InitialModelPath.c_str() : GLTFModels[m_SelectedModel].second);
}

void GLTFViewer::UpdateUI()
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        {
            const char* Models[_countof(GLTFModels)];
            for (size_t i = 0; i < _countof(GLTFModels); ++i)
                Models[i] = GLTFModels[i].first;
            if (ImGui::Combo("Model", &m_SelectedModel, Models, _countof(GLTFModels)))
            {
                LoadModel(GLTFModels[m_SelectedModel].second);
            }
        }
#ifdef PLATFORM_WIN32
        if (ImGui::Button("Load model"))
        {
            FileDialogAttribs OpenDialogAttribs{FILE_DIALOG_TYPE_OPEN};
            OpenDialogAttribs.Title  = "Select GLTF file";
            OpenDialogAttribs.Filter = "glTF files\0*.gltf;*.glb\0";
            auto FileName            = FileSystem::FileDialog(OpenDialogAttribs);
            if (!FileName.empty())
            {
                LoadModel(FileName.c_str());
            }
        }
#endif
        if (m_Model->Scenes.size() > 1)
        {
            std::vector<std::pair<Uint32, std::string>> SceneList;
            SceneList.reserve(m_Model->Scenes.size());
            for (Uint32 i = 0; i < m_Model->Scenes.size(); ++i)
            {
                SceneList.emplace_back(i, !m_Model->Scenes[i].Name.empty() ? m_Model->Scenes[i].Name : std::to_string(i));
            }
            if (ImGui::Combo("Scene", &m_RenderParams.SceneIndex, SceneList.data(), static_cast<int>(SceneList.size())))
                UpdateScene();
        }

        if (!m_CameraNodes.empty())
        {
            std::vector<std::pair<Uint32, std::string>> CamList;
            CamList.emplace_back(0, "default");
            for (Uint32 i = 0; i < m_CameraNodes.size(); ++i)
            {
                const auto& Cam = *m_CameraNodes[i]->pCamera;
                CamList.emplace_back(i + 1, Cam.Name.empty() ? std::to_string(i) : Cam.Name);
            }
            ImGui::Combo("Camera", &m_CameraId, CamList.data(), static_cast<int>(CamList.size()));
        }

        if (m_CameraId == 0)
        {
            auto ModelRotation = m_Camera.GetSecondaryRotation();
            if (ImGui::gizmo3D("Model Rotation", ModelRotation, ImGui::GetTextLineHeight() * 10))
                m_Camera.SetSecondaryRotation(ModelRotation);
            ImGui::SameLine();
            ImGui::gizmo3D("Light direction", m_LightDirection, ImGui::GetTextLineHeight() * 10);

            if (ImGui::Button("Reset view"))
            {
                m_Camera.ResetDefaults();
            }

            auto CameraDist = m_Camera.GetDist();
            if (ImGui::SliderFloat("Camera distance", &CameraDist, m_Camera.GetMinDist(), m_Camera.GetMaxDist()))
                m_Camera.SetDist(CameraDist);
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        if (ImGui::TreeNode("Lighting"))
        {
            ImGui::ColorEdit3("Light Color", &m_LightColor.r);
            // clang-format off
            ImGui::SliderFloat("Light Intensity",    &m_LightIntensity,                  0.f, 50.f);
            ImGui::SliderFloat("Occlusion strength", &m_ShaderAttribs.OcclusionStrength, 0.f,  1.f);
            ImGui::SliderFloat("Emission scale",     &m_ShaderAttribs.EmissionScale,     0.f,  1.f);
            ImGui::SliderFloat("IBL scale",          &m_ShaderAttribs.IBLScale,          0.f,  1.f);
            // clang-format on
            ImGui::TreePop();
        }

        if (!m_Model->Animations.empty())
        {
            ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
            if (ImGui::TreeNode("Animation"))
            {
                ImGui::Checkbox("Play", &m_PlayAnimation);
                std::vector<const char*> Animations(m_Model->Animations.size());
                for (size_t i = 0; i < m_Model->Animations.size(); ++i)
                    Animations[i] = m_Model->Animations[i].Name.c_str();
                ImGui::Combo("Active Animation", reinterpret_cast<int*>(&m_AnimationIndex), Animations.data(), static_cast<int>(Animations.size()));
                ImGui::TreePop();
            }
        }

        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        if (ImGui::TreeNode("Tone mapping"))
        {
            // clang-format off
            ImGui::SliderFloat("Average log lum", &m_ShaderAttribs.AverageLogLum, 0.01f, 10.0f);
            ImGui::SliderFloat("Middle gray",     &m_ShaderAttribs.MiddleGray,    0.01f,  1.0f);
            ImGui::SliderFloat("White point",     &m_ShaderAttribs.WhitePoint,    0.1f,  20.0f);
            // clang-format on
            ImGui::TreePop();
        }

        {
            std::array<const char*, static_cast<size_t>(BackgroundMode::NumModes)> BackgroundModes;
            BackgroundModes[static_cast<size_t>(BackgroundMode::None)]              = "None";
            BackgroundModes[static_cast<size_t>(BackgroundMode::EnvironmentMap)]    = "Environmen Map";
            BackgroundModes[static_cast<size_t>(BackgroundMode::Irradiance)]        = "Irradiance";
            BackgroundModes[static_cast<size_t>(BackgroundMode::PrefilteredEnvMap)] = "PrefilteredEnvMap";
            ImGui::Combo("Background mode", reinterpret_cast<int*>(&m_BackgroundMode), BackgroundModes.data(), static_cast<int>(BackgroundModes.size()));
        }

        ImGui::SliderFloat("Env map mip", &m_EnvMapMipLevel, 0.0f, 7.0f);

        {
            std::pair<GLTF_PBR_Renderer::DebugViewType, const char*> DebugViews[] = {
                {GLTF_PBR_Renderer::DebugViewType::None, "None"},
                {GLTF_PBR_Renderer::DebugViewType::Texcoord0, "Tex coords 0"},
                {GLTF_PBR_Renderer::DebugViewType::Texcoord1, "Tex coords 1"},
                {GLTF_PBR_Renderer::DebugViewType::BaseColor, "Base Color"},
                {GLTF_PBR_Renderer::DebugViewType::Transparency, "Transparency"},
                {GLTF_PBR_Renderer::DebugViewType::NormalMap, "Normal Map"},
                {GLTF_PBR_Renderer::DebugViewType::Occlusion, "Occlusion"},
                {GLTF_PBR_Renderer::DebugViewType::Emissive, "Emissive"},
                {GLTF_PBR_Renderer::DebugViewType::Metallic, "Metallic"},
                {GLTF_PBR_Renderer::DebugViewType::Roughness, "Roughness"},
                {GLTF_PBR_Renderer::DebugViewType::DiffuseColor, "Diffuse color"},
                {GLTF_PBR_Renderer::DebugViewType::SpecularColor, "Specular color (R0)"},
                {GLTF_PBR_Renderer::DebugViewType::Reflectance90, "Reflectance90"},
                {GLTF_PBR_Renderer::DebugViewType::MeshNormal, "Mesh normal"},
                {GLTF_PBR_Renderer::DebugViewType::PerturbedNormal, "Perturbed normal"},
                {GLTF_PBR_Renderer::DebugViewType::NdotV, "n*v"},
                {PBR_Renderer::DebugViewType::DirectLighting, "Direct Lighting"},
                {GLTF_PBR_Renderer::DebugViewType::DiffuseIBL, "Diffuse IBL"},
                {GLTF_PBR_Renderer::DebugViewType::SpecularIBL, "Specular IBL"},
            };
            static_assert(_countof(DebugViews) == 19, "Did you add a new debug view mode? You may want to handle it here");

            ImGui::Combo("Debug view", &m_RenderParams.DebugView, DebugViews, _countof(DebugViews));
        }

        ImGui::Combo("Bound box mode", reinterpret_cast<int*>(&m_BoundBoxMode),
                     "None\0"
                     "Local\0"
                     "Global\0\0");

        if (m_bWireframeSupported)
        {
            ImGui::Checkbox("Wireframe", &m_RenderParams.Wireframe);
        }

        if (ImGui::TreeNode("Renderer Features"))
        {
            auto FeatureCheckbox = [&](const char* Name, GLTF_PBR_Renderer::PSO_FLAGS Flag) {
                bool Enabled = (m_RenderParams.Flags & Flag) != 0;
                if (ImGui::Checkbox(Name, &Enabled))
                {
                    if (Enabled)
                        m_RenderParams.Flags |= Flag;
                    else
                        m_RenderParams.Flags &= ~Flag;
                }
            };
            FeatureCheckbox("Vertex Colors", GLTF_PBR_Renderer::PSO_FLAG_USE_VERTEX_COLORS);
            FeatureCheckbox("Vertex Normals", GLTF_PBR_Renderer::PSO_FLAG_USE_VERTEX_NORMALS);
            FeatureCheckbox("Texcoords", GLTF_PBR_Renderer::PSO_FLAG_USE_TEXCOORD0 | GLTF_PBR_Renderer::PSO_FLAG_USE_TEXCOORD1);
            FeatureCheckbox("Joints", GLTF_PBR_Renderer::PSO_FLAG_USE_JOINTS);
            FeatureCheckbox("Color map", GLTF_PBR_Renderer::PSO_FLAG_USE_COLOR_MAP);
            FeatureCheckbox("Normal map", GLTF_PBR_Renderer::PSO_FLAG_USE_NORMAL_MAP);
            FeatureCheckbox("Phys desc map", GLTF_PBR_Renderer::PSO_FLAG_USE_PHYS_DESC_MAP);
            FeatureCheckbox("Occlusion", GLTF_PBR_Renderer::PSO_FLAG_USE_AO_MAP);
            FeatureCheckbox("Emissive", GLTF_PBR_Renderer::PSO_FLAG_USE_EMISSIVE_MAP);
            FeatureCheckbox("IBL", GLTF_PBR_Renderer::PSO_FLAG_USE_IBL);
            FeatureCheckbox("Tone Mapping", GLTF_PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Alpha Modes"))
        {
            auto AlphaModeCheckbox = [&](const char* Name, GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAGS Flag) {
                bool Enabled = (m_RenderParams.AlphaModes & Flag) != 0;
                if (ImGui::Checkbox(Name, &Enabled))
                {
                    if (Enabled)
                        m_RenderParams.AlphaModes |= Flag;
                    else
                        m_RenderParams.AlphaModes &= ~Flag;
                }
            };
            AlphaModeCheckbox("Opaque", GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAG_OPAQUE);
            AlphaModeCheckbox("Mask", GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAG_MASK);
            AlphaModeCheckbox("Blend", GLTF_PBR_Renderer::RenderInfo::ALPHA_MODE_FLAG_BLEND);
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void GLTFViewer::CreateBoundBoxPSO(IRenderStateNotationLoader* pRSNLoader)
{
    auto ModifyCI = MakeCallback([this](PipelineStateCreateInfo& PipelineCI) {
        auto& GraphicsPipelineCI{static_cast<GraphicsPipelineStateCreateInfo&>(PipelineCI)};
        GraphicsPipelineCI.GraphicsPipeline.RTVFormats[0]    = m_pSwapChain->GetDesc().ColorBufferFormat;
        GraphicsPipelineCI.GraphicsPipeline.DSVFormat        = m_pSwapChain->GetDesc().DepthBufferFormat;
        GraphicsPipelineCI.GraphicsPipeline.NumRenderTargets = 1;
    });
    pRSNLoader->LoadPipelineState({"BoundBox PSO", PIPELINE_TYPE_GRAPHICS, true, ModifyCI, ModifyCI}, &m_BoundBoxPSO);

    m_BoundBoxPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "cbCameraAttribs")->Set(m_FrameAttribsCB);
    m_BoundBoxPSO->CreateShaderResourceBinding(&m_BoundBoxSRB, true);
}

GLTFViewer::~GLTFViewer()
{
}

// Render a frame
void GLTFViewer::Render()
{
    auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_pSwapChain->GetDepthBufferDSV();
    // Clear the back buffer
    const float ClearColor[] = {0.032f, 0.032f, 0.032f, 1.0f};
    m_pImmediateContext->ClearRenderTarget(pRTV, ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_pImmediateContext->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    float YFov  = PI_F / 4.0f;
    float ZNear = 0.1f;
    float ZFar  = 100.f;

    float4x4 CameraView;
    if (m_CameraId == 0)
    {
        CameraView = m_Camera.GetRotation().ToMatrix() * float4x4::Translation(0.f, 0.0f, m_Camera.GetDist());

        m_RenderParams.ModelTransform = m_Camera.GetSecondaryRotation().ToMatrix();
    }
    else
    {
        const auto* pCameraNode           = m_CameraNodes[m_CameraId - 1];
        const auto* pCamera               = pCameraNode->pCamera;
        const auto& CameraGlobalTransform = m_Transforms.NodeGlobalMatrices[pCameraNode->Index];

        // GLTF camera is defined such that the local +X axis is to the right,
        // the lens looks towards the local -Z axis, and the top of the camera
        // is aligned with the local +Y axis.
        // https://github.com/KhronosGroup/glTF/tree/master/specification/2.0#cameras
        // We need to inverse the Z axis as our camera looks towards +Z.
        float4x4 InvZAxis = float4x4::Identity();
        InvZAxis._33      = -1;

        CameraView = CameraGlobalTransform.Inverse() * InvZAxis;
        YFov       = pCamera->Perspective.YFov;
        ZNear      = pCamera->Perspective.ZNear;
        ZFar       = pCamera->Perspective.ZFar;

        m_RenderParams.ModelTransform = float4x4::Identity();
    }

    // Apply pretransform matrix that rotates the scene according the surface orientation
    CameraView *= GetSurfacePretransformMatrix(float3{0, 0, 1});

    float4x4 CameraWorld = CameraView.Inverse();

    // Get projection matrix adjusted to the current screen orientation
    const auto CameraProj     = GetAdjustedProjectionMatrix(YFov, ZNear, ZFar);
    const auto CameraViewProj = CameraView * CameraProj;

    float3 CameraWorldPos = float3::MakeVector(CameraWorld[3]);

    {
        MapHelper<HLSL::PBRFrameAttribs> FrameAttribs(m_pImmediateContext, m_FrameAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD);
        {
            auto& Camera         = FrameAttribs->Camera;
            Camera.mProjT        = CameraProj.Transpose();
            Camera.mViewProjT    = CameraViewProj.Transpose();
            Camera.mViewProjInvT = CameraViewProj.Inverse().Transpose();
            Camera.f4Position    = float4(CameraWorldPos, 1);

            if (m_BoundBoxMode != BoundBoxMode::None)
            {
                float4x4 BBTransform;
                if (m_BoundBoxMode == BoundBoxMode::Local)
                {
                    BBTransform =
                        float4x4::Scale(m_ModelAABB.Max - m_ModelAABB.Min) *
                        float4x4::Translation(m_ModelAABB.Min) *
                        m_RenderParams.ModelTransform;
                }
                else if (m_BoundBoxMode == BoundBoxMode::Global)
                {
                    auto TransformedBB = m_ModelAABB.Transform(m_RenderParams.ModelTransform);
                    BBTransform        = float4x4::Scale(TransformedBB.Max - TransformedBB.Min) * float4x4::Translation(TransformedBB.Min);
                }
                else
                {
                    UNEXPECTED("Unexpected bound box mode");
                }

                for (int row = 0; row < 4; ++row)
                    Camera.f4ExtraData[row] = float4::MakeVector(BBTransform[row]);
            }
        }
        {
            auto& Light     = FrameAttribs->Light;
            Light.Direction = m_LightDirection;
            Light.Intensity = m_LightColor * m_LightIntensity;
        }
        {
            auto& Renderer = FrameAttribs->Renderer;
            m_GLTFRenderer->SetInternalShaderParameters(Renderer);

            Renderer.OcclusionStrength = m_ShaderAttribs.OcclusionStrength;
            Renderer.EmissionScale     = m_ShaderAttribs.EmissionScale;
            Renderer.AverageLogLum     = m_ShaderAttribs.AverageLogLum;
            Renderer.MiddleGray        = m_ShaderAttribs.MiddleGray;
            Renderer.WhitePoint        = m_ShaderAttribs.WhitePoint;
            Renderer.IBLScale          = m_ShaderAttribs.IBLScale;
            Renderer.HighlightColor    = m_ShaderAttribs.HighlightColor;
            Renderer.UnshadedColor     = m_ShaderAttribs.WireframeColor;
            Renderer.PointSize         = 1;
        }
    }

    if (m_pResourceMgr)
    {
        m_GLTFRenderer->Begin(m_pDevice, m_pImmediateContext, m_CacheUseInfo, m_CacheBindings, m_FrameAttribsCB);
        m_GLTFRenderer->Render(m_pImmediateContext, *m_Model, m_Transforms, m_RenderParams, nullptr, &m_CacheBindings);
    }
    else
    {
        m_GLTFRenderer->Begin(m_pImmediateContext);
        m_GLTFRenderer->Render(m_pImmediateContext, *m_Model, m_Transforms, m_RenderParams, &m_ModelResourceBindings);
    }

    if (m_BoundBoxMode != BoundBoxMode::None)
    {
        m_pImmediateContext->SetPipelineState(m_BoundBoxPSO);
        m_pImmediateContext->CommitShaderResources(m_BoundBoxSRB, RESOURCE_STATE_TRANSITION_MODE_VERIFY);
        DrawAttribs DrawAttrs{24, DRAW_FLAG_VERIFY_ALL};
        m_pImmediateContext->Draw(DrawAttrs);
    }

    if (m_BackgroundMode != BackgroundMode::None)
    {
        ITextureView* pEnvMapSRV = nullptr;
        switch (m_BackgroundMode)
        {
            case BackgroundMode::EnvironmentMap:
                pEnvMapSRV = m_EnvironmentMapSRV;
                break;

            case BackgroundMode::Irradiance:
                pEnvMapSRV = m_GLTFRenderer->GetIrradianceCubeSRV();
                break;

            case BackgroundMode::PrefilteredEnvMap:
                pEnvMapSRV = m_GLTFRenderer->GetPrefilteredEnvMapSRV();
                break;

            default:
                UNEXPECTED("Unexpected background mode");
        }

        HLSL::ToneMappingAttribs TMAttribs;
        TMAttribs.iToneMappingMode     = (m_RenderParams.Flags & GLTF_PBR_Renderer::PSO_FLAG_ENABLE_TONE_MAPPING) ? TONE_MAPPING_MODE_UNCHARTED2 : TONE_MAPPING_MODE_NONE;
        TMAttribs.bAutoExposure        = 0;
        TMAttribs.fMiddleGray          = m_ShaderAttribs.MiddleGray;
        TMAttribs.bLightAdaptation     = 0;
        TMAttribs.fWhitePoint          = m_ShaderAttribs.WhitePoint;
        TMAttribs.fLuminanceSaturation = 1.0;

        EnvMapRenderer::RenderAttribs EnvMapAttribs;
        EnvMapAttribs.pContext            = m_pImmediateContext;
        EnvMapAttribs.pEnvMap             = pEnvMapSRV;
        EnvMapAttribs.AverageLogLum       = m_ShaderAttribs.AverageLogLum;
        EnvMapAttribs.MipLevel            = m_EnvMapMipLevel;
        EnvMapAttribs.ConvertOutputToSRGB = (m_RenderParams.Flags & GLTF_PBR_Renderer::PSO_FLAG_CONVERT_OUTPUT_TO_SRGB) != 0;

        m_EnvMapRenderer->Render(EnvMapAttribs, TMAttribs);
    }
}

void GLTFViewer::Update(double CurrTime, double ElapsedTime)
{
    if (m_CameraId == 0)
    {
        m_Camera.Update(m_InputController);
    }

    SampleBase::Update(CurrTime, ElapsedTime);
    UpdateUI();

    if (!m_Model->Animations.empty() && m_PlayAnimation)
    {
        float& AnimationTimer = m_AnimationTimers[m_AnimationIndex];
        AnimationTimer += static_cast<float>(ElapsedTime);
        AnimationTimer = std::fmod(AnimationTimer, m_Model->Animations[m_AnimationIndex].End);
        m_Model->ComputeTransforms(m_RenderParams.SceneIndex, m_Transforms, m_ModelTransform, m_AnimationIndex, AnimationTimer);
    }
}

} // namespace Diligent
