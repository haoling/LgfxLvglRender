#include <LovyanGFX.h>
#include <lvgl.h>

#ifndef LGFX_LVGL_RENDER_PIXEL_BUFFER_SIZE
#define LGFX_LVGL_RENDER_PIXEL_BUFFER_SIZE LV_HOR_RES_MAX * LV_VER_RES_MAX / 5
#endif

typedef struct _lv_font_t_lgfx : lv_font_t {
    const lgfx::IFont *font;

    _lv_font_t_lgfx(const lgfx::IFont *font);
} lv_font_t_lgfx;

void lgfx_lv_disp_drv_register(lgfx::LGFXBase *lcd);
