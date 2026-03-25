#if defined(_WIN32)

#include "D3DTextRendererImpl.h"

#include <algorithm>
#include <string>

namespace termcore {

// Map AgentState int to a status dot color
static uint32_t sidebarStateColor(int state) {
    switch (state) {
        case 3: /* Running */  return 0xEAB308; // yellow
        case 4: /* Thinking */ return 0xEAB308;
        case 5: /* ToolUse */  return 0xF97316; // orange
        case 6: /* Waiting */  return 0x3B82F6; // blue
        case 7: /* Error */    return 0xEF4444; // red
        case 2: /* Idle */     return 0x22C55E; // green
        case 8: /* Exited */   return 0x22C55E;
        default: return 0x666666; // gray
    }
}

// Helper: render a string of ASCII text at pixel position, returns X advance.
// Implemented as a member function wrapper to avoid accessing private Impl type.
float D3DTextRenderer::Impl::renderSidebarText(
        const std::string& text,
        float startX, float baseY, float ascent_,
        float cellW_, float fontSize_,
        uint32_t color, float maxX) {
    float x = startX;
    for (size_t i = 0; i < text.size(); ++i) {
        char32_t cp = static_cast<char32_t>(static_cast<unsigned char>(text[i]));
        if (cp == ' ' || cp == 0) { x += cellW_ * 0.55f; continue; }

        CollectionFaceId faceId = fontCollection->resolveFace(cp);
        if (faceId == kInvalidCollectionFace) { x += cellW_ * 0.55f; continue; }
        FontFaceId rastFace = fontCollection->rasterizerFaceId(faceId);
        uint32_t glyphIdx = rasterizer->getGlyphIndex(rastFace, cp);
        if (glyphIdx == 0) { x += cellW_ * 0.55f; continue; }

        GlyphKey key{rastFace, glyphIdx, {0, 0}};
        auto info = glyphCache->getOrRasterize(
            key, fontSize_, *rasterizer, *glyphAtlas);
        if (!info || info->region.width <= 0) { x += cellW_ * 0.55f; continue; }
        if (x + info->region.width > maxX) break;

        D3DCellInstance inst = {};
        inst.position[0] = x + info->region.bearing_x;
        inst.position[1] = baseY + ascent_ - info->region.bearing_y;
        inst.atlas_uv[0] = static_cast<float>(info->region.x);
        inst.atlas_uv[1] = static_cast<float>(info->region.y);
        inst.atlas_size[0] = static_cast<float>(info->region.width);
        inst.atlas_size[1] = static_cast<float>(info->region.height);
        colorFromRGBA(color | 0xFF000000, inst.fg_color);
        inst.flags = 1; // has_glyph
        cellInstances.push_back(inst);
        x += cellW_ * 0.55f;
    }
    return x - startX;
}

void D3DTextRenderer::Impl::buildSidebarOverlay(float cellW, float cellH,
                                                  float ascent,
                                                  float fontSize) {
    const auto& sb = this->sidebar;
    if (!sb.visible || sb.entries.empty()) return;
    if (!fontCollection || !rasterizer || !glyphCache || !glyphAtlas) return;

    float sidebarW = static_cast<float>(sb.width);
    float padX = 8.0f;
    float entryPadY = 6.0f;
    float titleH = cellH;
    float subtitleH = cellH * 0.85f;
    float progressH = 6.0f;
    float subagentH = cellH * 0.85f;
    float dotSize = 6.0f;
    float separatorH = 1.0f;

    // Tab bar offset
    float tabBarH = cellH * D3DTextRenderer::kTabBarHeightScale;
    float topY = (tabBar.visible && !tabBar.tabs.empty()) ? tabBarH : 0.0f;

    // 1. Sidebar background (full height)
    {
        D3DCellInstance bg = {};
        bg.position[0] = 0;
        bg.position[1] = topY;
        bg.atlas_size[0] = sidebarW;
        bg.atlas_size[1] = viewportHeight - topY;
        D3DTextRenderer::Impl::colorFromRGBA(sb.bg_color | 0xFF000000, bg.bg_color);
        bg.flags = 8; // solid tint
        cellInstances.push_back(bg);
    }

    // Right edge separator
    {
        D3DCellInstance sep = {};
        sep.position[0] = sidebarW - 1.0f;
        sep.position[1] = topY;
        sep.atlas_size[0] = 1.0f;
        sep.atlas_size[1] = viewportHeight - topY;
        D3DTextRenderer::Impl::colorFromRGBA(sb.separator_color | 0xFF000000, sep.bg_color);
        sep.flags = 8;
        cellInstances.push_back(sep);
    }

    float curY = topY + entryPadY - static_cast<float>(sb.scroll_offset);
    float maxTextX = sidebarW - padX;
    float bottomY = viewportHeight;

    for (int ei = 0; ei < static_cast<int>(sb.entries.size()); ++ei) {
        const auto& entry = sb.entries[ei];

        // Skip entries fully above visible area
        float entryStartY = curY;
        if (curY > bottomY) break; // past bottom

        // Entry highlight background (active or hovered)
        if (entry.active || ei == sb.hovered_entry) {
            uint32_t hlColor = entry.active ? 0x2a2d2e : 0x242424;
            // Compute approximate entry height for highlight
            float approxH = entryPadY + titleH;
            if (!entry.subtitle.empty()) approxH += subtitleH;
            if (entry.progress_value >= 0.0f) approxH += progressH + 4.0f;
            if (!entry.status_text.empty()) approxH += subtitleH;
            if (entry.subagents_expanded) {
                approxH += entry.subagents.size() * subagentH;
            }
            approxH += entryPadY;

            D3DCellInstance hl = {};
            hl.position[0] = 0;
            hl.position[1] = curY;
            hl.atlas_size[0] = sidebarW - 1.0f;
            hl.atlas_size[1] = approxH;
            D3DTextRenderer::Impl::colorFromRGBA(hlColor | 0xFF000000, hl.bg_color);
            hl.flags = 8;
            cellInstances.push_back(hl);
        }

        // a. Title text + state color dot on right
        if (curY + titleH > topY) {
            // Title text
            renderSidebarText(entry.title,
                              padX, curY, ascent, cellW, fontSize,
                              sb.fg_color, maxTextX - dotSize - 4.0f);

            // State color dot (right-aligned)
            uint32_t dotColor = sidebarStateColor(entry.agent_state);
            D3DCellInstance dot = {};
            dot.position[0] = sidebarW - padX - dotSize;
            dot.position[1] = curY + (titleH - dotSize) * 0.5f;
            dot.atlas_size[0] = dotSize;
            dot.atlas_size[1] = dotSize;
            D3DTextRenderer::Impl::colorFromRGBA(dotColor | 0xFF000000, dot.bg_color);
            dot.flags = 8;
            cellInstances.push_back(dot);
        }
        curY += titleH;

        // b. Subtitle text (dimmer)
        if (!entry.subtitle.empty()) {
            uint32_t dimColor = 0x808080;
            if (curY + subtitleH > topY && curY < bottomY) {
                renderSidebarText(entry.subtitle,
                                  padX, curY, ascent * 0.85f, cellW, fontSize,
                                  dimColor, maxTextX);
            }
            curY += subtitleH;
        }

        // c. Progress bar (if progress_value >= 0)
        if (entry.progress_value >= 0.0f) {
            curY += 2.0f;
            float barW = sidebarW - padX * 2.0f;
            float filledW = barW * (std::max)(0.0f, (std::min)(1.0f, entry.progress_value));

            if (curY + progressH > topY && curY < bottomY) {
                // Track
                D3DCellInstance track = {};
                track.position[0] = padX;
                track.position[1] = curY;
                track.atlas_size[0] = barW;
                track.atlas_size[1] = progressH;
                track.bg_color[0] = 0.2f; track.bg_color[1] = 0.2f;
                track.bg_color[2] = 0.2f; track.bg_color[3] = 1.0f;
                track.flags = 8;
                cellInstances.push_back(track);

                // Filled portion
                if (filledW > 0.0f) {
                    D3DCellInstance filled = {};
                    filled.position[0] = padX;
                    filled.position[1] = curY;
                    filled.atlas_size[0] = filledW;
                    filled.atlas_size[1] = progressH;
                    D3DTextRenderer::Impl::colorFromRGBA(sb.accent_color | 0xFF000000, filled.bg_color);
                    filled.flags = 8;
                    cellInstances.push_back(filled);
                }

                // Progress label (right of bar)
                if (!entry.progress_label.empty()) {
                    float labelX = padX + barW + 4.0f;
                    if (labelX < maxTextX) {
                        renderSidebarText(entry.progress_label,
                                          padX, curY + progressH + 1.0f,
                                          ascent * 0.75f, cellW, fontSize,
                                          0x808080, maxTextX);
                    }
                }
            }
            curY += progressH + 2.0f;
        }

        // d. Status text (e.g., "Thinking... (8s)")
        if (!entry.status_text.empty()) {
            if (curY + subtitleH > topY && curY < bottomY) {
                uint32_t statusColor = sidebarStateColor(entry.agent_state);
                renderSidebarText(entry.status_text,
                                  padX, curY, ascent * 0.85f, cellW, fontSize,
                                  statusColor, maxTextX);
            }
            curY += subtitleH;
        }

        // e. Subagent tree entries (if expanded)
        if (entry.subagents_expanded && !entry.subagents.empty()) {
            for (int si = 0; si < static_cast<int>(entry.subagents.size()); ++si) {
                const auto& sub = entry.subagents[si];
                if (curY > bottomY) break;

                if (curY + subagentH > topY) {
                    float indent = padX + 12.0f * (1 + sub.indent_level);

                    // Tree connector character
                    bool isLast = (si == static_cast<int>(entry.subagents.size()) - 1);
                    std::string connector = isLast ? "`-" : "|-";
                    renderSidebarText(connector,
                                      padX + 4.0f, curY, ascent * 0.85f,
                                      cellW, fontSize, 0x555555, indent);

                    // Subagent name
                    float nameEndX = maxTextX - dotSize - 4.0f;
                    renderSidebarText(sub.name,
                                      indent, curY, ascent * 0.85f,
                                      cellW, fontSize, 0xaaaaaa, nameEndX);

                    // Status text (e.g., "[Running]")
                    if (!sub.status.empty()) {
                        uint32_t subStatusColor = sidebarStateColor(sub.state);
                        // Right-aligned status before dot
                        float statusW = sub.status.size() * cellW * 0.55f;
                        float statusX = sidebarW - padX - dotSize - 4.0f - statusW;
                        if (statusX > indent) {
                            renderSidebarText(sub.status,
                                              statusX, curY, ascent * 0.85f,
                                              cellW, fontSize, subStatusColor, maxTextX);
                        }
                    }

                    // Subagent state dot
                    uint32_t subDotColor = sidebarStateColor(sub.state);
                    D3DCellInstance subDot = {};
                    subDot.position[0] = sidebarW - padX - dotSize;
                    subDot.position[1] = curY + (subagentH - dotSize) * 0.5f;
                    subDot.atlas_size[0] = dotSize * 0.8f;
                    subDot.atlas_size[1] = dotSize * 0.8f;
                    D3DTextRenderer::Impl::colorFromRGBA(subDotColor | 0xFF000000, subDot.bg_color);
                    subDot.flags = 8;
                    cellInstances.push_back(subDot);

                    // Hover highlight for subagent
                    if (ei == sb.hovered_entry && si == sb.hovered_subagent) {
                        D3DCellInstance subHl = {};
                        subHl.position[0] = 0;
                        subHl.position[1] = curY;
                        subHl.atlas_size[0] = sidebarW - 1.0f;
                        subHl.atlas_size[1] = subagentH;
                        subHl.bg_color[0] = 1.0f; subHl.bg_color[1] = 1.0f;
                        subHl.bg_color[2] = 1.0f; subHl.bg_color[3] = 0.05f;
                        subHl.flags = 8;
                        cellInstances.push_back(subHl);
                    }
                }
                curY += subagentH;
            }
        }

        // Unread indicator dot (top-left of entry)
        if (entry.has_unread && !entry.active) {
            D3DCellInstance unread = {};
            unread.position[0] = 2.0f;
            unread.position[1] = entryStartY + 4.0f;
            unread.atlas_size[0] = 4.0f;
            unread.atlas_size[1] = 4.0f;
            D3DTextRenderer::Impl::colorFromRGBA(sb.accent_color | 0xFF000000, unread.bg_color);
            unread.flags = 8;
            cellInstances.push_back(unread);
        }

        // Attention glow border (left edge pulse)
        if (entry.attention_intensity > 0.0f) {
            D3DCellInstance glow = {};
            glow.position[0] = 0;
            glow.position[1] = entryStartY;
            glow.atlas_size[0] = 2.0f;
            glow.atlas_size[1] = curY - entryStartY;
            float r = static_cast<float>((sb.accent_color >> 16) & 0xFF) / 255.0f;
            float g = static_cast<float>((sb.accent_color >> 8) & 0xFF) / 255.0f;
            float b = static_cast<float>(sb.accent_color & 0xFF) / 255.0f;
            float a = entry.attention_intensity;
            glow.bg_color[0] = r * a;
            glow.bg_color[1] = g * a;
            glow.bg_color[2] = b * a;
            glow.bg_color[3] = a;
            glow.flags = 8;
            cellInstances.push_back(glow);
        }

        curY += entryPadY;

        // f. Separator line between entries
        if (ei < static_cast<int>(sb.entries.size()) - 1) {
            if (curY > topY && curY < bottomY) {
                D3DCellInstance sep = {};
                sep.position[0] = padX;
                sep.position[1] = curY;
                sep.atlas_size[0] = sidebarW - padX * 2.0f;
                sep.atlas_size[1] = separatorH;
                D3DTextRenderer::Impl::colorFromRGBA(sb.separator_color | 0xFF000000, sep.bg_color);
                sep.flags = 8;
                cellInstances.push_back(sep);
            }
            curY += separatorH + entryPadY;
        }
    }
}

} // namespace termcore

#endif // _WIN32
