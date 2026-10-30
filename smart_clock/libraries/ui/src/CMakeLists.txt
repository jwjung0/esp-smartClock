SET(SOURCES screens/ui_loading.c
    screens/ui_home.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_539372687.c
    images/ui_img_575321948.c
    images/ui_img_1433770785.c
    images/ui_img_icon_temp_png.c
    images/ui_img_959432778.c
    images/ui_img_579096672.c
    images/ui_img_2047004501.c
    images/ui_img_1449682034.c
    images/ui_img_weather_clear_png.c
    fonts/ui_font_orbitron_20.c
    fonts/ui_font_orbitron_48.c)

add_library(ui ${SOURCES})
