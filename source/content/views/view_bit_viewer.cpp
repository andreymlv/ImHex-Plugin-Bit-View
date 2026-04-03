#include "content/views/view_bit_viewer.hpp"

#include <hex/api/imhex_api/hex_editor.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/events/events_interaction.hpp>
#include <hex/providers/provider.hpp>
#include <hex/ui/imgui_imhex_extensions.h>

#include <fonts/vscode_icons.hpp>
#include <imgui.h>

#include <algorithm>

namespace hex::plugin::bitview {

    ViewBitViewer::ViewBitViewer() : View::Window("Bit Viewer", ICON_VS_SYMBOL_ARRAY) {
        EventRegionSelected::subscribe(this, [this](const ImHexApi::HexEditor::ProviderRegion &providerRegion) {
            auto region = providerRegion.getRegion();
            if (region == Region::Invalid())
                m_selectedRegion.reset();
            else {
                m_selectedRegion = region;
                m_shouldScrollToSelection = true;
            }
        });
    }

    ViewBitViewer::~ViewBitViewer() {
        EventRegionSelected::unsubscribe(this);
    }

    void ViewBitViewer::drawHelpText() {
        ImGui::TextWrapped(
            "Visualizes binary data as a grid of colored cells — one cell per bit. "
            "Click on a bit to select the corresponding byte in the Hex Editor."
        );
    }

    void ViewBitViewer::drawContent() {
        auto provider = ImHexApi::Provider::get();
        if (provider == nullptr || !provider->isReadable() || provider->getActualSize() == 0) {
            ImGui::TextDisabled("No data available");
            return;
        }

        drawSettings();
        ImGui::Separator();
        drawBitGrid(provider);
    }

    void ViewBitViewer::drawSettings() {
        if (!ImGui::CollapsingHeader("Data Cell Options", ImGuiTreeNodeFlags_DefaultOpen))
            return;

        const float labelWidth = 120_scaled;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Bits per row");
        ImGui::SameLine(labelWidth);
        ImGui::Checkbox("Fit to width", &m_fitToWidth);

        if (!m_fitToWidth) {
            ImGui::SetCursorPosX(labelWidth);
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGuiExt::InputTextIcon("##bitsperrow", ICON_VS_SYMBOL_OPERATOR, m_bitsPerRowInput)) {
                m_lastEvalResult = m_evaluator.evaluate(m_bitsPerRowInput);
                if (m_lastEvalResult.has_value())
                    m_bitsPerRow = std::max(i64(1), *m_lastEvalResult);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::TextUnformatted("Supports math: 1024 * 4, 8 * 3 + 1, etc.");
                if (m_lastEvalResult.has_value())
                    ImGuiExt::TextFormatted("= {} bits", m_bitsPerRow);
                else
                    ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid expression");
                ImGui::EndTooltip();
            }
            ImGui::PopItemWidth();
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Pixel size");
        ImGui::SameLine(labelWidth);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##pixelsize", &m_pixelSize, 1, 32);
        m_pixelSize = std::clamp(m_pixelSize, 1, 32);
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Cell gap");
        ImGui::SameLine(labelWidth);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##cellgap", &m_cellGap, 0, 4);
        m_cellGap = std::clamp(m_cellGap, 0, 4);
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Bit order");
        ImGui::SameLine(labelWidth);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        const char *bitOrderItems[] = { "MSB first (7 6 5 4 3 2 1 0)", "LSB first (0 1 2 3 4 5 6 7)" };
        int bitOrderIdx = static_cast<int>(m_bitOrder);
        if (ImGui::Combo("##bitorder", &bitOrderIdx, bitOrderItems, 2))
            m_bitOrder = static_cast<BitOrder>(bitOrderIdx);
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Colors");
        ImGui::SameLine(labelWidth);
        ImGui::ColorEdit4("0 bit##col0", &m_colorZero.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("0");
        ImGui::SameLine(0, 16_scaled);
        ImGui::ColorEdit4("1 bit##col1", &m_colorOne.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("1");
    }

    void ViewBitViewer::drawBitGrid(prv::Provider *provider) {
        const u64 baseAddress = provider->getBaseAddress();
        const u64 totalBytes = provider->getActualSize();
        const u64 totalBits = totalBytes * 8;

        const float cellSize = static_cast<float>(m_pixelSize);
        const float gap = static_cast<float>(m_cellGap);
        const float cellStride = cellSize + gap;

        const ImVec2 availableSize = ImGui::GetContentRegionAvail();
        if (availableSize.x <= 0 || availableSize.y <= 0)
            return;

        const float scrollbarW = ImGui::GetStyle().ScrollbarSize;
        i64 bitsPerRow;
        if (m_fitToWidth)
            bitsPerRow = std::max(i64(1), static_cast<i64>((availableSize.x - scrollbarW) / cellStride));
        else
            bitsPerRow = std::max(i64(1), m_bitsPerRow);

        const u64 totalRows = (totalBits + bitsPerRow - 1) / bitsPerRow;
        const float gridWidth = bitsPerRow * cellStride;
        const float contentHeight = totalRows * cellStride;

        ImGuiWindowFlags childFlags = m_fitToWidth ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar;
        ImGui::SetNextWindowContentSize(ImVec2(gridWidth, contentHeight));

        if (!ImGui::BeginChild("##bitgrid", availableSize, ImGuiChildFlags_None, childFlags)) {
            ImGui::EndChild();
            return;
        }

        // Scroll to hex editor selection
        if (m_shouldScrollToSelection && m_selectedRegion.has_value()) {
            const u64 selBit = (m_selectedRegion->getStartAddress() - baseAddress) * 8;
            const float targetY = static_cast<float>(selBit / bitsPerRow) * cellStride;
            const float scrollY = ImGui::GetScrollY();
            const float winH = ImGui::GetWindowHeight();
            if (targetY < scrollY || targetY > scrollY + winH - cellStride)
                ImGui::SetScrollY(std::max(0.0f, targetY - winH / 3.0f));
            m_shouldScrollToSelection = false;
        }

        const float scrollY = ImGui::GetScrollY();
        const u64 firstRow = static_cast<u64>(scrollY / cellStride);
        const u64 visibleCount = static_cast<u64>(ImGui::GetWindowHeight() / cellStride) + 2;
        const u64 lastRow = std::min(totalRows, firstRow + visibleCount);

        const u64 firstByte = (firstRow * bitsPerRow) / 8;
        const u64 lastBit = std::min(totalBits, lastRow * bitsPerRow);
        const u64 lastByte = std::min(totalBytes, (lastBit + 7) / 8);

        if (lastByte <= firstByte) {
            ImGui::EndChild();
            return;
        }

        // Read only visible bytes, reusing buffer to avoid per-frame allocation
        m_readBuffer.resize(lastByte - firstByte);
        provider->read(baseAddress + firstByte, m_readBuffer.data(), m_readBuffer.size());

        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        const ImU32 colZero = ImGui::ColorConvertFloat4ToU32(m_colorZero);
        const ImU32 colOne  = ImGui::ColorConvertFloat4ToU32(m_colorOne);
        const ImU32 colSel  = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.5f, 1.0f, 0.7f));

        const bool msbFirst = (m_bitOrder == BitOrder::MsbFirst);

        u64 selStart = 0, selEnd = 0;
        const bool hasSel = m_selectedRegion.has_value();
        if (hasSel) {
            selStart = m_selectedRegion->getStartAddress() - baseAddress;
            selEnd = selStart + m_selectedRegion->getSize();
        }

        // O(1) hover detection: compute hovered cell from mouse position
        const ImVec2 mousePos = ImGui::GetMousePos();
        const bool mouseInWindow = ImGui::IsWindowHovered();
        i64 hoveredCol = -1;
        u64 hoveredRow = 0;

        if (mouseInWindow && cellStride > 0) {
            const float localX = mousePos.x - origin.x;
            const float localY = mousePos.y - origin.y;
            const i64 col = static_cast<i64>(localX / cellStride);
            const u64 row = static_cast<u64>(localY / cellStride);
            // Check that mouse is on the cell, not in the gap
            const float fracX = localX - col * cellStride;
            const float fracY = localY - row * cellStride;
            if (col >= 0 && col < bitsPerRow &&
                row >= firstRow && row < lastRow &&
                fracX >= 0 && fracX < cellSize &&
                fracY >= 0 && fracY < cellSize) {
                const u64 bitIdx = row * bitsPerRow + col;
                if (bitIdx < totalBits) {
                    hoveredCol = col;
                    hoveredRow = row;
                }
            }
        }

        // Render visible cells
        for (u64 row = firstRow; row < lastRow; row++) {
            const float y = origin.y + row * cellStride;
            const u64 rowBitStart = row * bitsPerRow;

            for (i64 col = 0; col < bitsPerRow; col++) {
                const u64 bitIdx = rowBitStart + col;
                if (bitIdx >= totalBits) break;

                const u64 byteIdx = bitIdx / 8;
                const int bitPos = static_cast<int>(bitIdx % 8);
                const int shift = msbFirst ? (7 - bitPos) : bitPos;
                const bool val = (m_readBuffer[byteIdx - firstByte] >> shift) & 1;

                const float x = origin.x + col * cellStride;
                const ImVec2 cellMin(x, y);
                const ImVec2 cellMax(x + cellSize, y + cellSize);

                drawList->AddRectFilled(cellMin, cellMax, val ? colOne : colZero);

                if (hasSel && byteIdx >= selStart && byteIdx < selEnd)
                    drawList->AddRect(cellMin, cellMax, colSel, 0.0f, 0, 1.0f);
            }
        }

        // Draw hover highlight and tooltip outside the render loop
        if (hoveredCol >= 0) {
            const u64 bitIdx = hoveredRow * bitsPerRow + hoveredCol;
            const u64 byteIdx = bitIdx / 8;
            const int bitPos = static_cast<int>(bitIdx % 8);
            const int shift = msbFirst ? (7 - bitPos) : bitPos;
            const bool val = (m_readBuffer[byteIdx - firstByte] >> shift) & 1;

            const float x = origin.x + hoveredCol * cellStride;
            const float y = origin.y + hoveredRow * cellStride;
            const ImVec2 cellMin(x, y);
            const ImVec2 cellMax(x + cellSize, y + cellSize);

            const ImU32 colHover = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 0.0f, 0.7f));
            drawList->AddRect(cellMin, cellMax, colHover, 0.0f, 0, 2.0f);

            ImGui::BeginTooltip();
            ImGuiExt::TextFormatted("Byte: 0x{:X}", byteIdx);
            ImGuiExt::TextFormatted("Bit: {} ({})", bitPos, msbFirst ? "MSB first" : "LSB first");
            ImGuiExt::TextFormatted("Value: {}", val ? 1 : 0);
            ImGui::EndTooltip();

            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                ImHexApi::HexEditor::setSelection(baseAddress + byteIdx, 1);
        }

        ImGui::EndChild();
    }

}
