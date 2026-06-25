/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "lvgl.h"
#include <stdio.h>

#define PANEL_BG_COLOR      lv_color_hex(0x07111F)
#define CARD_COLOR          lv_color_hex(0x0D1B2A)
#define CARD_BORDER_COLOR   lv_color_hex(0x1F3B5B)
#define TEXT_PRIMARY        lv_color_hex(0xEAF3FF)
#define TEXT_SECONDARY      lv_color_hex(0x8FA9C2)
#define ACCENT_COLOR        lv_color_hex(0x4CC9F0)
#define SELECTED_COLOR      lv_color_hex(0x4C7DFF)
#define UNSELECTED_COLOR    lv_color_hex(0x102437)
#define UI_FONT             &lv_font_montserrat_16

typedef struct {
    const char *name;
    const char *subtitle;
    const char *icon;
    lv_obj_t *btn;
} cooking_option_t;

static cooking_option_t s_options[] = {
    {"Steam", "Low fat", "", NULL},
    {"Boil", "Light", "", NULL},
    {"Stir Fry", "Quick", "", NULL},
    {"Pan Fry", "Hot pan", "", NULL},
    {"Bake", "Oven", "", NULL},
    {"Deep Fry", "High oil", "", NULL},
};

static int s_selected_index = 0;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_hint_label = NULL;
static lv_obj_t *s_current_mode_label = NULL;

static void update_option_style(int index, bool selected)
{
    lv_obj_t *btn = s_options[index].btn;
    if(!btn) return;

    lv_obj_set_style_bg_color(btn, selected ? SELECTED_COLOR : UNSELECTED_COLOR, 0);
    lv_obj_set_style_border_color(btn, selected ? ACCENT_COLOR : CARD_BORDER_COLOR, 0);
    lv_obj_set_style_shadow_opa(btn, selected ? LV_OPA_30 : LV_OPA_10, 0);
    lv_obj_set_style_translate_y(btn, selected ? -4 : 0, 0);
}

static void refresh_selection(void)
{
    for(size_t i = 0; i < sizeof(s_options) / sizeof(s_options[0]); i++) {
        update_option_style((int)i, (int)i == s_selected_index);
    }

    if(s_current_mode_label) {
        lv_label_set_text_fmt(s_current_mode_label, "Selected  %s", s_options[s_selected_index].name);
    }
    if(s_status_label) {
        lv_label_set_text_fmt(s_status_label, "%s | %s", s_options[s_selected_index].name, s_options[s_selected_index].subtitle);
    }
}

static void option_event_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        s_selected_index = idx;
        refresh_selection();
    }
}

static lv_obj_t *create_option_button(lv_obj_t *parent, const cooking_option_t *opt, int index)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 136, 92);
    lv_obj_set_style_radius(btn, 18, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(btn, 16, 0);
    lv_obj_set_style_shadow_spread(btn, 1, 0);
    lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_ofs_y(btn, 6, 0);
    lv_obj_add_event_cb(btn, option_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)index);

    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, opt->icon);
    lv_obj_set_style_text_color(icon, ACCENT_COLOR, 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_22, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 12, 10);

    lv_obj_t *name = lv_label_create(btn);
    lv_label_set_text(name, opt->name);
    lv_obj_set_style_text_color(name, TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(name, UI_FONT, 0);
    lv_obj_align(name, LV_ALIGN_BOTTOM_LEFT, 12, -30);

    lv_obj_t *sub = lv_label_create(btn);
    lv_label_set_text(sub, opt->subtitle);
    lv_obj_set_style_text_color(sub, TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(sub, UI_FONT, 0);
    lv_obj_align(sub, LV_ALIGN_BOTTOM_LEFT, 12, -10);

    return btn;
}

void example_lvgl_demo_ui(lv_display_t *disp)
{
    LV_UNUSED(disp);
    lv_obj_t *scr = lv_display_get_screen_active(disp);
    lv_obj_set_style_bg_color(scr, PANEL_BG_COLOR, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *top = lv_obj_create(scr);
    lv_obj_set_size(top, LV_PCT(100), 88);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x0A1624), 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(top);
    lv_label_set_text(title, "Kitchen Scale");
    lv_obj_set_style_text_color(title, TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(title, UI_FONT, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 24, -6);

    s_current_mode_label = lv_label_create(top);
    lv_label_set_text(s_current_mode_label, "Selected  Steam");
    lv_obj_set_style_text_color(s_current_mode_label, TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_current_mode_label, UI_FONT, 0);
    lv_obj_align(s_current_mode_label, LV_ALIGN_LEFT_MID, 24, 22);

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_set_size(card, 430, 420);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 16);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(card, 26, 0);
    lv_obj_set_style_bg_color(card, CARD_COLOR, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, CARD_BORDER_COLOR, 0);
    lv_obj_set_style_shadow_width(card, 30, 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);

    lv_obj_t *subtitle = lv_label_create(card);
    lv_label_set_text(subtitle, "Choose cooking mode");
    lv_obj_set_style_text_color(subtitle, TEXT_PRIMARY, 0);
    lv_obj_set_style_text_font(subtitle, UI_FONT, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 20);

    s_status_label = lv_label_create(card);
    lv_label_set_text(s_status_label, "Steam | Low fat");
    lv_obj_set_style_text_color(s_status_label, ACCENT_COLOR, 0);
    lv_obj_set_style_text_font(s_status_label, UI_FONT, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 48);

    lv_obj_t *grid = lv_obj_create(card);
    lv_obj_set_size(grid, 380, 300);
    lv_obj_align(grid, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_style_pad_column(grid, 14, 0);

    static int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    for(int i = 0; i < 6; i++) {
        s_options[i].btn = create_option_button(grid, &s_options[i], i);
        lv_obj_set_grid_cell(s_options[i].btn,
                             LV_GRID_ALIGN_STRETCH, i % 2, 1,
                             LV_GRID_ALIGN_STRETCH, i / 2, 1);
    }

    s_hint_label = lv_label_create(scr);
    lv_label_set_text(s_hint_label, "Touch a mode to preview selection");
    lv_obj_set_style_text_color(s_hint_label, TEXT_SECONDARY, 0);
    lv_obj_set_style_text_font(s_hint_label, UI_FONT, 0);
    lv_obj_align(s_hint_label, LV_ALIGN_BOTTOM_MID, 0, -14);

    refresh_selection();
}
