SET(SOURCES ui_Screen1.c
    ui_Screen2.c
    ui_Screen3.c
    ui_Screen4.c
    ui_Screen5.c
    ui.c
    ui_comp_hook.c
    ui_helpers.c
    ui_img_accounticon_png_png.c
    ui_img_loginicon_png.c
    ui_img_home1_png.c
    ui_img_home2_png.c
    ui_img_home3_png.c
    ui_img_home4_png.c)

add_library(ui ${SOURCES})
