#include "SlangImGuiRenderer.h"

#include <imgui.h>
#include <slang.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cfloat>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

constexpr char kImGuiShaderSource[] = R"(

struct ImGuiVSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct ImGuiVSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

cbuffer ImGuiConstants : register(b0)
{
    float4x4 u_projection;
};

[shader("vertex")]
ImGuiVSOutput vertexMain(ImGuiVSInput input)
{
    ImGuiVSOutput output;
    float4 localPosition = float4(input.position, 0.0, 1.0);
    output.position = mul(localPosition, u_projection);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

Texture2D u_fontTexture : register(t0);
SamplerState u_fontSampler : register(s0);

[shader("fragment")]
float4 fragmentMain(ImGuiVSOutput input) : SV_Target
{
    float4 fontSample = u_fontTexture.Sample(u_fontSampler, input.uv);
    return input.color * fontSample;
}

)";

static const char* kImGuiEntryPoints[] = {"vertexMain", "fragmentMain"};

void printDiagnostics(const char* label, slang::IBlob* diagnostics)
{
    if (!diagnostics || diagnostics->getBufferSize() == 0)
    {
        return;
    }

    const std::string message(
        static_cast<const char*>(diagnostics->getBufferPointer()),
        diagnostics->getBufferSize());

    std::cout << "[SlangImGuiRenderer] " << label << "\n" << message << std::endl;
}

struct ShaderCursorHelper
{
    gfx::IShaderObject* baseObject = nullptr;
    slang::TypeLayoutReflection* typeLayout = nullptr;
    gfx::ShaderOffset offset = {};

    ShaderCursorHelper() = default;

    explicit ShaderCursorHelper(gfx::IShaderObject* object)
        : baseObject(object)
        , typeLayout(object ? object->getElementTypeLayout() : nullptr)
    {
    }

    bool isValid() const { return baseObject != nullptr && typeLayout != nullptr; }

    ShaderCursorHelper getField(const char* name) const
    {
        ShaderCursorHelper result;
        if (!isValid() || !name)
        {
            return result;
        }

        switch (typeLayout->getKind())
        {
        case slang::TypeReflection::Kind::Struct:
        {
            SlangInt fieldIndex = typeLayout->findFieldIndexByName(name, nullptr);
            if (fieldIndex < 0)
            {
                break;
            }

            slang::VariableLayoutReflection* fieldLayout =
                typeLayout->getFieldByIndex(static_cast<unsigned int>(fieldIndex));
            if (!fieldLayout)
            {
                break;
            }

            result.baseObject = baseObject;
            result.typeLayout = fieldLayout->getTypeLayout();
            result.offset = offset;
            result.offset.uniformOffset += fieldLayout->getOffset();
            result.offset.bindingRangeIndex += static_cast<gfx::GfxIndex>(
                typeLayout->getFieldBindingRangeOffset(fieldIndex));
            result.offset.bindingArrayIndex = offset.bindingArrayIndex;
            return result;
        }
        case slang::TypeReflection::Kind::ConstantBuffer:
        case slang::TypeReflection::Kind::ParameterBlock:
        {
            Slang::ComPtr<gfx::IShaderObject> subObject;
            if (SLANG_SUCCEEDED(baseObject->getObject(offset, subObject.writeRef())) && subObject)
            {
                ShaderCursorHelper subCursor(subObject);
                ShaderCursorHelper found = subCursor.getField(name);
                if (found.isValid())
                {
                    return found;
                }
            }
            break;
        }
        default:
            break;
        }

        const gfx::GfxIndex entryPointCount =
            baseObject ? static_cast<gfx::GfxIndex>(baseObject->getEntryPointCount()) : 0;
        for (gfx::GfxIndex entryIndex = 0; entryIndex < entryPointCount; ++entryIndex)
        {
            Slang::ComPtr<gfx::IShaderObject> entryPoint;
            if (SLANG_SUCCEEDED(baseObject->getEntryPoint(entryIndex, entryPoint.writeRef())) &&
                entryPoint)
            {
                ShaderCursorHelper entryCursor(entryPoint);
                ShaderCursorHelper found = entryCursor.getField(name);
                if (found.isValid())
                {
                    return found;
                }
            }
        }

        return result;
    }

    SlangResult setData(const void* data, size_t sizeInBytes) const
    {
        if (!isValid())
        {
            return SLANG_E_INVALID_ARG;
        }
        return baseObject->setData(offset, data, sizeInBytes);
    }

    SlangResult setResource(gfx::IResourceView* resource) const
    {
        if (!isValid())
        {
            return SLANG_E_INVALID_ARG;
        }
        return baseObject->setResource(offset, resource);
    }

    SlangResult setSampler(gfx::ISamplerState* sampler) const
    {
        if (!isValid())
        {
            return SLANG_E_INVALID_ARG;
        }
        return baseObject->setSampler(offset, sampler);
    }

};

void logShaderObjectLayout(gfx::IShaderObject* rootObject)
{
    if (!rootObject)
    {
        return;
    }

    slang::TypeLayoutReflection* layout = rootObject->getElementTypeLayout();
    if (!layout)
    {
        std::cout << "[SlangImGuiRenderer] Root shader object has no layout" << std::endl;
        return;
    }

    std::cout << "[SlangImGuiRenderer] Root layout kind=" << static_cast<int>(layout->getKind())
              << ", fields=" << layout->getFieldCount()
              << ", bindingRanges=" << layout->getBindingRangeCount() << std::endl;

    const SlangInt fieldCount = layout->getFieldCount();
    for (SlangInt i = 0; i < fieldCount; ++i)
    {
        slang::VariableLayoutReflection* fieldLayout = layout->getFieldByIndex(static_cast<unsigned int>(i));
        if (!fieldLayout)
        {
            continue;
        }

        slang::TypeLayoutReflection* fieldTypeLayout = fieldLayout->getTypeLayout();
        const char* name = fieldLayout->getName();
        const slang::ParameterCategory category = fieldLayout->getCategory();
        const size_t uniformOffset = fieldLayout->getOffset(category);
        const SlangInt bindingRangeOffset = layout->getFieldBindingRangeOffset(i);
        const unsigned bindingIndex = fieldLayout->getBindingIndex();
        size_t bindingSpace = 0;
        if (category != slang::ParameterCategory::None)
        {
            bindingSpace = static_cast<size_t>(fieldLayout->getBindingSpace(category));
        }

    std::cout << "    Field[" << i << "] name='" << (name ? name : "<unnamed>")
          << "' kind=" << (fieldTypeLayout ? static_cast<int>(fieldTypeLayout->getKind()) : -1)
                  << " category=" << static_cast<int>(category)
                  << " uniformOffset=" << uniformOffset
                  << " bindingRangeOffset=" << bindingRangeOffset
                  << " bindingIndex=" << bindingIndex
                  << " bindingSpace=" << bindingSpace
                  << std::endl;
    }

    const SlangInt bindingRangeCount = layout->getBindingRangeCount();
    for (SlangInt i = 0; i < bindingRangeCount; ++i)
    {
        const slang::BindingType type = layout->getBindingRangeType(i);
        const SlangInt count = layout->getBindingRangeBindingCount(i);
        std::cout << "    BindingRange[" << i << "] type=" << static_cast<int>(type)
                  << " count=" << count
                  << std::endl;
    }
}

} // namespace

SlangImGuiRenderer::SlangImGuiRenderer() = default;

SlangImGuiRenderer::~SlangImGuiRenderer() {
    shutdown();
}

bool SlangImGuiRenderer::initialize(const InitializeDesc& desc) {
    shutdown();

    if (!desc.device) {
        std::cout << "[SlangImGuiRenderer] initialize() missing gfx device; returning false" << std::endl;
        return false;
    }

    if (!desc.framebufferLayout) {
        std::cout << "[SlangImGuiRenderer] initialize() missing framebuffer layout; returning false" << std::endl;
        return false;
    }

    m_device = desc.device;
    m_framebufferLayout = desc.framebufferLayout;
    m_renderPassLayout = desc.renderPassLayout;

    if (!createShaderProgram()) {
        std::cout << "[SlangImGuiRenderer] Failed to create shader program; renderer inactive" << std::endl;
        shutdown();
        return false;
    }

    if (!createInputLayout()) {
        std::cout << "[SlangImGuiRenderer] Failed to create input layout; renderer inactive" << std::endl;
        shutdown();
        return false;
    }

    if (!createPipelineState()) {
        std::cout << "[SlangImGuiRenderer] Failed to create pipeline state; renderer inactive" << std::endl;
        shutdown();
        return false;
    }

    if (!initializeConstantBuffer()) {
        std::cout << "[SlangImGuiRenderer] Failed to create constant buffer; renderer inactive" << std::endl;
        shutdown();
        return false;
    }

    std::cout << "[SlangImGuiRenderer] Stage 3 buffer scaffolding ready; upload path active while rendering remains stubbed" << std::endl;

    if (!createFontResources()) {
        std::cout << "[SlangImGuiRenderer] Failed to upload ImGui font atlas; renderer inactive" << std::endl;
        shutdown();
        return false;
    }

    std::cout << "[SlangImGuiRenderer] Stage 4 font atlas uploaded; sampler ready while draw encoding remains pending" << std::endl;

    m_initialized = true;
    m_renderWarned = false;
    m_loggedDrawStats = false;
    m_loggedBindingLayout = false;
    m_loggedBindingResults = false;
    m_loggedVertexBounds = false;
    m_loggedVertexSamples = false;
    m_loggedCommandDetails = false;
    m_loggedProjectionSample = false;
    m_loggedLayoutInfo = false;
    m_loggedSourceIndexSamples = false;
    m_loggedProjectionMatrix = false;
    m_loggedProjectionLayout = false;
    return true;
}

void SlangImGuiRenderer::shutdown() {
    if (ImGui::GetCurrentContext()) {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts && io.Fonts->TexID == reinterpret_cast<ImTextureID>(m_fontTextureView.get())) {
            io.Fonts->SetTexID(nullptr);
        }
    }

    m_shaderProgram = nullptr;
    m_pipelineState = nullptr;
    m_inputLayout = nullptr;

    m_vertexBuffer = nullptr;
    m_indexBuffer = nullptr;
    m_constantBuffer = nullptr;

    m_fontTexture = nullptr;
    m_fontTextureView = nullptr;
    m_fontSampler = nullptr;

    m_device = nullptr;
    m_framebufferLayout = nullptr;
    m_renderPassLayout = nullptr;

    m_vertexBufferSize = 0;
    m_indexBufferSize = 0;
    m_constantBufferSize = 0;

    m_initialized = false;
    m_renderWarned = false;
    m_loggedDrawStats = false;
    m_loggedBindingLayout = false;
    m_loggedBindingResults = false;
    m_loggedVertexBounds = false;
    m_loggedVertexSamples = false;
    m_loggedCommandDetails = false;
    m_loggedProjectionSample = false;
    m_loggedLayoutInfo = false;
    m_loggedSourceIndexSamples = false;
    m_loggedProjectionMatrix = false;
    m_loggedProjectionLayout = false;
}

void SlangImGuiRenderer::render(const ImDrawData* drawData, gfx::IRenderCommandEncoder* renderEncoder) {
    if (!m_device || !m_initialized || !m_pipelineState) {
        if (!m_renderWarned) {
            std::cout << "[SlangImGuiRenderer] render() skipped (renderer inactive)" << std::endl;
            m_renderWarned = true;
        }
        return;
    }

    if (!renderEncoder) {
        if (!m_renderWarned) {
            std::cout << "[SlangImGuiRenderer] render() missing command encoder; skipping draw" << std::endl;
            m_renderWarned = true;
        }
        return;
    }

    if (!drawData || drawData->CmdListsCount == 0 || drawData->TotalVtxCount == 0 ||
        drawData->TotalIdxCount == 0) {
        return;
    }

    const size_t vertexBytes = static_cast<size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const size_t indexBytes = static_cast<size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

    if (!ensureVertexBufferCapacity(vertexBytes) || !ensureIndexBufferCapacity(indexBytes)) {
        std::cout << "[SlangImGuiRenderer] Unable to ensure buffer capacity; skipping draw upload" << std::endl;
        return;
    }

    if (!uploadDrawData(drawData)) {
        std::cout << "[SlangImGuiRenderer] Failed to upload ImGui draw data; skipping draw" << std::endl;
        return;
    }

    if (!createFontResources()) {
        std::cout << "[SlangImGuiRenderer] Font resources unavailable; skipping draw" << std::endl;
        return;
    }

    if (!updateProjectionConstants(drawData)) {
        std::cout << "[SlangImGuiRenderer] Failed to update projection constants; skipping draw" << std::endl;
        return;
    }

    if (!m_loggedProjectionSample) {
        const float samplePos[4] = {
            drawData->DisplayPos.x + drawData->DisplaySize.x * 0.5f,
            drawData->DisplayPos.y + drawData->DisplaySize.y * 0.5f,
            0.0f,
            1.0f};

        float clip[4] = {};
        for (int col = 0; col < 4; ++col) {
            clip[col] =
                samplePos[0] * m_projectionMatrix[0 * 4 + col] +
                samplePos[1] * m_projectionMatrix[1 * 4 + col] +
                samplePos[2] * m_projectionMatrix[2 * 4 + col] +
                samplePos[3] * m_projectionMatrix[3 * 4 + col];
        }
        float ndcX = clip[3] != 0.0f ? clip[0] / clip[3] : 0.0f;
        float ndcY = clip[3] != 0.0f ? clip[1] / clip[3] : 0.0f;
        std::cout << "[SlangImGuiRenderer] Projection sample at display center -> clip(" << clip[0] << ", "
                  << clip[1] << ", " << clip[2] << ", " << clip[3] << ") ndc(" << ndcX << ", " << ndcY
                  << ")" << std::endl;
        m_loggedProjectionSample = true;
    }

    const float fbWidth = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float fbHeight = drawData->DisplaySize.y * drawData->FramebufferScale.y;

    gfx::Viewport viewport = {};
    viewport.originX = 0.0f;
    viewport.originY = 0.0f;
    viewport.extentX = fbWidth;
    viewport.extentY = fbHeight;
    viewport.minZ = 0.0f;
    viewport.maxZ = 1.0f;

    renderEncoder->setViewportAndScissor(viewport);

    auto* rootObject = renderEncoder->bindPipeline(m_pipelineState);
    if (!rootObject) {
        std::cout << "[SlangImGuiRenderer] Failed to bind pipeline; skipping draw" << std::endl;
        return;
    }

    if (!m_loggedBindingLayout) {
        logShaderObjectLayout(rootObject);
        m_loggedBindingLayout = true;
    }

    ShaderCursorHelper rootCursor(rootObject);

    ShaderCursorHelper constantsCursor = rootCursor.getField("ImGuiConstants");
    ShaderCursorHelper projectionCursor = constantsCursor.getField("u_projection");
    const bool projectionCursorValid = projectionCursor.isValid();
    bool projectionBound = false;
    SlangResult projectionResult = SLANG_E_INVALID_ARG;
    const float* projectionData = nullptr;
    SlangMatrixLayoutMode projectionMatrixLayout = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
    bool projectionUsesRowMajor = false;
    if (projectionCursorValid) {
        if (projectionCursor.typeLayout) {
            projectionMatrixLayout = projectionCursor.typeLayout->getMatrixLayoutMode();
        }
        projectionUsesRowMajor = projectionMatrixLayout == SLANG_MATRIX_LAYOUT_ROW_MAJOR;
        projectionData = projectionUsesRowMajor
            ? m_projectionMatrix.data()
            : m_projectionMatrixGpu.data();

        projectionResult = projectionCursor.setData(
            projectionData, sizeof(float) * m_projectionMatrix.size());
        projectionBound = SLANG_SUCCEEDED(projectionResult);
        if (projectionBound && !m_loggedProjectionLayout) {
            std::cout << "[SlangImGuiRenderer] Projection uniform matrix layout: "
                      << (projectionUsesRowMajor ? "row-major" : "column-major")
                      << " (enum=" << static_cast<int>(projectionMatrixLayout) << ")" << std::endl;
            m_loggedProjectionLayout = true;
        }
        if (!projectionBound && !m_renderWarned) {
            std::cout << "[SlangImGuiRenderer] setData for u_projection failed: " << projectionResult
                      << std::endl;
        }
    } else if (!m_renderWarned) {
        std::cout << "[SlangImGuiRenderer] Unable to locate ImGuiConstants.u_projection binding" << std::endl;
    }

    if (projectionData && m_constantBuffer) {
        const size_t matrixBytes = sizeof(float) * m_projectionMatrix.size();
        gfx::MemoryRange matrixRange = {0, matrixBytes};
        void* mappedMatrix = nullptr;
        SlangResult mapResult = m_constantBuffer->map(&matrixRange, &mappedMatrix);
        if (SLANG_SUCCEEDED(mapResult) && mappedMatrix) {
            std::memcpy(mappedMatrix, projectionData, matrixBytes);
            m_constantBuffer->unmap(&matrixRange);
        } else if (!m_renderWarned) {
            std::cout << "[SlangImGuiRenderer] Failed to map projection constant buffer: " << mapResult
                      << std::endl;
        }
    }

    ShaderCursorHelper textureCursor = rootCursor.getField("u_fontTexture");
    ShaderCursorHelper samplerCursor = rootCursor.getField("u_fontSampler");

    const bool samplerCursorValid = samplerCursor.isValid();
    bool samplerBound = false;
    SlangResult samplerResult = SLANG_E_INVALID_ARG;
    if (samplerCursorValid && m_fontSampler) {
        samplerResult = samplerCursor.setSampler(m_fontSampler);
        samplerBound = SLANG_SUCCEEDED(samplerResult);
        if (!samplerBound && !m_renderWarned) {
            std::cout << "[SlangImGuiRenderer] setSampler failed: " << samplerResult << std::endl;
        }
    } else if (!samplerCursorValid && !m_renderWarned) {
        std::cout << "[SlangImGuiRenderer] Unable to locate u_fontSampler binding" << std::endl;
    }

    const bool textureCursorValid = textureCursor.isValid();
    bool attemptedTextureBinding = false;
    bool textureBound = false;
    SlangResult textureResult = SLANG_E_INVALID_ARG;

    renderEncoder->setVertexBuffer(0, m_vertexBuffer);
    renderEncoder->setIndexBuffer(
        m_indexBuffer,
        sizeof(ImDrawIdx) == 2 ? gfx::Format::R16_UINT : gfx::Format::R32_UINT);
    renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);

    ImVec2 displayPos = drawData->DisplayPos;
    ImVec2 displayScale = drawData->FramebufferScale;

    uint32_t globalVertexOffset = 0;
    uint32_t globalIndexOffset = 0;

    gfx::IResourceView* currentTexture = nullptr;

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* commandList = drawData->CmdLists[listIndex];
        for (int commandIndex = 0; commandIndex < commandList->CmdBuffer.Size; ++commandIndex) {
            const ImDrawCmd* command = &commandList->CmdBuffer[commandIndex];

            if (command->UserCallback) {
                if (command->UserCallback == ImDrawCallback_ResetRenderState) {
                    renderEncoder->setViewportAndScissor(viewport);
                    renderEncoder->setVertexBuffer(0, m_vertexBuffer);
                    renderEncoder->setIndexBuffer(
                        m_indexBuffer,
                        sizeof(ImDrawIdx) == 2 ? gfx::Format::R16_UINT : gfx::Format::R32_UINT);
                    renderEncoder->setPrimitiveTopology(gfx::PrimitiveTopology::TriangleList);
                } else {
                    command->UserCallback(commandList, command);
                }
                continue;
            }

            gfx::IResourceView* textureView = m_fontTextureView.get();
            if (command->TextureId) {
                textureView = static_cast<gfx::IResourceView*>(command->TextureId);
            }
            if (!textureView) {
                textureView = m_fontTextureView.get();
            }

            if (textureView && textureView != currentTexture) {
                if (textureCursorValid) {
                    SlangResult srvResult = textureCursor.setResource(textureView);
                    if (SLANG_FAILED(srvResult) && !m_renderWarned) {
                        std::cout << "[SlangImGuiRenderer] setResource failed: " << srvResult << std::endl;
                    }
                    if (!attemptedTextureBinding) {
                        attemptedTextureBinding = true;
                        textureResult = srvResult;
                        textureBound = SLANG_SUCCEEDED(srvResult);
                    }
                    currentTexture = textureView;
                } else if (!m_renderWarned) {
                    std::cout << "[SlangImGuiRenderer] Unable to locate u_fontTexture binding" << std::endl;
                }
            }

            ImVec2 clipMin = ImVec2(
                (command->ClipRect.x - displayPos.x) * displayScale.x,
                (command->ClipRect.y - displayPos.y) * displayScale.y);
            ImVec2 clipMax = ImVec2(
                (command->ClipRect.z - displayPos.x) * displayScale.x,
                (command->ClipRect.w - displayPos.y) * displayScale.y);

            clipMin.x = std::max(clipMin.x, 0.0f);
            clipMin.y = std::max(clipMin.y, 0.0f);
            clipMax.x = std::min(clipMax.x, fbWidth);
            clipMax.y = std::min(clipMax.y, fbHeight);

            if (!m_loggedCommandDetails) {
                std::cout << "[SlangImGuiRenderer] Cmd clipRect orig=" << command->ClipRect.x << ", "
                          << command->ClipRect.y << " -> " << command->ClipRect.z << ", "
                          << command->ClipRect.w << " | transformed min(" << clipMin.x << ", " << clipMin.y
                          << ") max(" << clipMax.x << ", " << clipMax.y << ")"
                          << " elemCount=" << command->ElemCount
                          << " idxOffset=" << command->IdxOffset
                          << " vtxOffset=" << command->VtxOffset
                          << " texture=" << textureView << std::endl;
            }

            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y) {
                continue;
            }

            gfx::ScissorRect rect = {};
            rect.minX = static_cast<int32_t>(std::floor(clipMin.x));
            rect.minY = static_cast<int32_t>(std::floor(clipMin.y));
            rect.maxX = static_cast<int32_t>(std::ceil(clipMax.x));
            rect.maxY = static_cast<int32_t>(std::ceil(clipMax.y));

            if (!m_loggedCommandDetails) {
                std::cout << "[SlangImGuiRenderer] Cmd scissor rect: (" << rect.minX << ", " << rect.minY
                          << ") -> (" << rect.maxX << ", " << rect.maxY << ")" << std::endl;
                m_loggedCommandDetails = true;
            }

            renderEncoder->setScissorRects(1, &rect);

            SlangResult drawResult = renderEncoder->drawIndexed(
                command->ElemCount,
                command->IdxOffset + globalIndexOffset,
                command->VtxOffset + globalVertexOffset);
            if (SLANG_FAILED(drawResult) && !m_renderWarned) {
                std::cout << "[SlangImGuiRenderer] drawIndexed failed: " << drawResult << std::endl;
            }
        }

        globalVertexOffset += commandList->VtxBuffer.Size;
        globalIndexOffset += commandList->IdxBuffer.Size;
    }

    if (!m_loggedDrawStats) {
        std::cout << "[SlangImGuiRenderer] Stage 5 draw encoding active: "
                  << drawData->CmdListsCount << " lists, "
                  << drawData->TotalVtxCount << " vertices, "
                  << drawData->TotalIdxCount << " indices"
                  << " | DisplaySize=" << drawData->DisplaySize.x << "x" << drawData->DisplaySize.y
                  << " FramebufferScale=" << drawData->FramebufferScale.x << "x" << drawData->FramebufferScale.y
                  << " ComputedViewport=" << fbWidth << "x" << fbHeight
                  << std::endl;
        m_loggedDrawStats = true;
    }

    if (!m_loggedBindingResults) {
    std::cout << "[SlangImGuiRenderer] Binding summary: ImGuiConstants.u_projection="
          << (projectionCursorValid ? (projectionBound ? "ok" : "set-failed") : "missing")
          << " (result=" << projectionResult << ")"
          << ", u_fontSampler="
                  << (samplerCursorValid ? (samplerBound ? "ok" : "set-failed") : "missing")
                  << " (result=" << samplerResult << ")"
                  << ", u_fontTexture="
                  << (textureCursorValid
                          ? (textureBound ? "ok" : (attemptedTextureBinding ? "set-failed" : "not-attempted"))
                          : "missing")
          << " (result="
          << (textureCursorValid
              ? (attemptedTextureBinding ? textureResult : SLANG_E_INVALID_ARG)
              : SLANG_E_INVALID_ARG)
          << ")"
                  << std::endl;
        m_loggedBindingResults = true;
    }

    m_renderWarned = false;
}

bool SlangImGuiRenderer::createShaderProgram() {
    if (!m_device) {
        return false;
    }

    Slang::ComPtr<slang::ISession> slangSession;
    SlangResult result = m_device->getSlangSession(slangSession.writeRef());
    if (SLANG_FAILED(result) || !slangSession) {
        std::cout << "[SlangImGuiRenderer] Failed to acquire Slang session for shader compilation" << std::endl;
        return false;
    }

    Slang::ComPtr<slang::IBlob> diagnostics;
    slang::IModule* module = slangSession->loadModuleFromSourceString(
        "SlangImGuiRenderer",
        "SlangImGuiRenderer.slang",
        kImGuiShaderSource,
        diagnostics.writeRef());
    printDiagnostics("Shader diagnostics", diagnostics.get());

    if (!module) {
        std::cout << "[SlangImGuiRenderer] Failed to load ImGui shader module" << std::endl;
        return false;
    }

    Slang::ComPtr<slang::IEntryPoint> vertexEntryPoint;
    Slang::ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    if (SLANG_FAILED(module->findEntryPointByName("vertexMain", vertexEntryPoint.writeRef())) ||
        SLANG_FAILED(module->findEntryPointByName("fragmentMain", fragmentEntryPoint.writeRef()))) {
        std::cout << "[SlangImGuiRenderer] Failed to locate ImGui shader entry points" << std::endl;
        return false;
    }

    std::array<slang::IComponentType*, 3> componentTypes = {
        module,
        vertexEntryPoint.get(),
        fragmentEntryPoint.get(),
    };

    Slang::ComPtr<slang::IComponentType> composedProgram;
    diagnostics = nullptr;
    result = slangSession->createCompositeComponentType(
        componentTypes.data(),
        static_cast<SlangInt>(componentTypes.size()),
        composedProgram.writeRef(),
        diagnostics.writeRef());
    printDiagnostics("Program composition diagnostics", diagnostics.get());

    if (SLANG_FAILED(result) || !composedProgram) {
        std::cout << "[SlangImGuiRenderer] Failed to compose ImGui shader program" << std::endl;
        return false;
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    diagnostics = nullptr;
    result = composedProgram->link(linkedProgram.writeRef(), diagnostics.writeRef());
    printDiagnostics("Program link diagnostics", diagnostics.get());

    if (SLANG_FAILED(result) || !linkedProgram) {
        std::cout << "[SlangImGuiRenderer] Failed to link ImGui shader program" << std::endl;
        return false;
    }

    gfx::IShaderProgram::Desc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram.get();

    diagnostics = nullptr;
    result = m_device->createProgram(
        programDesc,
        m_shaderProgram.writeRef(),
        diagnostics.writeRef());
    printDiagnostics("Shader program creation diagnostics", diagnostics.get());

    if (SLANG_FAILED(result) || !m_shaderProgram) {
        std::cout << "[SlangImGuiRenderer] createProgram failed with result " << result << std::endl;
        return false;
    }

    return true;
}

bool SlangImGuiRenderer::createInputLayout() {
    if (!m_device) {
        return false;
    }

    if (!m_loggedLayoutInfo) {
        std::cout << "[SlangImGuiRenderer] ImDrawVert size=" << sizeof(ImDrawVert)
                  << " posOffset=" << offsetof(ImDrawVert, pos)
                  << " uvOffset=" << offsetof(ImDrawVert, uv)
                  << " colOffset=" << offsetof(ImDrawVert, col)
                  << " ImDrawIdx size=" << sizeof(ImDrawIdx) << std::endl;
        m_loggedLayoutInfo = true;
    }

    gfx::InputElementDesc inputElements[] = {
        {"POSITION", 0, gfx::Format::R32G32_FLOAT, offsetof(ImDrawVert, pos), 0},
        {"TEXCOORD", 0, gfx::Format::R32G32_FLOAT, offsetof(ImDrawVert, uv), 0},
        {"COLOR", 0, gfx::Format::R8G8B8A8_UNORM, offsetof(ImDrawVert, col), 0},
    };

    constexpr gfx::GfxCount elementCount =
        static_cast<gfx::GfxCount>(sizeof(inputElements) / sizeof(inputElements[0]));

    m_inputLayout =
        m_device->createInputLayout(sizeof(ImDrawVert), inputElements, elementCount);

    return m_inputLayout != nullptr;
}

bool SlangImGuiRenderer::createPipelineState() {
    if (!m_device || !m_shaderProgram || !m_inputLayout || !m_framebufferLayout) {
        return false;
    }

    gfx::GraphicsPipelineStateDesc pipelineDesc = {};
    pipelineDesc.program = m_shaderProgram.get();
    pipelineDesc.inputLayout = m_inputLayout.get();
    pipelineDesc.framebufferLayout = m_framebufferLayout.get();
    pipelineDesc.primitiveType = gfx::PrimitiveType::Triangle;

    pipelineDesc.depthStencil.depthTestEnable = false;
    pipelineDesc.depthStencil.depthWriteEnable = false;

    pipelineDesc.rasterizer.cullMode = gfx::CullMode::None;
    pipelineDesc.rasterizer.fillMode = gfx::FillMode::Solid;
    pipelineDesc.rasterizer.frontFace = gfx::FrontFaceMode::CounterClockwise;
    pipelineDesc.rasterizer.scissorEnable = true;

    pipelineDesc.blend.targetCount = 1;
    auto& target = pipelineDesc.blend.targets[0];
    target.enableBlend = true;
    target.color.srcFactor = gfx::BlendFactor::SrcAlpha;
    target.color.dstFactor = gfx::BlendFactor::InvSrcAlpha;
    target.color.op = gfx::BlendOp::Add;
    target.alpha.srcFactor = gfx::BlendFactor::One;
    target.alpha.dstFactor = gfx::BlendFactor::InvSrcAlpha;
    target.alpha.op = gfx::BlendOp::Add;
    target.writeMask = gfx::RenderTargetWriteMask::EnableAll;

    m_pipelineState = m_device->createGraphicsPipelineState(pipelineDesc);
    return m_pipelineState != nullptr;
}

bool SlangImGuiRenderer::ensureVertexBufferCapacity(size_t requiredBytes) {
    if (requiredBytes == 0) {
        return true;
    }

    if (!m_device) {
        return false;
    }

    if (requiredBytes <= m_vertexBufferSize && m_vertexBuffer) {
        return true;
    }

    size_t newSize = m_vertexBufferSize ? m_vertexBufferSize : 65536;
    while (newSize < requiredBytes) {
        newSize *= 2;
    }

    gfx::IBufferResource::Desc desc = {};
    desc.type = gfx::IResource::Type::Buffer;
    desc.sizeInBytes = newSize;
    desc.defaultState = gfx::ResourceState::VertexBuffer;
    desc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::General,
        gfx::ResourceState::VertexBuffer);
    desc.memoryType = gfx::MemoryType::Upload;

    Slang::ComPtr<gfx::IBufferResource> buffer = m_device->createBufferResource(desc, nullptr);
    if (!buffer) {
        return false;
    }

    buffer->setDebugName("SlangImGuiRenderer VertexBuffer");
    m_vertexBuffer = buffer;
    m_vertexBufferSize = newSize;
    std::cout << "[SlangImGuiRenderer] Resized vertex buffer to " << m_vertexBufferSize << " bytes" << std::endl;
    return true;
}

bool SlangImGuiRenderer::ensureIndexBufferCapacity(size_t requiredBytes) {
    if (requiredBytes == 0) {
        return true;
    }

    if (!m_device) {
        return false;
    }

    if (requiredBytes <= m_indexBufferSize && m_indexBuffer) {
        return true;
    }

    size_t newSize = m_indexBufferSize ? m_indexBufferSize : 32768;
    while (newSize < requiredBytes) {
        newSize *= 2;
    }

    gfx::IBufferResource::Desc desc = {};
    desc.type = gfx::IResource::Type::Buffer;
    desc.sizeInBytes = newSize;
    desc.defaultState = gfx::ResourceState::IndexBuffer;
    desc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::General,
        gfx::ResourceState::IndexBuffer);
    desc.memoryType = gfx::MemoryType::Upload;

    Slang::ComPtr<gfx::IBufferResource> buffer = m_device->createBufferResource(desc, nullptr);
    if (!buffer) {
        return false;
    }

    buffer->setDebugName("SlangImGuiRenderer IndexBuffer");
    m_indexBuffer = buffer;
    m_indexBufferSize = newSize;
    std::cout << "[SlangImGuiRenderer] Resized index buffer to " << m_indexBufferSize << " bytes" << std::endl;
    return true;
}

bool SlangImGuiRenderer::uploadDrawData(const ImDrawData* drawData) {
    if (!drawData || !m_vertexBuffer || !m_indexBuffer) {
        return false;
    }

    const size_t vertexBytes = static_cast<size_t>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const size_t indexBytes = static_cast<size_t>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);

    if (vertexBytes == 0 || indexBytes == 0) {
        return true;
    }

    gfx::MemoryRange vertexRange = {0, vertexBytes};
    void* vertexData = nullptr;
    if (SLANG_FAILED(m_vertexBuffer->map(&vertexRange, &vertexData)) || !vertexData) {
        return false;
    }

    gfx::MemoryRange indexRange = {0, indexBytes};
    void* indexData = nullptr;
    if (SLANG_FAILED(m_indexBuffer->map(&indexRange, &indexData)) || !indexData) {
        m_vertexBuffer->unmap(nullptr);
        return false;
    }

    ImDrawVert* vertexDst = static_cast<ImDrawVert*>(vertexData);
    ImDrawIdx* indexDst = static_cast<ImDrawIdx*>(indexData);
    ImDrawVert* const vertexStart = vertexDst;
    ImDrawIdx* const indexStart = indexDst;

    ImVec2 minPos = {FLT_MAX, FLT_MAX};
    ImVec2 maxPos = {-FLT_MAX, -FLT_MAX};
    size_t vertexCountAccum = 0;
    ImU32 sampleColor = 0;
    bool sampleCaptured = false;

    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex) {
        const ImDrawList* cmdList = drawData->CmdLists[listIndex];
        const size_t vtxCount = static_cast<size_t>(cmdList->VtxBuffer.Size);
        const size_t idxCount = static_cast<size_t>(cmdList->IdxBuffer.Size);

        if (!m_loggedSourceIndexSamples && idxCount > 0) {
            const size_t sampleCount = std::min<size_t>(12, idxCount);
            std::cout << "[SlangImGuiRenderer] Source index samples (count=" << sampleCount << "):";
            for (size_t i = 0; i < sampleCount; ++i) {
                std::cout << ' ' << cmdList->IdxBuffer.Data[i];
            }
            std::cout << std::endl;
            m_loggedSourceIndexSamples = true;
        }

        std::memcpy(vertexDst, cmdList->VtxBuffer.Data, vtxCount * sizeof(ImDrawVert));
        std::memcpy(indexDst, cmdList->IdxBuffer.Data, idxCount * sizeof(ImDrawIdx));

        for (ImDrawIdx i = 0; i < cmdList->VtxBuffer.Size; ++i) {
            const ImDrawVert& v = cmdList->VtxBuffer.Data[i];
            minPos.x = std::min(minPos.x, v.pos.x);
            minPos.y = std::min(minPos.y, v.pos.y);
            maxPos.x = std::max(maxPos.x, v.pos.x);
            maxPos.y = std::max(maxPos.y, v.pos.y);
            if (!sampleCaptured) {
                sampleColor = v.col;
                sampleCaptured = true;
            }
        }
        vertexCountAccum += vtxCount;

        vertexDst += vtxCount;
        indexDst += idxCount;
    }

    if (vertexCountAccum > 0 && !m_loggedVertexBounds) {
    std::cout << "[SlangImGuiRenderer] Vertex bounds: min(" << minPos.x << ", " << minPos.y
                  << ") max(" << maxPos.x << ", " << maxPos.y << ")"
                  << " firstColor=0x" << std::hex << sampleColor << std::dec << std::endl;
        m_loggedVertexBounds = true;
    }

    if (drawData->TotalVtxCount > 0 && !m_loggedVertexSamples) {
        const size_t sampleCount = std::min<size_t>(4, static_cast<size_t>(drawData->TotalVtxCount));
        std::cout << "[SlangImGuiRenderer] Vertex samples (count=" << sampleCount << "):" << std::endl;
        for (size_t i = 0; i < sampleCount; ++i) {
            const ImDrawVert& v = vertexStart[i];
            std::cout << "    V" << i << " pos(" << v.pos.x << ", " << v.pos.y << ") uv(" << v.uv.x << ", "
                      << v.uv.y << ") col=0x" << std::hex << std::setw(8) << std::setfill('0') << v.col
                      << std::dec << std::setfill(' ') << std::endl;
        }

        const size_t sampleIdxCount = std::min<size_t>(12, static_cast<size_t>(drawData->TotalIdxCount));
        std::cout << "[SlangImGuiRenderer] Index samples (count=" << sampleIdxCount << "):";
        for (size_t i = 0; i < sampleIdxCount; ++i) {
            std::cout << ' ' << indexStart[i];
        }
        std::cout << std::endl;
        m_loggedVertexSamples = true;
    }

    m_vertexBuffer->unmap(&vertexRange);
    m_indexBuffer->unmap(&indexRange);

    return true;
}

bool SlangImGuiRenderer::initializeConstantBuffer() {
    if (!m_device) {
        return false;
    }

    if (m_constantBuffer && m_constantBufferSize >= sizeof(float) * 16) {
        return true;
    }

    const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    gfx::IBufferResource::Desc desc = {};
    desc.type = gfx::IResource::Type::Buffer;
    desc.sizeInBytes = sizeof(identity);
    desc.defaultState = gfx::ResourceState::ConstantBuffer;
    desc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::General,
        gfx::ResourceState::ConstantBuffer);
    desc.memoryType = gfx::MemoryType::Upload;

    m_constantBuffer = m_device->createBufferResource(desc, identity);
    if (!m_constantBuffer) {
        return false;
    }

    m_constantBuffer->setDebugName("SlangImGuiRenderer ConstantBuffer");
    m_constantBufferSize = desc.sizeInBytes;
    return true;
}

bool SlangImGuiRenderer::updateProjectionConstants(const ImDrawData* drawData) {
    if (!drawData) {
        return false;
    }

    if (!initializeConstantBuffer()) {
        return false;
    }

    const float L = drawData->DisplayPos.x;
    const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float T = drawData->DisplayPos.y;
    const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

    m_projectionMatrix = {
        2.0f / (R - L), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (T - B), 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f,
    };

    if (!m_loggedProjectionMatrix) {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << "[SlangImGuiRenderer] Projection matrix rows:" << std::endl;
        for (int row = 0; row < 4; ++row) {
            std::cout << "    [";
            for (int col = 0; col < 4; ++col) {
                int idx = row * 4 + col;
                std::cout << std::setw(12) << m_projectionMatrix[idx];
                if (col < 3) {
                    std::cout << ", ";
                }
            }
            std::cout << "]" << std::endl;
        }

        auto logPoint = [&](const char* label, float x, float y) {
            float vec[4] = {x, y, 0.0f, 1.0f};
            float clip[4] = {};
            for (int col = 0; col < 4; ++col) {
                clip[col] =
                    vec[0] * m_projectionMatrix[0 * 4 + col] +
                    vec[1] * m_projectionMatrix[1 * 4 + col] +
                    vec[2] * m_projectionMatrix[2 * 4 + col] +
                    vec[3] * m_projectionMatrix[3 * 4 + col];
            }
            float ndcX = clip[3] != 0.0f ? clip[0] / clip[3] : 0.0f;
            float ndcY = clip[3] != 0.0f ? clip[1] / clip[3] : 0.0f;
            std::cout << "[SlangImGuiRenderer] " << label << " -> clip(" << clip[0] << ", " << clip[1] << ", "
                      << clip[2] << ", " << clip[3] << ") ndc(" << ndcX << ", " << ndcY << ")" << std::endl;
        };

        logPoint("Proj corner TL", L, T);
        logPoint("Proj corner BR", R, B);
        logPoint("Proj display center", L + (R - L) * 0.5f, T + (B - T) * 0.5f);

        std::cout.unsetf(std::ios::floatfield);
        std::cout.precision(6);
        m_loggedProjectionMatrix = true;
    }

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            m_projectionMatrixGpu[col * 4 + row] = m_projectionMatrix[row * 4 + col];
        }
    }

    return true;
}

bool SlangImGuiRenderer::createFontResources() {
    if (!m_device) {
        return false;
    }

    if (m_fontTexture && m_fontTextureView && m_fontSampler) {
        return true;
    }

    if (!ImGui::GetCurrentContext()) {
        std::cout << "[SlangImGuiRenderer] createFontResources() requires an active ImGui context" << std::endl;
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    if (!pixels || width <= 0 || height <= 0) {
        std::cout << "[SlangImGuiRenderer] ImGui font atlas data missing or empty" << std::endl;
        return false;
    }

    gfx::ITextureResource::Desc textureDesc = {};
    textureDesc.type = gfx::IResource::Type::Texture2D;
    textureDesc.format = gfx::Format::R8G8B8A8_UNORM;
    textureDesc.size.width = static_cast<uint32_t>(width);
    textureDesc.size.height = static_cast<uint32_t>(height);
    textureDesc.size.depth = 1;
    textureDesc.numMipLevels = 1;
    textureDesc.arraySize = 1;
    textureDesc.sampleDesc.numSamples = 1;
    textureDesc.sampleDesc.quality = 0;
    textureDesc.defaultState = gfx::ResourceState::ShaderResource;
    textureDesc.allowedStates = gfx::ResourceStateSet(
        gfx::ResourceState::ShaderResource,
        gfx::ResourceState::CopyDestination);

    gfx::ITextureResource::SubresourceData initData = {};
    initData.data = pixels;
    initData.strideY = static_cast<gfx::Size>(width) * 4;

    Slang::ComPtr<gfx::ITextureResource> fontTexture =
        m_device->createTextureResource(textureDesc, &initData);
    if (!fontTexture) {
        std::cout << "[SlangImGuiRenderer] Failed to create font texture resource" << std::endl;
        return false;
    }
    fontTexture->setDebugName("SlangImGuiRenderer FontTexture");

    gfx::IResourceView::Desc viewDesc = {};
    viewDesc.format = textureDesc.format;
    viewDesc.type = gfx::IResourceView::Type::ShaderResource;

    Slang::ComPtr<gfx::IResourceView> fontTextureView =
        m_device->createTextureView(fontTexture, viewDesc);
    if (!fontTextureView) {
        std::cout << "[SlangImGuiRenderer] Failed to create font texture view" << std::endl;
        return false;
    }

    gfx::ISamplerState::Desc samplerDesc = {};
    Slang::ComPtr<gfx::ISamplerState> fontSampler =
        m_device->createSamplerState(samplerDesc);
    if (!fontSampler) {
        std::cout << "[SlangImGuiRenderer] Failed to create font sampler" << std::endl;
        return false;
    }

    io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(fontTextureView.get()));
    io.Fonts->ClearTexData();

    m_fontTexture = fontTexture;
    m_fontTextureView = fontTextureView;
    m_fontSampler = fontSampler;

    std::cout << "[SlangImGuiRenderer] Uploaded font atlas of size "
              << width << "x" << height << std::endl;

    return true;
}
