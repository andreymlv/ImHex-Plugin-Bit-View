#include "content/views/view_bit_viewer.hpp"

#include <hex/api/imhex_api/hex_editor.hpp>
#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/events/events_interaction.hpp>
#include <hex/api/localization_manager.hpp>
#include <hex/providers/provider.hpp>
#include <hex/ui/imgui_imhex_extensions.h>

#include <fonts/vscode_icons.hpp>
#include <imgui.h>

#include <algorithm>

namespace hex::plugin::bitview {

    ViewBitViewer::ViewBitViewer() : View::Window("hex.bitview.view.name", ICON_VS_SYMBOL_ARRAY) {
        EventRegionSelected::subscribe(this, [this](const ImHexApi::HexEditor::ProviderRegion &providerRegion) {
            // Skip events that are echoes of our own syncSelectionToHexEditor call.
            // The subscriber normalizes anchor=start, cursor=end which destroys
            // selection direction — breaking drag-upward and shift-select-upward.
            if (m_suppressNextExternalEvent) {
                m_suppressNextExternalEvent = false;
                return;
            }

            auto region = providerRegion.getRegion();
            if (region == Region::Invalid()) {
                m_selectedRegion.reset();
                m_hasCursor = false;
                return;
            }

            m_selectedRegion = region;
            m_shouldScrollToSelection = true;

            auto *provider = ImHexApi::Provider::get();
            if (provider) {
                u64 base = provider->getBaseAddress();
                m_selectionAnchor = region.getStartAddress() - base;
                m_cursorOffset = m_selectionAnchor + region.getSize() - 1;
                m_hasCursor = true;
            }
        });
    }

    ViewBitViewer::~ViewBitViewer() {
        EventRegionSelected::unsubscribe(this);
    }

    void ViewBitViewer::drawHelpText() {
        ImGuiExt::TextFormattedWrapped("{}", "hex.bitview.view.help"_lang);
    }

    void ViewBitViewer::drawContent() {
        auto provider = ImHexApi::Provider::get();
        if (provider == nullptr || !provider->isReadable() || provider->getActualSize() == 0) {
            ImGuiExt::TextFormattedDisabled("{}", "hex.bitview.view.no_data"_lang);
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape) && m_hasCursor) {
            const bool hasMultiByteSelection = m_selectedRegion.has_value() && m_selectedRegion->getSize() > 1;
            if (hasMultiByteSelection) {
                // First Esc: collapse multi-byte selection to single cursor
                m_selectionAnchor = m_cursorOffset;
                auto *prov = ImHexApi::Provider::get();
                if (prov) {
                    u64 addr = prov->getBaseAddress() + m_cursorOffset;
                    m_selectedRegion = Region{addr, 1};
                    m_suppressNextExternalEvent = true;
                    ImHexApi::HexEditor::setSelection(addr, 1);
                }
            } else {
                // Second Esc: hide cursor in bit viewer.
                // The hex editor always maintains a cursor — no API to clear it.
                m_hasCursor = false;
                m_selectedRegion.reset();
            }
        }

        drawSettings();
        ImGui::Separator();
        drawBitGrid(provider);
    }

    void ViewBitViewer::drawSettings() {
        if (!ImGui::CollapsingHeader("hex.bitview.settings.header"_lang, ImGuiTreeNodeFlags_DefaultOpen))
            return;

        const float labelWidth = 120_scaled;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("hex.bitview.settings.bits_per_row"_lang);
        ImGui::SameLine(labelWidth);
        ImGui::Checkbox("hex.bitview.settings.fit_to_width"_lang, &m_fitToWidth);

        if (!m_fitToWidth) {
            ImGui::SetCursorPosX(labelWidth);
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGuiExt::InputTextIcon("##bitsperrow", ICON_VS_SYMBOL_OPERATOR, m_bitsPerRowInput)) {
                m_lastEvalResult = m_evaluator.evaluate(m_bitsPerRowInput);
                if (m_lastEvalResult.has_value())
                    m_bitsPerRow = std::max(i64(1), *m_lastEvalResult);
            }
            ImGui::PopItemWidth();
            ImGui::SetCursorPosX(labelWidth);
            if (m_lastEvalResult.has_value())
                ImGuiExt::TextFormatted("= {} bits", m_bitsPerRow);
            else
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", static_cast<const char *>("hex.bitview.settings.math_error"_lang));
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("hex.bitview.settings.pixel_size"_lang);
        ImGui::SameLine(labelWidth);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderInt("##pixelsize", &m_pixelSize, 1, 32);
        m_pixelSize = std::clamp(m_pixelSize, 1, 32);
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("hex.bitview.settings.bit_order"_lang);
        ImGui::SameLine(labelWidth);
        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
        const std::string msbLabel = "hex.bitview.settings.bit_order.msb"_lang;
        const std::string lsbLabel = "hex.bitview.settings.bit_order.lsb"_lang;
        const char *bitOrderItems[] = { msbLabel.c_str(), lsbLabel.c_str() };
        int bitOrderIdx = static_cast<int>(m_bitOrder);
        if (ImGui::Combo("##bitorder", &bitOrderIdx, bitOrderItems, 2))
            m_bitOrder = static_cast<BitOrder>(bitOrderIdx);
        ImGui::PopItemWidth();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("hex.bitview.settings.colors"_lang);
        ImGui::SameLine(labelWidth);
        ImGui::ColorEdit4("0 bit##col0", &m_colorZero.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("0");
        ImGui::SameLine(0, 16_scaled);
        ImGui::ColorEdit4("1 bit##col1", &m_colorOne.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
        ImGui::SameLine();
        ImGui::TextUnformatted("1");
    }

    // --- Grid orchestration ---

    void ViewBitViewer::drawBitGrid(prv::Provider *provider) {
        GridLayout layout = computeLayout(provider);

        const ImVec2 availableSize = ImGui::GetContentRegionAvail();
        if (availableSize.x <= 0 || availableSize.y <= 0)
            return;

        // ImGui uses float for scroll positions, which loses precision beyond ~10M pixels.
        // Cap virtual content size and map scroll positions to rows/columns proportionally.
        constexpr float maxVirtualSize = 10'000'000.0f;
        const double realContentHeight = static_cast<double>(layout.totalRows) * static_cast<double>(layout.cellStride);
        const float contentHeight = static_cast<float>(std::min(realContentHeight, static_cast<double>(maxVirtualSize)));
        const double realGridWidth = static_cast<double>(layout.bitsPerRow) * static_cast<double>(layout.cellStride);
        const float gridWidth = static_cast<float>(std::min(realGridWidth, static_cast<double>(maxVirtualSize)));

        // NoNav prevents ImGui's built-in keyboard navigation from scrolling
        // this child window with arrow keys — we handle keyboard scrolling ourselves.
        ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoNav
            | (m_fitToWidth ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::SetNextWindowContentSize(ImVec2(gridWidth, contentHeight));

        if (!ImGui::BeginChild("##bitgrid", availableSize, ImGuiChildFlags_None, childFlags)) {
            ImGui::EndChild();
            return;
        }

        const float scrollY = ImGui::GetScrollY();
        const float scrollMaxY = ImGui::GetScrollMaxY();
        const float hScrollbarH = m_fitToWidth ? 0.0f : ImGui::GetStyle().ScrollbarSize;
        const u64 actualVisibleRows = std::max(u64(1), static_cast<u64>((availableSize.y - hScrollbarH) / layout.cellStride));
        layout.actualVisibleRows = actualVisibleRows;
        const u64 visibleRows = actualVisibleRows + 2;
        const u64 maxFirstRow = layout.totalRows > actualVisibleRows ? layout.totalRows - actualVisibleRows : 0;

        // Map float scroll position to row index proportionally (see virtual scroll design in CLAUDE.md)
        u64 scrollRow = 0;
        if (scrollMaxY > 0 && maxFirstRow > 0) {
            double fraction = std::clamp(static_cast<double>(scrollY) / static_cast<double>(scrollMaxY), 0.0, 1.0);
            scrollRow = std::min(static_cast<u64>(fraction * static_cast<double>(maxFirstRow)), maxFirstRow);
        }

        if (m_targetFirstRow.has_value()) {
            u64 tolerance = std::max(u64(1), maxFirstRow / 1'000'000);
            if (scrollRow > *m_targetFirstRow ? scrollRow - *m_targetFirstRow <= tolerance
                                              : *m_targetFirstRow - scrollRow <= tolerance)
                layout.firstRow = std::min(*m_targetFirstRow, maxFirstRow);
            else
                m_targetFirstRow.reset();
        }
        if (!m_targetFirstRow.has_value())
            layout.firstRow = scrollRow;

        // Compute visible column range -- clip drawing to on-screen columns only
        const float windowW = ImGui::GetWindowWidth();
        const i64 visibleCols = std::max(i64(1), static_cast<i64>(windowW / layout.cellStride)) + 2;

        if (!m_fitToWidth) {
            const i64 maxFirstCol = std::max(i64(0), layout.bitsPerRow - visibleCols);
            const float scrollX = ImGui::GetScrollX();
            const float scrollMaxX = ImGui::GetScrollMaxX();

            i64 scrollCol = 0;
            if (scrollMaxX > 0 && maxFirstCol > 0) {
                double fraction = std::clamp(static_cast<double>(scrollX) / static_cast<double>(scrollMaxX), 0.0, 1.0);
                scrollCol = std::min(static_cast<i64>(fraction * static_cast<double>(maxFirstCol)), maxFirstCol);
            }

            // Use saved precise column if scroll hasn't drifted far
            if (m_targetFirstVisCol.has_value()) {
                i64 tolerance = std::max(i64(1), maxFirstCol / 1'000'000);
                if (std::abs(scrollCol - *m_targetFirstVisCol) <= tolerance)
                    layout.firstVisCol = std::clamp(*m_targetFirstVisCol, i64(0), maxFirstCol);
                else
                    m_targetFirstVisCol.reset();
            }
            if (!m_targetFirstVisCol.has_value())
                layout.firstVisCol = scrollCol;

            layout.lastVisCol = std::min(layout.bitsPerRow, layout.firstVisCol + visibleCols);
        } else {
            layout.firstVisCol = 0;
            layout.lastVisCol = layout.bitsPerRow;
            m_targetFirstVisCol.reset();
        }

        // Keyboard input runs AFTER scroll mapping so it knows the visible range
        // and can do edge-scrolling directly on the same frame.
        handleKeyboardInput(layout, scrollMaxY, maxFirstRow);

        handleScrollToSelection(layout);

        layout.lastRow = std::min(layout.totalRows, layout.firstRow + visibleRows);

        if (layout.lastVisCol <= layout.firstVisCol) {
            ImGui::EndChild();
            return;
        }

        // Undo scroll from screen position so rows draw at small relative offsets,
        // avoiding float precision issues in the draw origin.
        const ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
        layout.drawOriginX = cursorScreenPos.x + ImGui::GetScrollX();
        layout.drawOriginY = cursorScreenPos.y + scrollY;

        readVisibleBytes(provider, layout);

        HitTestResult hit = hitTest(layout);
        handleMouseInput(layout, hit);
        drawGrid(layout);
        drawHoverTooltip(layout, hit);

        ImGui::EndChild();

        // Deferred sync: setSelection() mutates provider state and fires events
        // synchronously, so it must run after EndChild to avoid mid-draw jitter.
        if (m_needsSyncToHexEditor) {
            m_needsSyncToHexEditor = false;
            syncSelectionToHexEditor(layout.baseAddress);
        }
    }

    // --- Layout ---

    ViewBitViewer::GridLayout ViewBitViewer::computeLayout(prv::Provider *provider) {
        GridLayout layout;
        layout.baseAddress = provider->getBaseAddress();
        layout.totalBytes = provider->getActualSize();
        layout.totalBits = layout.totalBytes * 8;
        layout.cellSize = static_cast<float>(m_pixelSize);
        layout.cellStride = layout.cellSize;

        const float scrollbarW = ImGui::GetStyle().ScrollbarSize;
        const float availableW = ImGui::GetContentRegionAvail().x;

        if (m_fitToWidth)
            layout.bitsPerRow = std::max(i64(1), static_cast<i64>((availableW - scrollbarW) / layout.cellStride));
        else
            layout.bitsPerRow = std::max(i64(1), m_bitsPerRow);

        layout.totalRows = (layout.totalBits + layout.bitsPerRow - 1) / layout.bitsPerRow;

        // These are filled in later during scroll mapping
        layout.actualVisibleRows = 0;
        layout.firstRow = 0;
        layout.lastRow = 0;
        layout.firstVisCol = 0;
        layout.lastVisCol = layout.bitsPerRow;
        layout.drawOriginX = 0;
        layout.drawOriginY = 0;

        return layout;
    }

    // --- Scroll-to-selection ---

    void ViewBitViewer::handleScrollToSelection(GridLayout &layout) {
        if (!m_shouldScrollToSelection || !m_selectedRegion.has_value())
            return;

        m_shouldScrollToSelection = false;

        const u64 ubitsPerRow = static_cast<u64>(layout.bitsPerRow);
        const u64 selBit = (m_selectedRegion->getStartAddress() - layout.baseAddress) * 8;
        const u64 selRow = selBit / ubitsPerRow;
        const i64 selCol = static_cast<i64>(selBit % ubitsPerRow);

        // actualVisibleRows is computed from the parent's available size, not the
        // child's content size (which reflects SetNextWindowContentSize, not the viewport).
        const u64 visRows = layout.actualVisibleRows;

        // Vertical scroll
        if (selRow < layout.firstRow || selRow >= layout.firstRow + visRows) {
            const float scrollMaxY = ImGui::GetScrollMaxY();
            const u64 maxFirstRow = layout.totalRows > visRows ? layout.totalRows - visRows : 0;

            u64 targetFirst = selRow > visRows / 3 ? selRow - visRows / 3 : 0;
            targetFirst = std::min(targetFirst, maxFirstRow);

            if (scrollMaxY > 0 && maxFirstRow > 0) {
                float targetScroll = static_cast<float>(
                    static_cast<double>(targetFirst) / static_cast<double>(maxFirstRow)
                    * static_cast<double>(scrollMaxY));
                ImGui::SetScrollY(targetScroll);
            }
            m_targetFirstRow = targetFirst;
            layout.firstRow = targetFirst;
        }

        // Horizontal scroll (non-fitToWidth only)
        if (!m_fitToWidth && (selCol < layout.firstVisCol || selCol >= layout.lastVisCol)) {
            const float windowW = ImGui::GetWindowWidth();
            const i64 visibleCols = std::max(i64(1), static_cast<i64>(windowW / layout.cellStride)) + 2;
            const i64 maxFirstCol = std::max(i64(0), layout.bitsPerRow - visibleCols);

            i64 targetCol = std::max(i64(0), selCol - static_cast<i64>(visibleCols / 3));
            targetCol = std::min(targetCol, maxFirstCol);

            const float scrollMaxX = ImGui::GetScrollMaxX();
            if (scrollMaxX > 0 && maxFirstCol > 0) {
                float targetScroll = static_cast<float>(
                    static_cast<double>(targetCol) / static_cast<double>(maxFirstCol)
                    * static_cast<double>(scrollMaxX));
                ImGui::SetScrollX(targetScroll);
            }
            m_targetFirstVisCol = targetCol;
            layout.firstVisCol = targetCol;
            layout.lastVisCol = std::min(layout.bitsPerRow, layout.firstVisCol + visibleCols);
        }
    }

    // --- Data reading ---

    void ViewBitViewer::readVisibleBytes(prv::Provider *provider, const GridLayout &layout) {
        const u64 rowCount = layout.lastRow - layout.firstRow;
        const u64 ubitsPerRow = static_cast<u64>(layout.bitsPerRow);

        // Contiguous read bounds
        const u64 fullFirstByte = (layout.firstRow * ubitsPerRow) / 8;
        const u64 fullLastBit = std::min(layout.totalBits, layout.lastRow * ubitsPerRow);
        const u64 fullLastByte = std::min(layout.totalBytes, (fullLastBit + 7) / 8);
        const u64 fullReadSize = fullLastByte > fullFirstByte ? fullLastByte - fullFirstByte : 0;

        // When bitsPerRow is very large, visible columns are a tiny fraction of each row,
        // but rows are far apart in the byte stream. Per-row reads avoid wasting memory.
        // +1 accounts for a partial byte at the start when the first visible bit isn't byte-aligned.
        const u64 compactBytesPerRow = static_cast<u64>(layout.lastVisCol - layout.firstVisCol + 7) / 8 + 1;
        const u64 compactSize = rowCount * compactBytesPerRow;
        constexpr u64 compactReadThreshold = 8 * 1024 * 1024;
        const bool useCompact = compactSize < fullReadSize && fullReadSize > compactReadThreshold;

        // Save read parameters for readByte()
        m_useCompactRead = useCompact;
        m_compactBytesPerRow = compactBytesPerRow;
        m_compactFirstVisCol = static_cast<u64>(layout.firstVisCol);
        m_fullFirstByte = fullFirstByte;

        if (useCompact) {
            m_readBuffer.resize(compactSize);
            for (u64 i = 0; i < rowCount; i++) {
                const u64 rowBitOff = (layout.firstRow + i) * ubitsPerRow;
                const u64 startByte = (rowBitOff + m_compactFirstVisCol) / 8;
                const u64 endByte = std::min(layout.totalBytes, (rowBitOff + static_cast<u64>(layout.lastVisCol) + 7) / 8);
                const u64 count = endByte > startByte ? std::min(endByte - startByte, compactBytesPerRow) : 0;
                if (count > 0)
                    provider->read(layout.baseAddress + startByte, m_readBuffer.data() + i * compactBytesPerRow, count);
            }
        } else {
            if (fullReadSize == 0) {
                m_readBuffer.clear();
                return;
            }
            m_readBuffer.resize(fullReadSize);
            provider->read(layout.baseAddress + fullFirstByte, m_readBuffer.data(), fullReadSize);
        }
    }

    u8 ViewBitViewer::readByte(const GridLayout &layout, u64 row, u64 globalByteIdx) const {
        if (m_useCompactRead) {
            u64 ubitsPerRow = static_cast<u64>(layout.bitsPerRow);
            u64 rowStartByte = (row * ubitsPerRow + m_compactFirstVisCol) / 8;
            u64 idx = (row - layout.firstRow) * m_compactBytesPerRow + (globalByteIdx - rowStartByte);
            return idx < m_readBuffer.size() ? m_readBuffer[idx] : 0;
        }
        return m_readBuffer[globalByteIdx - m_fullFirstByte];
    }

    // --- Hit testing ---

    ViewBitViewer::HitTestResult ViewBitViewer::hitTest(const GridLayout &layout) const {
        HitTestResult result;

        const ImVec2 mousePos = ImGui::GetMousePos();
        if (!ImGui::IsWindowHovered() || layout.cellStride <= 0)
            return result;

        const float localX = mousePos.x - layout.drawOriginX;
        const float localY = mousePos.y - layout.drawOriginY;
        if (localX < 0 || localY < 0)
            return result;

        const i64 col = layout.firstVisCol + static_cast<i64>(localX / layout.cellStride);
        const u64 relRow = static_cast<u64>(localY / layout.cellStride);

        // Check that the mouse is within the cell, not in the gap
        const float fracX = localX - static_cast<float>(col - layout.firstVisCol) * layout.cellStride;
        const float fracY = localY - static_cast<float>(relRow) * layout.cellStride;
        if (fracX < 0 || fracX >= layout.cellSize || fracY < 0 || fracY >= layout.cellSize)
            return result;

        const u64 row = layout.firstRow + relRow;
        if (col < layout.firstVisCol || col >= layout.lastVisCol || row >= layout.lastRow)
            return result;

        const u64 bitIdx = row * static_cast<u64>(layout.bitsPerRow) + col;
        if (bitIdx >= layout.totalBits)
            return result;

        result.valid = true;
        result.row = row;
        result.col = col;
        result.byteIdx = bitIdx / 8;
        return result;
    }

    // --- Input handling ---

    void ViewBitViewer::syncSelectionToHexEditor(u64 baseAddress) {
        u64 start = std::min(m_selectionAnchor, m_cursorOffset);
        u64 end = std::max(m_selectionAnchor, m_cursorOffset);
        m_suppressNextExternalEvent = true;
        ImHexApi::HexEditor::setSelection(baseAddress + start, end - start + 1);
    }

    void ViewBitViewer::handleMouseInput(const GridLayout &layout, const HitTestResult &hit) {
        if (!hit.valid)
            return;

        const bool shift = ImGui::GetIO().KeyShift;
        bool changed = false;

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (shift && m_hasCursor) {
                m_cursorOffset = hit.byteIdx;
            } else {
                m_selectionAnchor = hit.byteIdx;
                m_cursorOffset = hit.byteIdx;
            }
            m_hasCursor = true;
            changed = true;
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            if (m_hasCursor && m_cursorOffset != hit.byteIdx) {
                m_cursorOffset = hit.byteIdx;
                changed = true;
            }
        }

        if (changed) {
            u64 start = std::min(m_selectionAnchor, m_cursorOffset);
            u64 end = std::max(m_selectionAnchor, m_cursorOffset);
            m_selectedRegion = Region{layout.baseAddress + start, end - start + 1};
            m_needsSyncToHexEditor = true;
        }
    }

    void ViewBitViewer::handleKeyboardInput(GridLayout &layout, float scrollMaxY, u64 maxFirstRow) {
        if (!ImGui::IsWindowFocused() || !m_hasCursor || layout.totalBytes == 0)
            return;

        const bool shift = ImGui::GetIO().KeyShift;
        const u64 ubitsPerRow = static_cast<u64>(layout.bitsPerRow);
        bool moved = false;
        u64 newOffset = m_cursorOffset;

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) && m_cursorOffset > 0) {
            newOffset = m_cursorOffset - 1;
            moved = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) && m_cursorOffset < layout.totalBytes - 1) {
            newOffset = m_cursorOffset + 1;
            moved = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) {
            u64 curBit = m_cursorOffset * 8;
            if (curBit >= ubitsPerRow) {
                newOffset = (curBit - ubitsPerRow) / 8;
                moved = true;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) {
            u64 targetBit = m_cursorOffset * 8 + ubitsPerRow;
            if (targetBit / 8 < layout.totalBytes) {
                newOffset = targetBit / 8;
                moved = true;
            }
        }

        if (moved) {
            m_cursorOffset = newOffset;
            if (!shift)
                m_selectionAnchor = m_cursorOffset;

            u64 start = std::min(m_selectionAnchor, m_cursorOffset);
            u64 end = std::max(m_selectionAnchor, m_cursorOffset);
            m_selectedRegion = Region{layout.baseAddress + start, end - start + 1};

            m_needsSyncToHexEditor = true;

            // Cancel pending scroll-to-selection so the deferred EventRegionSelected
            // from our sync doesn't re-center the view and fight edge-scroll.
            m_shouldScrollToSelection = false;

            // Edge-scroll: keep cursor visible by adjusting firstRow minimally,
            // rather than centering via handleScrollToSelection.
            const u64 cursorRow = (m_cursorOffset * 8) / ubitsPerRow;
            bool scrolled = false;

            if (cursorRow < layout.firstRow) {
                layout.firstRow = cursorRow;
                scrolled = true;
            } else if (cursorRow >= layout.firstRow + layout.actualVisibleRows) {
                layout.firstRow = cursorRow - layout.actualVisibleRows + 1;
                scrolled = true;
            }

            if (scrolled) {
                layout.firstRow = std::min(layout.firstRow, maxFirstRow);
                m_targetFirstRow = layout.firstRow;
                if (scrollMaxY > 0 && maxFirstRow > 0) {
                    float targetScroll = static_cast<float>(
                        static_cast<double>(layout.firstRow) / static_cast<double>(maxFirstRow)
                        * static_cast<double>(scrollMaxY));
                    ImGui::SetScrollY(targetScroll);
                }
            }
        }
    }

    // --- Rendering ---

    void ViewBitViewer::drawGrid(const GridLayout &layout) {
        if (m_readBuffer.empty())
            return;

        ImDrawList *drawList = ImGui::GetWindowDrawList();

        const ImU32 colZero  = ImGui::ColorConvertFloat4ToU32(m_colorZero);
        const ImU32 colOne   = ImGui::ColorConvertFloat4ToU32(m_colorOne);
        const ImU32 colSelFill    = ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 0.5f, 1.0f, 0.35f));
        const ImU32 colSelBorder = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.6f, 1.0f, 0.9f));
        const ImU32 colCurFill   = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.3f));
        const ImU32 colCurBorder = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
        const bool useBorders = layout.cellSize >= 3.0f;
        const bool msbFirst = (m_bitOrder == BitOrder::MsbFirst);

        u64 selStart = 0, selEnd = 0;
        const bool hasSel = m_selectedRegion.has_value();
        if (hasSel) {
            selStart = m_selectedRegion->getStartAddress() - layout.baseAddress;
            selEnd = selStart + m_selectedRegion->getSize();
        }

        const u64 ubitsPerRow = static_cast<u64>(layout.bitsPerRow);

        for (u64 row = layout.firstRow; row < layout.lastRow; row++) {
            const float y = layout.drawOriginY + static_cast<float>(row - layout.firstRow) * layout.cellStride;
            const u64 rowBitStart = row * ubitsPerRow;

            for (i64 col = layout.firstVisCol; col < layout.lastVisCol; col++) {
                const u64 bitIdx = rowBitStart + col;
                if (bitIdx >= layout.totalBits) break;

                const u64 byteIdx = bitIdx / 8;
                const int bitPos = static_cast<int>(bitIdx % 8);
                const int shift = msbFirst ? (7 - bitPos) : bitPos;
                const bool val = (readByte(layout, row, byteIdx) >> shift) & 1;

                const float x = layout.drawOriginX + static_cast<float>(col - layout.firstVisCol) * layout.cellStride;
                const ImVec2 cellMin(x, y);
                const ImVec2 cellMax(x + layout.cellSize, y + layout.cellSize);

                drawList->AddRectFilled(cellMin, cellMax, val ? colOne : colZero);

                if (hasSel && byteIdx >= selStart && byteIdx < selEnd) {
                    drawList->AddRectFilled(cellMin, cellMax, colSelFill);
                    if (useBorders)
                        drawList->AddRect(cellMin, cellMax, colSelBorder);
                }

                if (m_hasCursor && byteIdx == m_cursorOffset) {
                    drawList->AddRectFilled(cellMin, cellMax, colCurFill);
                    if (useBorders)
                        drawList->AddRect(cellMin, cellMax, colCurBorder);
                }
            }
        }
    }

    void ViewBitViewer::drawHoverTooltip(const GridLayout &layout, const HitTestResult &hit) {
        if (!hit.valid)
            return;

        const bool msbFirst = (m_bitOrder == BitOrder::MsbFirst);
        const u64 ubitsPerRow = static_cast<u64>(layout.bitsPerRow);
        const u64 bitIdx = hit.row * ubitsPerRow + hit.col;
        const int bitPos = static_cast<int>(bitIdx % 8);
        const int shift = msbFirst ? (7 - bitPos) : bitPos;
        const bool val = (readByte(layout, hit.row, hit.byteIdx) >> shift) & 1;

        // Draw hover highlight
        const float x = layout.drawOriginX + static_cast<float>(hit.col - layout.firstVisCol) * layout.cellStride;
        const float y = layout.drawOriginY + static_cast<float>(hit.row - layout.firstRow) * layout.cellStride;
        const ImVec2 cellMin(x, y);
        const ImVec2 cellMax(x + layout.cellSize, y + layout.cellSize);

        const ImU32 colHover = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 0.0f, 0.7f));
        ImGui::GetWindowDrawList()->AddRect(cellMin, cellMax, colHover, 0.0f, 0, 2.0f);

        ImGui::BeginTooltip();
        ImGuiExt::TextFormatted("hex.bitview.tooltip.byte"_lang, layout.baseAddress + hit.byteIdx);
        if (msbFirst)
            ImGuiExt::TextFormatted("hex.bitview.tooltip.bit_msb"_lang, shift);
        else
            ImGuiExt::TextFormatted("hex.bitview.tooltip.bit_lsb"_lang, shift);
        ImGuiExt::TextFormatted("hex.bitview.tooltip.value"_lang, val ? 1 : 0);
        ImGui::EndTooltip();
    }

}
