// Copyright (c) 2017-2021 Intel Corporation
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.


#include "mfx_c2_vpp_wrapp.h"
#include "mfx_c2_defs.h"
#include "mfx_c2_utils.h"
#include "mfx_msdk_debug.h"
#include "mfx_c2_frame_out.h"
#include "mfx_va_allocator.h"
#include <C2AllocatorGralloc.h>
#include "mfx_va_frame_pool_allocator.h"

#undef MFX_DEBUG_MODULE_NAME
#define MFX_DEBUG_MODULE_NAME "mfx_c2_vpp_wrapp"

MfxC2VppWrapp::MfxC2VppWrapp(void):
    m_pVpp(NULL),
#ifdef USE_ONEVPL
    m_mfxSession(NULL),
#else
    m_pSession(NULL),
#endif
    m_videoMemory(true)
{
    MFX_DEBUG_TRACE_FUNC;
    MFX_ZERO_MEMORY(m_vppParam);
    MFX_ZERO_MEMORY(m_allocator);
}

MfxC2VppWrapp::~MfxC2VppWrapp(void)
{
    MFX_DEBUG_TRACE_FUNC;
    Close();
}

mfxStatus MfxC2VppWrapp::Init(MfxC2VppWrappParam *param, MfxC2Conversion conversion)
{
    MFX_DEBUG_TRACE_FUNC;
    mfxStatus sts = MFX_ERR_NONE;

    if (!param || !param->session || !param->allocator || !param->frame_info)
        sts = MFX_ERR_NULL_PTR;

    if (MFX_ERR_NONE == sts)
    {
        m_allocator = param->allocator;
        m_converter = param->converter;
#ifdef USE_ONEVPL
        m_mfxSession = param->session;
        MFX_NEW(m_pVpp, MFXVideoVPP(m_mfxSession));
#else
        m_pSession = param->session;
        MFX_NEW(m_pVpp, MFXVideoVPP(*m_pSession));
#endif

        if(!m_pVpp) sts = MFX_ERR_UNKNOWN;

        m_conversion = conversion;
        m_videoMemory = param->videoMemory;

        if (MFX_ERR_NONE == sts) sts = FillVppParams(param->frame_info);
        MFX_DEBUG_TRACE__mfxFrameInfo(m_vppParam.vpp.In);
        MFX_DEBUG_TRACE__mfxFrameInfo(m_vppParam.vpp.Out);

        if (MFX_ERR_NONE == sts) sts = m_pVpp->Init(&m_vppParam);
    }

    // if (MFX_ERR_NONE == sts) sts = AllocateOneSurface();

    if (MFX_ERR_NONE != sts) Close();

    MFX_DEBUG_TRACE_I32(sts);
    return sts;
}

mfxStatus MfxC2VppWrapp::Close(void)
{
    MFX_DEBUG_TRACE_FUNC;
    mfxStatus sts = MFX_ERR_NONE;

    if (m_pVpp)
    {
        sts = m_pVpp->Close();
        MFX_DELETE(m_pVpp);
        m_pVpp = NULL;
    }

    for (auto surface: m_vppSrf)
    {
        mfxFrameAllocResponse response;
        response.NumFrameActual = 1;
        response.mids[0] = surface->Data.MemId;
        m_allocator->FreeFrames(&response);
    }

#ifdef USE_ONEVPL
    m_mfxSession = NULL;
#else
    m_pSession = NULL;
#endif
    m_allocator.reset();
    m_vppSrf.clear();

    MFX_DEBUG_TRACE_I32(sts);
    return sts;
}

mfxStatus MfxC2VppWrapp::FillVppParams(mfxFrameInfo *frame_info)
{
    MFX_DEBUG_TRACE_FUNC;
    mfxStatus sts = MFX_ERR_NONE;

    if (!frame_info) sts = MFX_ERR_NULL_PTR;

    if (MFX_ERR_NONE == sts)
    {
        MFX_ZERO_MEMORY(m_vppParam);
        m_vppParam.AsyncDepth = 1;
        m_vppParam.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;
        m_vppParam.vpp.In = *frame_info;
        m_vppParam.vpp.Out = *frame_info;

        switch (m_conversion)
        {
            case ARGB_TO_NV12:
                if (MFX_FOURCC_RGB4 != m_vppParam.vpp.In.FourCC)
                    sts = MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;

                m_vppParam.vpp.Out.FourCC = MFX_FOURCC_NV12;
                break;

            case NV12_TO_ARGB:
                if (MFX_FOURCC_NV12 != m_vppParam.vpp.In.FourCC)
                    sts = MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;

                m_vppParam.vpp.Out.FourCC = MFX_FOURCC_RGB4;
                break;

            case P010_TO_NV12:
                if (MFX_FOURCC_P010 != m_vppParam.vpp.In.FourCC)
                    sts = MFX_ERR_INCOMPATIBLE_VIDEO_PARAM;

                m_vppParam.vpp.Out.FourCC = MFX_FOURCC_NV12;
                break;

            case CONVERT_NONE:
                break;
        }

        if (MFX_ERR_NONE != sts)
            MFX_ZERO_MEMORY(m_vppParam);
    }

    MFX_DEBUG_TRACE_I32(sts);
    return sts;
}

mfxStatus MfxC2VppWrapp::AllocateOneSurface(void)
{
    MFX_DEBUG_TRACE_FUNC;
    mfxStatus sts = MFX_ERR_NONE;

    if (m_vppSrf.size() >= VPP_MAX_SRF_NUM) sts = MFX_ERR_UNKNOWN;
    if (MFX_ERR_NONE == sts)
    {
        if (1)
        {
            mfxFrameAllocRequest request;
            MFX_ZERO_MEMORY(request);
            request.Info = m_vppParam.vpp.Out;
            request.NumFrameMin = 1;
            request.NumFrameSuggested = 1;
            request.Type = MFX_MEMTYPE_VIDEO_MEMORY_PROCESSOR_TARGET | MFX_MEMTYPE_FROM_VPPOUT | MFX_MEMTYPE_EXTERNAL_FRAME;

            mfxFrameAllocResponse response;
            mfxFrameSurface1 surface;
            sts = m_allocator->AllocFrames(&request, &response);

            if (MFX_ERR_NONE == sts)
            {
                surface.Info = m_vppParam.vpp.Out;
                surface.Data.MemId = response.mids[0];
                m_vppSrf.push_back(std::make_shared<mfxFrameSurface1>(surface));
            }
        } else {

            /*
            c2_status_t res = C2_OK;
            std::shared_ptr<C2GraphicBlock> out_block;
            C2MemoryUsage mem_usage = {(GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_HW_COMPOSER), android::C2AndroidMemoryUsage::HW_CODEC_WRITE};
            uint32_t retry = 0;
            do { // fake loop
                do {
                    res = m_c2Allocator->fetchGraphicBlock(MFXGetSurfaceWidth(m_vppParam.vpp.In, false),
                        MFXGetSurfaceHeight(m_vppParam.vpp.In, false), 
                        MfxFourCCToGralloc(m_vppParam.vpp.Out.FourCC, false), mem_usage, &out_block);
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } while (res == C2_BLOCKING && retry++ < 10);

                if (C2_OK != res) {
                    MFX_DEBUG_TRACE_STREAM("fetchGraphicBlock failed, res = " << res);
                    break;
                }
                MfxC2FrameOut frame_out;
                res = MfxC2FrameOut::Create(std::move(out_block), m_vppParam.vpp.Out, MFX_SECOND_NS, &frame_out);

                if (C2_OK != res) {
                    MFX_DEBUG_TRACE_STREAM("MfxC2FrameOut::Create failed, res = " << res);
                    break;
                }
                auto surface = frame_out.GetMfxFrameSurface();

                native_handle_t *hndl = android::UnwrapNativeCodec2GrallocHandle(out_block->handle());
                mfxMemId mId;
                    
                if (nullptr == surface || nullptr == m_converter) {
                    sts = MFX_ERR_NULL_PTR;
                    break;
                }

                m_converter->ConvertGrallocToVa(hndl, true, &mId);
                surface->Data.MemId = &mId;
                m_vppSrf.push_back(surface);

            } while(false);
            */
        }
    }

    MFX_DEBUG_TRACE_I32(sts);
    return sts;
}

mfxFrameSurface1* MfxC2VppWrapp::GetFreeSurface(void)
{
    MFX_DEBUG_TRACE_FUNC;
    mfxFrameSurface1 *outSurface = nullptr;
    for (auto surface: m_vppSrf)
    {
        if (false == surface->Data.Locked)
        {
            outSurface = surface.get();
            break;
        }
    }

    return outSurface;
}

mfxStatus MfxC2VppWrapp::ProcessFrameVpp(mfxFrameSurface1 *in_srf, mfxFrameSurface1 **out_srf)
{
    MFX_DEBUG_TRACE_FUNC;
    mfxStatus sts = MFX_ERR_NONE;
    mfxFrameSurface1* outSurface = nullptr;
    mfxSyncPoint syncp;

    if (!in_srf || !out_srf) return MFX_ERR_UNKNOWN;

    outSurface = GetFreeSurface();

    if (m_vppSrf.size() < VPP_MAX_SRF_NUM && nullptr == outSurface)
    {
        sts = AllocateOneSurface();
        if (MFX_ERR_NONE == sts) outSurface = GetFreeSurface(); // just created outSurface
    }

    if (outSurface)
    {
        sts = m_pVpp->RunFrameVPPAsync(in_srf, outSurface, nullptr, &syncp);
        MFX_DEBUG_TRACE_STREAM("RunFrameVPPAsync sts = " << sts);
        if (MFX_ERR_NONE == sts)
#ifdef USE_ONEVPL
            sts = MFXVideoCORE_SyncOperation(m_mfxSession, syncp, MFX_TIMEOUT_INFINITE);
#else
            sts = m_pSession->SyncOperation(syncp, MFX_TIMEOUT_INFINITE);
#endif
    }
    else sts = MFX_ERR_MORE_SURFACE;

    if (MFX_ERR_NONE == sts)
    {
        *out_srf = outSurface;
    }

    MFX_DEBUG_TRACE_I32(sts);
    return sts;
}