static void
InitializeGuiRenderer(imgui_renderer& Gui, directx12& Dx)
{
    uint8_t* Pixels;
    int Width, Height;
    ImGui::GetIO().Fonts->AddFontFromFileTTF("data/Roboto-Medium.ttf", 18.0f);
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&Pixels, &Width, &Height);

    const auto TextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, (UINT64)Width, Height);
    VHR(Dx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
                                           &TextureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                           IID_PPV_ARGS(&Gui.FontTexture)));

    ID3D12Resource* IntermediateBuffer = nullptr;
    {
        uint64_t BufferSize;
        Dx.Device->GetCopyableFootprints(&TextureDesc, 0, 1, 0, nullptr, nullptr, nullptr, &BufferSize);

        const auto BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(BufferSize);
        VHR(Dx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
                                               &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&IntermediateBuffer)));

        Dx.IntermediateResources.push_back(IntermediateBuffer);
    }

    D3D12_SUBRESOURCE_DATA TextureData = { Pixels, (LONG_PTR)(Width * 4) };
    UpdateSubresources<1>(Dx.CmdList, Gui.FontTexture, IntermediateBuffer, 0, 0, 1, &TextureData);

    Dx.CmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Gui.FontTexture,
                                                                         D3D12_RESOURCE_STATE_COPY_DEST,
                                                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
    D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc = {};
    SrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    SrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    SrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    SrvDesc.Texture2D.MipLevels = 1;

    AllocateDescriptors(Dx, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, Gui.FontTextureDescriptor);
    Dx.Device->CreateShaderResourceView(Gui.FontTexture, &SrvDesc, Gui.FontTextureDescriptor);


    D3D12_INPUT_ELEMENT_DESC InputElements[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    std::vector<uint8_t> CsoVs = LoadFile("data/shaders/imgui-vs.cso");
    std::vector<uint8_t> CsoPs = LoadFile("data/shaders/imgui-ps.cso");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC PsoDesc = {};
    PsoDesc.InputLayout = { InputElements, (unsigned)std::size(InputElements) };
    PsoDesc.VS = { CsoVs.data(), CsoVs.size() };
    PsoDesc.PS = { CsoPs.data(), CsoPs.size() };
    PsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    PsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    PsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    PsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    PsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    PsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    PsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    PsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    PsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    PsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    PsoDesc.SampleMask = UINT_MAX;
    PsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    PsoDesc.NumRenderTargets = 1;
    PsoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    PsoDesc.SampleDesc.Count = 1;

    VHR(Dx.Device->CreateGraphicsPipelineState(&PsoDesc, IID_PPV_ARGS(&Gui.PipelineState)));
    VHR(Dx.Device->CreateRootSignature(0, CsoVs.data(), CsoVs.size(), IID_PPV_ARGS(&Gui.RootSignature)));
}

static void
RenderGui(imgui_renderer& Gui, directx12& Dx)
{
    ImDrawData* DrawData = ImGui::GetDrawData();
    if (!DrawData || DrawData->TotalVtxCount == 0)
        return;

    ImGuiIO& Io = ImGui::GetIO();
    imgui_frame_resources& Frame = Gui.FrameResources[Dx.FrameIndex];

    const int ViewportWidth = (int)(Io.DisplaySize.x * Io.DisplayFramebufferScale.x);
    const int ViewportHeight = (int)(Io.DisplaySize.y * Io.DisplayFramebufferScale.y);
    DrawData->ScaleClipRects(Io.DisplayFramebufferScale);

    // create/resize vertex buffer
    if (Frame.VertexBufferSize == 0 || Frame.VertexBufferSize < DrawData->TotalVtxCount * sizeof(ImDrawVert))
    {
        SAFE_RELEASE(Frame.VertexBuffer);
        VHR(Dx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
                                               &CD3DX12_RESOURCE_DESC::Buffer(DrawData->TotalVtxCount * sizeof(ImDrawVert)),
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&Frame.VertexBuffer)));

        VHR(Frame.VertexBuffer->Map(0, &CD3DX12_RANGE(0, 0), &Frame.VertexBufferCpuAddress));

        Frame.VertexBufferSize = DrawData->TotalVtxCount * sizeof(ImDrawVert);

        Frame.VertexBufferView.BufferLocation = Frame.VertexBuffer->GetGPUVirtualAddress();
        Frame.VertexBufferView.StrideInBytes = sizeof(ImDrawVert);
        Frame.VertexBufferView.SizeInBytes = DrawData->TotalVtxCount * sizeof(ImDrawVert);
    }
    // create/resize index buffer
    if (Frame.IndexBufferSize == 0 || Frame.IndexBufferSize < DrawData->TotalIdxCount * sizeof(ImDrawIdx))
    {
        SAFE_RELEASE(Frame.IndexBuffer);
        VHR(Dx.Device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
                                               &CD3DX12_RESOURCE_DESC::Buffer(DrawData->TotalIdxCount * sizeof(ImDrawIdx)),
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&Frame.IndexBuffer)));

        VHR(Frame.IndexBuffer->Map(0, &CD3DX12_RANGE(0, 0), &Frame.IndexBufferCpuAddress));

        Frame.IndexBufferSize = DrawData->TotalIdxCount * sizeof(ImDrawIdx);

        Frame.IndexBufferView.BufferLocation = Frame.IndexBuffer->GetGPUVirtualAddress();
        Frame.IndexBufferView.Format = sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        Frame.IndexBufferView.SizeInBytes = DrawData->TotalIdxCount * sizeof(ImDrawIdx);
    }
    // update vertex and index buffers
    {
        ImDrawVert* VertexPtr = (ImDrawVert*)Frame.VertexBufferCpuAddress;
        ImDrawIdx* IndexPtr = (ImDrawIdx*)Frame.IndexBufferCpuAddress;

        for (unsigned N = 0; N < (unsigned)DrawData->CmdListsCount; ++N)
        {
            ImDrawList* DrawList = DrawData->CmdLists[N];
            memcpy(VertexPtr, &DrawList->VtxBuffer[0], DrawList->VtxBuffer.size() * sizeof(ImDrawVert));
            memcpy(IndexPtr, &DrawList->IdxBuffer[0], DrawList->IdxBuffer.size() * sizeof(ImDrawIdx));
            VertexPtr += DrawList->VtxBuffer.size();
            IndexPtr += DrawList->IdxBuffer.size();
        }
    }

    D3D12_GPU_VIRTUAL_ADDRESS ConstantBufferGpuAddress;
    void* ConstantBufferCpuAddress = AllocateGpuUploadMemory(Dx, 64, ConstantBufferGpuAddress);

    // update constant buffer
    {
        XMMATRIX M = XMMatrixTranspose(XMMatrixOrthographicOffCenterLH(0.0f, (float)ViewportWidth,
                                                                       (float)ViewportHeight, 0.0f,
                                                                       0.0f, 1.0f));
        XMFLOAT4X4A F;
        XMStoreFloat4x4A(&F, M);
        memcpy(ConstantBufferCpuAddress, &F, sizeof(F));
    }

    D3D12_VIEWPORT Viewport = { 0.0f, 0.0f, (float)ViewportWidth, (float)ViewportHeight, 0.0f, 1.0f };
    Dx.CmdList->RSSetViewports(1, &Viewport);

    Dx.CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    Dx.CmdList->SetPipelineState(Gui.PipelineState);

    Dx.CmdList->SetGraphicsRootSignature(Gui.RootSignature);
    Dx.CmdList->SetGraphicsRootConstantBufferView(0, ConstantBufferGpuAddress);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
        AllocateGpuDescriptors(Dx, 1, CpuHandle, GpuHandle);
        Dx.Device->CopyDescriptorsSimple(1, CpuHandle, Gui.FontTextureDescriptor, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        Dx.CmdList->SetGraphicsRootDescriptorTable(1, GpuHandle);
    }

    Dx.CmdList->IASetVertexBuffers(0, 1, &Frame.VertexBufferView);
    Dx.CmdList->IASetIndexBuffer(&Frame.IndexBufferView);


    int VertexOffset = 0;
    unsigned IndexOffset = 0;
    for (unsigned N = 0; N < (unsigned)DrawData->CmdListsCount; ++N)
    {
        ImDrawList* DrawList = DrawData->CmdLists[N];

        for (unsigned CmdIndex = 0; CmdIndex < (uint32_t)DrawList->CmdBuffer.size(); ++CmdIndex)
        {
            ImDrawCmd* Cmd = &DrawList->CmdBuffer[CmdIndex];

            if (Cmd->UserCallback)
            {
                Cmd->UserCallback(DrawList, Cmd);
            }
            else
            {
                D3D12_RECT R = { (LONG)Cmd->ClipRect.x, (LONG)Cmd->ClipRect.y, (LONG)Cmd->ClipRect.z, (LONG)Cmd->ClipRect.w };
                Dx.CmdList->RSSetScissorRects(1, &R);
                Dx.CmdList->DrawIndexedInstanced(Cmd->ElemCount, 1, IndexOffset, VertexOffset, 0);
            }
            IndexOffset += Cmd->ElemCount;
        }
        VertexOffset += DrawList->VtxBuffer.size();
    }
}
// vim: set ts=4 sw=4 expandtab:
