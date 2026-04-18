__declspec(dllexport) int DLSS_Evaluate(
    void* nvngxInstance,
    void* cmdList,
    void* params) {
    if (!g_initialized || !cmdList || !params) return NVNGX_FAIL;

    NGXParameter* p = static_cast<NGXParameter*>(params);
    ID3D12GraphicsCommandList* cl = static_cast<ID3D12GraphicsCommandList*>(cmdList);

    // Extract D3D12 resources from parameter keys
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* motion = nullptr;
    ID3D12Resource* output = nullptr;
    float sharpness = 0.80f;
    uint32_t renderW = 0, renderH = 0;

    for (const char* key : {"Color", "DLSS.Input.Color", "Color.Input"}) {
        if (p->GetValueD3D12Res(key, &color) == 0 && color) break;
    }
    for (const char* key : {"Depth", "DLSS.Input.Depth", "Depth.Input"}) {
        if (p->GetValueD3D12Res(key, &depth) == 0 && depth) break;
    }
    for (const char* key : {"MotionVectors", "DLSS.Input.MotionVectors", "MV.Input"}) {
        if (p->GetValueD3D12Res(key, &motion) == 0 && motion) break;
    }
    for (const char* key : {"Output", "DLSS.Output", "Output.Backbuffer"}) {
        if (p->GetValueD3D12Res(key, &output) == 0 && output) break;
    }
    p->GetValueF32("Sharpness", &sharpness);
    p->GetValueUI32("Render.Subrect.Width", &renderW);
    p->GetValueUI32("Render.Subrect.Height", &renderH);

    SharedState* ss = GetSharedState();
    if (ss) {
        SharedStateLock lock(&ss->lock);
        if (renderW == 0) renderW = ss->render_width;
        if (renderH == 0) renderH = ss->render_height;
        sharpness = ss->sharpness;
    }

    AD_LOG_I("DLSS_Evaluate: color=%p depth=%p motion=%p output=%p render=%ux%u",
             color, depth, motion, output, renderW, renderH);

    Config& cfg = GetConfig();

    // ── Path A with SGSR2 (temporal upscaling via official algorithm) ──
    if (cfg.sgsr_mode == SGSRMode::SGSR2 && g_sgsr2.IsInitialized() && output && color) {
        SGSR2Params sgsr2Params{};
        sgsr2Params.color = color;
        sgsr2Params.depth = depth;
        sgsr2Params.motion = motion;
        sgsrParams.output = output;
        sgsr2Params.sharpness = sharpness;
        sgsr2Params.renderWidth = renderW;
        sgsr2Params.renderHeight = renderH;
        sgsr2Params.displayWidth = ss ? ss->display_width : renderW;
        sgsr2Params.displayHeight = ss ? ss->display_height : renderH;
        sgsr2Params.resetHistory = !g_historyValid;
        sgsr2Params.preExposure = 1.0f;
        sgsr2Params.minLerpContribution = 0.15f;
        sgsr2Params.bSameCamera = true;
        sgsr2Params.cameraFovAngleHor = 1.0472f;
        sgsr2Params.cameraNear = 0.01f;
        sgsr2Params.cameraFar = 1000.0f;

        g_sgsr2.Execute(cl, sgsr2Params);
        AD_LOG_I("DLSS_Evaluate: SGSR2 official temporal upscaling executed");
        return NVNGX_SUCCESS;
    }

    // ── Path A with SGSR1 (spatial upscaling via official algorithm) ──
    if (cfg.sgsr_mode == SGSRMode::SGSR1 && g_sgsr.IsInitialized() && output && color) {
        SGSRParams sgsrParams{};
        sgsrParams.color = color;
        sgsrParams.depth = depth;
        sgsrParams.motion = motion;
        sgsrParams.output = output;
        sgsrParams.sharpness = sharpness;
        sgsrParams.renderWidth = renderW;
        sgsrParams.renderHeight = renderH;
        sgsrParams.displayWidth = ss ? ss->display_width : renderW;
        sgsrParams.displayHeight = ss ? ss->display_height : renderH;

        g_sgsr.Execute(cl, sgsrParams);
        AD_LOG_I("DLSS_Evaluate: SGSR1 official spatial upscaling executed");
        return NVNGHX_SUCCESS;
    }

    return NVNGX_SUCCESS;
}
