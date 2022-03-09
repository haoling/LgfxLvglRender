#include <LovyanGFX.h>
#include <LGFX_AUTODETECT.hpp>
#include <lvgl.h>
#include <LgfxLvglRender.h>

LGFX lcd;

lv_style_t style1;
lv_font_t_lgfx lv_efontJA_16(&lgfx::fonts::efontJA_16);


void setup()
{
    lcd.init();
    lcd.setColorDepth(16);

    lv_init();
    lgfx_lv_disp_drv_register(&lcd);

    lv_style_init(&style1);
    lv_style_set_text_font(&style1, &lv_efontJA_16);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_add_style(label, &style1, LV_STATE_DEFAULT);
    lv_label_set_text(label, "これはテストです\n改行できるYOwww\n記号も行けるﾖ!");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

void loop()
{
    lv_timer_handler();
}