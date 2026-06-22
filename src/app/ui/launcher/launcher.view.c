#include "launcher.controller.h"
#include "apps.controller.h"

#include "ui/root.h"

#include "lvgl.h"

#include "lvgl/ext/lv_child_group.h"
#include "lvgl/util/lv_app_utils.h"
#include "lvgl/font/material_icons_regular_symbols.h"

#include "util/font.h"
#include "util/i18n.h"

#include "app.h"
#include "lvgl/theme/lv_theme_moonlight.h"

static void detail_group_add(lv_event_t *event);

#define NAV_WIDTH_EXPANDED 240
#define NAV_TRANSLATE_OFFSET (NAV_WIDTH_EXPANDED - NAV_WIDTH_COLLAPSED)

lv_obj_t *launcher_win_create(lv_fragment_t *self, lv_obj_t *parent) {
    launcher_fragment_t *controller = (launcher_fragment_t *) self;
    app_ui_t *ui = &controller->global->ui;
    /*Create a window*/
    lv_obj_t *win = lv_win_create(parent, LV_DPX(NAV_WIDTH_COLLAPSED));

    lv_obj_t *header = lv_win_get_header(win);
    lv_obj_add_flag(header, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *content = lv_win_get_content(win);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(content, LV_LAYOUT_GRID);
    controller->col_dsc[0] = LV_DPX(NAV_WIDTH_COLLAPSED);
    controller->col_dsc[1] = LV_DPX(NAV_TRANSLATE_OFFSET);
    controller->col_dsc[2] = LV_GRID_FR(1);
    controller->col_dsc[3] = LV_GRID_TEMPLATE_LAST;
    controller->row_dsc[0] = LV_GRID_FR(1);
    controller->row_dsc[1] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_style_pad_gap(content, 0, 0);
    lv_obj_set_grid_dsc_array(content, controller->col_dsc, controller->row_dsc);

    controller->nav_group = lv_group_create();
    controller->detail_group = lv_group_create();
    lv_obj_t *nav = lv_obj_create(content);
    lv_obj_t *detail = lv_obj_create(content);
    lv_obj_add_event_cb(nav, cb_child_group_add, LV_EVENT_CHILD_CREATED, controller->nav_group);
    lv_obj_add_event_cb(detail, detail_group_add, LV_EVENT_CHILD_CREATED, controller);

    lv_obj_set_grid_cell(nav, LV_GRID_ALIGN_STRETCH, 0, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_set_grid_cell(detail, LV_GRID_ALIGN_STRETCH, 1, 2, LV_GRID_ALIGN_STRETCH, 0, 1);
    lv_obj_clear_flag(detail, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_pad_row(nav, 0, 0);

    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(nav, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(nav, 0, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_border_width(nav, 0, 0);

    lv_obj_set_style_pad_all(detail, 0, 0);
    lv_obj_set_style_radius(detail, 0, 0);
    lv_obj_set_style_bg_color(detail, lv_color_lighten(lv_color_black(), 30), 0);
    lv_obj_set_style_shadow_color(detail, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(detail, LV_OPA_MAX, 0);
    lv_obj_set_style_shadow_width(detail, lv_dpx(5), 0);
    lv_obj_set_style_border_width(detail, 0, 0);
    lv_obj_set_style_translate_x(detail, lv_dpx(NAV_TRANSLATE_OFFSET), 0);
    lv_obj_set_style_translate_x(detail, 0, LV_STATE_USER_1);

    // Create a beautiful default welcome content container inside detail
    lv_obj_t *welcome_cnt = lv_obj_create(detail);
    lv_obj_remove_style_all(welcome_cnt);
    lv_obj_set_size(welcome_cnt, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(welcome_cnt, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(welcome_cnt, LV_OPA_COVER, 0);

    lv_obj_set_layout(welcome_cnt, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(welcome_cnt, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(welcome_cnt, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(welcome_cnt, 0, 0);
    lv_obj_set_style_pad_row(welcome_cnt, 0, 0);

    // Add a big beautiful looping animated logo directly to welcome_cnt so it flows in flex layout
    lv_obj_t *welcome_logo;
#ifdef APPID
    char gif_path[256];
    snprintf(gif_path, sizeof(gif_path), "A:/media/developer/apps/usr/palm/applications/%s/android-hi.gif", APPID);
    welcome_logo = lv_gif_create(welcome_cnt);
    lv_gif_set_src(welcome_logo, gif_path);
    lv_gif_t *gif_obj = (lv_gif_t *)welcome_logo;
    if(gif_obj->gif) {
        gif_obj->gif->loop_count = 0; // Force loop forever
    }
#else
    welcome_logo = lv_img_create(welcome_cnt);
    lv_img_set_src(welcome_logo, ui_logo_src());
#endif

    // Create Section 2: Text Section occupying remaining space
    lv_obj_t *text_sec = lv_obj_create(welcome_cnt);
    lv_obj_remove_style_all(text_sec);
    lv_obj_set_size(text_sec, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text_sec, 1);
    lv_obj_set_layout(text_sec, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_sec, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_sec, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(text_sec, LV_DPX(20), 0);
    lv_obj_set_style_pad_row(text_sec, LV_DPX(10), 0);
    lv_obj_set_style_translate_x(text_sec, LV_PCT(-12), 0);
    lv_obj_set_style_translate_y(text_sec, LV_PCT(-20), 0);


    // Add a styled welcome title (dark text on white bg)
    lv_obj_t *welcome_title = lv_label_create(text_sec);
    lv_obj_set_style_text_font(welcome_title, lv_theme_get_font_large(detail), 0);
    lv_obj_set_style_text_align(welcome_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(welcome_title, lv_color_hex(0x2f3237), 0);
    lv_label_set_text(welcome_title, "Your Android journey starts here.");

    // Add a detailed body/guidance text (medium dark text on white bg)
    lv_obj_t *welcome_text = lv_label_create(text_sec);
    lv_obj_set_style_text_font(welcome_text, lv_theme_get_font_normal(detail), 0);
    lv_obj_set_style_text_align(welcome_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(welcome_text, lv_color_hex(0x555555), 0);
    lv_label_set_text(welcome_text,
                      "Select 'Start Screencast' on the left menu to begin.");

    controller->welcome_cnt = welcome_cnt;

    lv_obj_t *title = lv_obj_create(nav);
    lv_obj_remove_style_all(title);
    lv_obj_set_size(title, LV_PCT(100), LV_DPX(50));
    lv_obj_set_flex_flow(title, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(title, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(title, lv_color_darken(lv_color_hex(0x2f3237), 4), 0);
    lv_obj_set_style_pad_gap(title, 0, 0);

    lv_obj_t *title_logo = lv_img_create(title);
    lv_obj_set_size(title_logo, LV_DPX(NAV_WIDTH_COLLAPSED), LV_DPX(50));
    lv_obj_set_style_pad_hor(title_logo, LV_DPX((NAV_WIDTH_COLLAPSED - NAV_LOGO_SIZE) / 2), 0);
    lv_obj_set_style_pad_ver(title_logo, LV_DPX((50 - NAV_LOGO_SIZE) / 2), 0);
    lv_img_set_src(title_logo, ui_logo_src());

    lv_obj_t *title_label = lv_label_create(title);
    lv_obj_set_style_pad_hor(title_label, 0, 0);
    lv_obj_set_style_text_font(title_label, lv_theme_get_font_large(title), 0);
    lv_label_set_text_static(title_label, "ADB.webOS");
    lv_obj_get_style_flex_grow(title_label, 1);

    lv_obj_t *pclist = lv_list_create(nav);
    lv_obj_add_flag(pclist, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_width(pclist, LV_PCT(100));
    lv_obj_set_style_pad_all(pclist, 0, 0);
    lv_obj_set_style_radius(pclist, 0, 0);
    lv_obj_set_style_border_width(pclist, 0, 0);
    lv_obj_set_style_bg_opa(pclist, 0, 0);

    lv_obj_set_flex_grow(pclist, 1);

    // Use list button for normal container
    lv_obj_t *add_btn = lv_list_add_btn(nav, MAT_SYMBOL_ADD_TO_QUEUE, locstr("Add computer"));
    lv_obj_add_style(add_btn, &controller->nav_menu_style, 0);
    lv_btn_set_icon_font(add_btn, lv_theme_moonlight_get_iconfont_small(nav));
    lv_btn_set_text_font(add_btn, lv_theme_get_font_small(nav));
    lv_obj_add_flag(add_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(add_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *help_btn = lv_list_add_btn(nav, MAT_SYMBOL_HELP, locstr("Help"));
    lv_obj_add_style(help_btn, &controller->nav_menu_style, 0);
    lv_btn_set_icon_font(help_btn, lv_theme_moonlight_get_iconfont_small(nav));
    lv_btn_set_text_font(help_btn, lv_theme_get_font_small(nav));
    lv_obj_add_flag(help_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(help_btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *pref_btn = lv_list_add_btn(nav, MAT_SYMBOL_SETTINGS, locstr("Settings"));
    lv_obj_add_style(pref_btn, &controller->nav_menu_style, 0);
    lv_btn_set_icon_font(pref_btn, lv_theme_moonlight_get_iconfont_small(nav));
    lv_btn_set_text_font(pref_btn, lv_theme_get_font_small(nav));
    lv_obj_add_flag(pref_btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(pref_btn, LV_OBJ_FLAG_HIDDEN);


    // only two used buttons for now
    lv_obj_t *scrcpy_btn = lv_list_add_btn(nav, MAT_SYMBOL_PLAY_ARROW, "Start Screencast");
    lv_obj_add_style(scrcpy_btn, &controller->nav_menu_style, 0);
    lv_btn_set_icon_font(scrcpy_btn, lv_theme_moonlight_get_iconfont_small(nav));
    lv_btn_set_text_font(scrcpy_btn, lv_theme_get_font_small(nav));
    lv_obj_add_flag(scrcpy_btn, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *quit_btn = lv_list_add_btn(nav, MAT_SYMBOL_CLOSE, locstr("Quit"));
    lv_obj_add_style(quit_btn, &controller->nav_menu_style, 0);
    lv_btn_set_icon_font(quit_btn, lv_theme_moonlight_get_iconfont_small(nav));
    lv_btn_set_text_font(quit_btn, lv_theme_get_font_small(nav));
    lv_obj_add_flag(quit_btn, LV_OBJ_FLAG_EVENT_BUBBLE);

    controller->nav = nav;
    controller->detail = detail;
    controller->pclist = pclist;
    controller->add_btn = add_btn;
    controller->pref_btn = pref_btn;
    controller->help_btn = help_btn;

    controller->scrcpy_btn = scrcpy_btn;
    controller->quit_btn = quit_btn;
    return win;
}

static void detail_group_add(lv_event_t *event) {
    lv_obj_t *child = lv_event_get_param(event);
    launcher_fragment_t *fragment = lv_event_get_user_data(event);
    apps_fragment_t *apps_fragment = (apps_fragment_t *) lv_fragment_manager_find_by_container(
            fragment->base.child_manager, fragment->detail);
    if (!apps_fragment) return;
    lv_obj_t *view = apps_fragment->base.obj;
    if (!view) return;
    if (!child || !lv_obj_is_group_def(child)) return;
    if (lv_obj_get_group(child)) {
        lv_group_remove_obj(child);
    }
    if (child->parent != view && child->parent != apps_fragment->apperror) {
        return;
    }
    lv_group_add_obj(fragment->detail_group, child);
}
