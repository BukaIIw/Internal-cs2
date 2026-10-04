#pragma once
#include "imgui.h"
#include <cmath>

namespace ui
{
    namespace ratio
    {
        constexpr float item_image = 0.66f;
        constexpr float icon_aspect = 0.75f;
        constexpr float combo_width = 0.52f;
        constexpr float input_width = 0.5f;
        constexpr float card_glow = 0.42f;
        constexpr float card_bar_idle = 0.35f;
        constexpr float title_split = 0.5f;
        constexpr float events_columns[6] = { 0.f, 0.2f, 0.48f, 0.62f, 0.75f, 0.88f };
        constexpr float hooks_columns[4] = { 0.f, 0.38f, 0.66f, 0.84f };
        constexpr float generator_field = 0.35f;
    }

    struct metrics
    {
        float scale = 1.f;

        float space_xxs = 2.f;
        float space_xs = 4.f;
        float space_sm = 8.f;
        float space_md = 12.f;
        float space_lg = 16.f;
        float space_xl = 24.f;

        float radius_xs = 5.f;
        float radius_sm = 6.f;
        float radius_md = 8.f;
        float radius_frame = 7.f;
        float radius_card = 10.f;
        float radius_lg = 12.f;
        float radius_xl = 14.f;

        float stroke = 2.f;
        float stroke_thin = 1.6f;
        float stroke_nav = 1.9f;
        float stroke_toast = 2.2f;
        float hairline = 1.f;

        float control = 28.f;
        float row = 32.f;
        float row_dense = 24.f;
        float row_gap = 6.f;
        float item_gap = 8.f;
        float inner_gap_x = 6.f;
        float inner_gap_y = 4.f;
        float list_item = 24.f;

        float switch_width = 32.f;
        float switch_height = 18.f;
        float switch_knob_inset = 3.f;
        float gear = 17.f;
        float gear_gap = 12.f;
        float key_badge_height = 18.f;
        float key_badge_pad = 6.f;

        float slider_label = 24.f;
        float slider_hit = 16.f;
        float slider_line = 4.f;
        float slider_knob = 6.f;
        float slider_glow = 10.f;
        float slider_glow_grow = 7.f;

        float combo_min = 130.f;
        float swatch_width = 30.f;
        float swatch_height = 16.f;
        float checker = 4.f;
        float color_picker = 200.f;

        float segmented_height = 30.f;
        float segmented_inset = 3.f;
        float segmented_radius = 9.f;
        float tab_width = 84.f;

        float chip_pad = 13.f;
        float search_pad = 34.f;
        float key_button = 140.f;
        float button_height = 32.f;
        float button_small = 28.f;
        float button_wide = 120.f;

        float card_pad_x = 14.f;
        float card_pad_y = 12.f;
        float card_header = 24.f;
        float card_gap = 12.f;

        float icon_sm = 14.f;
        float icon_md = 16.f;
        float icon_lg = 20.f;
        float icon_xl = 24.f;
        float icon_nav = 22.f;
        float icon_empty = 48.f;
        float icon_placeholder = 26.f;

        float window_width = 860.f;
        float window_height = 580.f;
        float window_radius = 12.f;
        float window_shadow = 34.f;
        float window_shadow_inset = 4.f;
        float window_shadow_drop = 10.f;
        float window_shadow_tail = 6.f;
        float window_slide = 14.f;
        float rail = 64.f;
        float header = 72.f;
        float logo = 32.f;
        float logo_x = 16.f;
        float logo_y = 18.f;
        float nav_item = 40.f;
        float nav_step = 52.f;
        float nav_top = 96.f;
        float nav_bottom = 40.f;
        float nav_radius = 11.f;
        float nav_bar_width = 3.f;
        float nav_bar_height = 18.f;
        float tooltip_offset = 14.f;
        float tooltip_slide = 6.f;
        float tooltip_height = 26.f;
        float tooltip_pad = 9.f;
        float title_y = 15.f;
        float subtitle_y = 42.f;
        float tabs_y = 20.f;
        float content_pad_x = 22.f;
        float content_pad_y = 16.f;
        float page_slide = 14.f;

        float popover_width = 270.f;
        float popover_pad_x = 14.f;
        float popover_pad_y = 12.f;
        float popup_pad = 10.f;
        float scrollbar = 6.f;
        float grab_min = 10.f;
        float cell_pad_x = 5.f;

        float toast_width = 310.f;
        float toast_height = 46.f;
        float toast_margin = 24.f;
        float toast_gap = 8.f;
        float toast_icon_box = 24.f;
        float toast_icon_x = 12.f;
        float toast_text_x = 46.f;
        float toast_slide = 40.f;
        float toast_shadow = 14.f;

        float status_dot = 3.5f;
        float status_text = 16.f;

        float item_card_min = 158.f;
        float item_card_gap = 10.f;
        float item_card_text = 52.f;
        float item_card_pad = 11.f;
        float item_card_lift = 2.f;
        float item_card_bar = 2.f;
        float item_card_shadow = 16.f;
        float badge_height = 17.f;
        float badge_pad = 5.f;
        float badge_gap = 4.f;
        float badge_inset = 9.f;

        float draft_width = 660.f;
        float draft_pad = 18.f;
        float draft_preview_width = 250.f;
        float draft_preview_height = 210.f;
        float draft_glow = 115.f;
        float draft_gap = 18.f;
        float sticker_slot = 72.f;
        float sticker_gap = 10.f;
        float draft_button_width = 170.f;
        float draft_button_height = 36.f;
        float inventory_popup = 280.f;

        float sticker_picker_width = 440.f;
        float sticker_picker_height = 420.f;
        float sticker_row = 42.f;
        float sticker_icon = 48.f;
        float sticker_text = 62.f;

        float weapon_combo = 200.f;
        float table_header = 20.f;
        float table_row = 26.f;
        float table_height = 340.f;
        float pattern_row = 36.f;
        float pattern_row_generated = 52.f;
        float pattern_button = 60.f;
        float pattern_button_height = 24.f;
        float pattern_list = 250.f;
        float log_list = 330.f;
        float log_age = 44.f;
        float empty_icon_y = 60.f;
        float empty_text_y = 122.f;

        float preset_swatch = 22.f;
        float preset_step = 30.f;
        float preset_hover = 1.f;

        float font_body = 15.f;
        float font_strong = 15.f;
        float font_compact = 13.f;
        float font_caption = 12.f;
        float font_badge = 11.f;
        float font_title = 18.f;
        float font_display = 21.f;
        float font_mono = 12.f;

        metrics scaled(float s) const
        {
            const auto px = [s](float v) { return std::fmax(1.f, std::round(v * s)); };
            const auto fine = [s](float v) { return v * s; };
            metrics r = *this;
            r.scale = s;
            r.space_xxs = px(space_xxs);
            r.space_xs = px(space_xs);
            r.space_sm = px(space_sm);
            r.space_md = px(space_md);
            r.space_lg = px(space_lg);
            r.space_xl = px(space_xl);
            r.radius_xs = px(radius_xs);
            r.radius_sm = px(radius_sm);
            r.radius_md = px(radius_md);
            r.radius_frame = px(radius_frame);
            r.radius_card = px(radius_card);
            r.radius_lg = px(radius_lg);
            r.radius_xl = px(radius_xl);
            r.stroke = fine(stroke);
            r.stroke_thin = fine(stroke_thin);
            r.stroke_nav = fine(stroke_nav);
            r.stroke_toast = fine(stroke_toast);
            r.hairline = px(hairline);
            r.control = px(control);
            r.row = px(row);
            r.row_dense = px(row_dense);
            r.row_gap = px(row_gap);
            r.item_gap = px(item_gap);
            r.inner_gap_x = px(inner_gap_x);
            r.inner_gap_y = px(inner_gap_y);
            r.list_item = px(list_item);
            r.switch_width = px(switch_width);
            r.switch_height = px(switch_height);
            r.switch_knob_inset = px(switch_knob_inset);
            r.gear = px(gear);
            r.gear_gap = px(gear_gap);
            r.key_badge_height = px(key_badge_height);
            r.key_badge_pad = px(key_badge_pad);
            r.slider_label = px(slider_label);
            r.slider_hit = px(slider_hit);
            r.slider_line = px(slider_line);
            r.slider_knob = px(slider_knob);
            r.slider_glow = px(slider_glow);
            r.slider_glow_grow = px(slider_glow_grow);
            r.combo_min = px(combo_min);
            r.swatch_width = px(swatch_width);
            r.swatch_height = px(swatch_height);
            r.checker = px(checker);
            r.color_picker = px(color_picker);
            r.segmented_height = px(segmented_height);
            r.segmented_inset = px(segmented_inset);
            r.segmented_radius = px(segmented_radius);
            r.tab_width = px(tab_width);
            r.chip_pad = px(chip_pad);
            r.search_pad = px(search_pad);
            r.key_button = px(key_button);
            r.button_height = px(button_height);
            r.button_small = px(button_small);
            r.button_wide = px(button_wide);
            r.card_pad_x = px(card_pad_x);
            r.card_pad_y = px(card_pad_y);
            r.card_header = px(card_header);
            r.card_gap = px(card_gap);
            r.icon_sm = px(icon_sm);
            r.icon_md = px(icon_md);
            r.icon_lg = px(icon_lg);
            r.icon_xl = px(icon_xl);
            r.icon_nav = px(icon_nav);
            r.icon_empty = px(icon_empty);
            r.icon_placeholder = px(icon_placeholder);
            r.window_width = px(window_width);
            r.window_height = px(window_height);
            r.window_radius = px(window_radius);
            r.window_shadow = px(window_shadow);
            r.window_shadow_inset = px(window_shadow_inset);
            r.window_shadow_drop = px(window_shadow_drop);
            r.window_shadow_tail = px(window_shadow_tail);
            r.window_slide = px(window_slide);
            r.rail = px(rail);
            r.header = px(header);
            r.logo = px(logo);
            r.logo_x = px(logo_x);
            r.logo_y = px(logo_y);
            r.nav_item = px(nav_item);
            r.nav_step = px(nav_step);
            r.nav_top = px(nav_top);
            r.nav_bottom = px(nav_bottom);
            r.nav_radius = px(nav_radius);
            r.nav_bar_width = px(nav_bar_width);
            r.nav_bar_height = px(nav_bar_height);
            r.tooltip_offset = px(tooltip_offset);
            r.tooltip_slide = px(tooltip_slide);
            r.tooltip_height = px(tooltip_height);
            r.tooltip_pad = px(tooltip_pad);
            r.title_y = px(title_y);
            r.subtitle_y = px(subtitle_y);
            r.tabs_y = px(tabs_y);
            r.content_pad_x = px(content_pad_x);
            r.content_pad_y = px(content_pad_y);
            r.page_slide = px(page_slide);
            r.popover_width = px(popover_width);
            r.popover_pad_x = px(popover_pad_x);
            r.popover_pad_y = px(popover_pad_y);
            r.popup_pad = px(popup_pad);
            r.scrollbar = px(scrollbar);
            r.grab_min = px(grab_min);
            r.cell_pad_x = px(cell_pad_x);
            r.toast_width = px(toast_width);
            r.toast_height = px(toast_height);
            r.toast_margin = px(toast_margin);
            r.toast_gap = px(toast_gap);
            r.toast_icon_box = px(toast_icon_box);
            r.toast_icon_x = px(toast_icon_x);
            r.toast_text_x = px(toast_text_x);
            r.toast_slide = px(toast_slide);
            r.toast_shadow = px(toast_shadow);
            r.status_dot = fine(status_dot);
            r.status_text = px(status_text);
            r.item_card_min = px(item_card_min);
            r.item_card_gap = px(item_card_gap);
            r.item_card_text = px(item_card_text);
            r.item_card_pad = px(item_card_pad);
            r.item_card_lift = px(item_card_lift);
            r.item_card_bar = px(item_card_bar);
            r.item_card_shadow = px(item_card_shadow);
            r.badge_height = px(badge_height);
            r.badge_pad = px(badge_pad);
            r.badge_gap = px(badge_gap);
            r.badge_inset = px(badge_inset);
            r.draft_width = px(draft_width);
            r.draft_pad = px(draft_pad);
            r.draft_preview_width = px(draft_preview_width);
            r.draft_preview_height = px(draft_preview_height);
            r.draft_glow = px(draft_glow);
            r.draft_gap = px(draft_gap);
            r.sticker_slot = px(sticker_slot);
            r.sticker_gap = px(sticker_gap);
            r.draft_button_width = px(draft_button_width);
            r.draft_button_height = px(draft_button_height);
            r.inventory_popup = px(inventory_popup);
            r.sticker_picker_width = px(sticker_picker_width);
            r.sticker_picker_height = px(sticker_picker_height);
            r.sticker_row = px(sticker_row);
            r.sticker_icon = px(sticker_icon);
            r.sticker_text = px(sticker_text);
            r.weapon_combo = px(weapon_combo);
            r.table_header = px(table_header);
            r.table_row = px(table_row);
            r.table_height = px(table_height);
            r.pattern_row = px(pattern_row);
            r.pattern_row_generated = px(pattern_row_generated);
            r.pattern_button = px(pattern_button);
            r.pattern_button_height = px(pattern_button_height);
            r.pattern_list = px(pattern_list);
            r.log_list = px(log_list);
            r.log_age = px(log_age);
            r.empty_icon_y = px(empty_icon_y);
            r.empty_text_y = px(empty_text_y);
            r.preset_swatch = px(preset_swatch);
            r.preset_step = px(preset_step);
            r.preset_hover = px(preset_hover);
            r.font_body = px(font_body);
            r.font_strong = px(font_strong);
            r.font_compact = px(font_compact);
            r.font_caption = px(font_caption);
            r.font_badge = px(font_badge);
            r.font_title = px(font_title);
            r.font_display = px(font_display);
            r.font_mono = px(font_mono);
            return r;
        }
    };

    inline const metrics g_base_metrics{};
    inline metrics g_metrics{};

    inline const metrics& m()
    {
        return g_metrics;
    }
}
