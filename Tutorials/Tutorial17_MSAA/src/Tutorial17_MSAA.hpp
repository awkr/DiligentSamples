/*
 *  Copyright 2019-2022 Diligent Graphics LLC
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

#pragma once

#include "SampleBase.hpp"
#include "BasicMath.hpp"

namespace Diligent
{

class Tutorial17_MSAA final : public SampleBase
{
public:
    virtual void Initialize(const SampleInitInfo& InitInfo) override final;

    virtual void Render() override final;
    virtual void Update(double CurrTime, double ElapsedTime) override final;

    virtual const Char* GetSampleName() const override final { return "Tutorial17: MSAA"; }

    virtual void WindowResize(Uint32 Width, Uint32 Height) override final;

private:
    void CreateCubePSO();
    void UpdateUI();
    void CreateMSAARenderTarget();

    static constexpr TEXTURE_FORMAT DepthBufferFormat = TEX_FORMAT_D32_FLOAT;

    // Cube resources
    RefCntAutoPtr<IPipelineState>         m_pCubePSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pCubeSRB;
    RefCntAutoPtr<IBuffer>                m_CubeVertexBuffer;
    RefCntAutoPtr<IBuffer>                m_CubeIndexBuffer;
    RefCntAutoPtr<IBuffer>                m_CubeVSConstants;
    RefCntAutoPtr<ITextureView>           m_CubeTextureSRV;

    // Offscreen multi-sampled render target and depth-stencil
    RefCntAutoPtr<ITextureView> m_pMSColorRTV;
    RefCntAutoPtr<ITextureView> m_pMSDepthDSV;

    Uint8  m_SampleCount           = 4;
    Uint32 m_SupportedSampleCounts = 0;

    float4x4 m_WorldViewProjMatrix;
    float    m_fCurrentTime = 0.f;
    bool     m_bRotateGrid  = true;
};

} // namespace Diligent
