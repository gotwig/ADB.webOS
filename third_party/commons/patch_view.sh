#!/bin/bash
FILE="/home/gotwig/projects/moonlight-tv/src/app/ui/launcher/launcher.view.c"

# Add scrcpy_btn creation
sed -i '/controller->quit_btn = quit_btn;/i \    lv_obj_t *scrcpy_btn = lv_list_add_btn(nav, MAT_SYMBOL_PLAY_ARROW, "Start Scrcpy");\n    lv_obj_add_style(scrcpy_btn, \&controller->nav_menu_style, 0);\n    lv_btn_set_icon_font(scrcpy_btn, lv_theme_moonlight_get_iconfont_small(nav));\n    lv_btn_set_text_font(scrcpy_btn, lv_theme_get_font_small(nav));\n    lv_obj_add_flag(scrcpy_btn, LV_OBJ_FLAG_EVENT_BUBBLE);\n    controller->scrcpy_btn = scrcpy_btn;' "$FILE"
