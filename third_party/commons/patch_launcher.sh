#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/ui/launcher/launcher.controller.c"

# Add button creation
sed -i '/controller->quit_btn = quit_btn;/i \    lv_obj_t *scrcpy_btn = lv_list_add_btn(nav, MAT_SYMBOL_PLAY_ARROW, "Start Scrcpy");\n    lv_obj_add_style(scrcpy_btn, \&controller->nav_menu_style, 0);\n    lv_btn_set_icon_font(scrcpy_btn, lv_theme_moonlight_get_iconfont_small(nav));\n    lv_btn_set_text_font(scrcpy_btn, lv_theme_get_font_small(nav));\n    lv_obj_add_flag(scrcpy_btn, LV_OBJ_FLAG_EVENT_BUBBLE);' "$FILE"

# Add handler declaration
sed -i '/static void open_help(lv_event_t \*event);/a static void start_scrcpy(lv_event_t *event);' "$FILE"

# Add event callback registration
sed -i '/lv_obj_add_event_cb(fragment->quit_btn, app_quit_confirm, LV_EVENT_CLICKED, fragment);/a \    lv_obj_add_event_cb(scrcpy_btn, start_scrcpy, LV_EVENT_CLICKED, fragment);' "$FILE"

# Add handler function
cat << 'HANDLER' >> "$FILE"

static void start_scrcpy(lv_event_t *event) {
    launcher_fragment_t *controller = lv_event_get_user_data(event);
    app_t *app = controller->global;
    extern int app_session_begin_scrcpy(app_t *app);
    app_session_begin_scrcpy(app);
}
HANDLER
