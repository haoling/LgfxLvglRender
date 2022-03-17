#include "LgfxLvglRender.h"

// フォント描画用のコールバック関数の定義
bool lgfx_font_get_glyph_dsc_cb(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);
const uint8_t * lgfx_font_get_glyph_bitmap_cb(const lv_font_t *, uint32_t);

/**
 * _lv_font_t_lgfxのコンストラクタ<br>
 * フォントのメトリクスから、高さとベースラインを取得してLVGLに設定できるようにする
 */
_lv_font_t_lgfx::_lv_font_t_lgfx(const lgfx::IFont *font)
{
    this->font = font;

    lgfx::FontMetrics metric;
    memset(&metric, 0x00, sizeof(metric));
    font->getDefaultMetric(&metric);

    this->get_glyph_dsc = lgfx_font_get_glyph_dsc_cb;
    this->get_glyph_bitmap = lgfx_font_get_glyph_bitmap_cb;
    this->line_height = metric.y_advance;
    this->base_line = metric.baseline;
}

// フォント描画用のバッファ定義
static size_t lgfx_font_glyph_buf_size = 0;
static uint8_t *lgfx_font_glyph_buf = NULL;
static lgfx::LGFX_Sprite lgfx_font_glyph_sprite;

// LVGLのディスプレイドライバとして使うバッファの定義
const size_t buf_pix_count = LGFX_LVGL_RENDER_PIXEL_BUFFER_SIZE;
static lv_disp_draw_buf_t disp_buf;
static lv_color_t *lv_buf1, *lv_buf2;
static bool lgfx_lv_disp_flush_cb_in_progress = false;
static lgfx::LGFXBase *lcd;

/**
 * 文字の情報を取得するコールバック関数
 */
bool lgfx_font_get_glyph_dsc_cb(const lv_font_t * font, lv_font_glyph_dsc_t * dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next) {
    //ESP_LOGD("lgfx_lvgl", "efont_get_glyph_dsc_cb: 0x%x", unicode_letter);

    // UTF-16にマッピングできる文字のみが対象
    if ((unicode_letter & 0xffff0000) != 0L) {
        ESP_LOGD("lgfx_lvgl", "unsupported unicode letter: 0x%x", unicode_letter);
        return false;
    }

    // 制御文字は処理しない
    if (unicode_letter < 0x20) {
        return false;
    }

    // 上位16ビットを捨てる
    uint16_t utf16_letter = unicode_letter & 0x0000ffff;
    // ESP_LOGD("lgfx_lvgl", "utf16_letter: 0x%x", utf16_letter);

    // lgfxからフォント情報を取る
    lgfx::FontMetrics metric;
    memset(&metric, 0x00, sizeof(metric));
    ((lv_font_t_lgfx *)font)->font->getDefaultMetric(&metric);
    if (! ((lv_font_t_lgfx *)font)->font->updateFontMetric(&metric, utf16_letter)) {
        ESP_LOGD("lgfx_lvgl", "updateFontMetric return false: 0x%x", unicode_letter);
        return false;
    }

    // 文字に関する情報を設定して返す
    dsc_out->adv_w = metric.x_advance;
    dsc_out->box_h = metric.height;
    dsc_out->box_w = metric.width;
    dsc_out->ofs_x = metric.x_offset;
    dsc_out->ofs_y = metric.y_offset;

    // 8ビットグレースケール
    dsc_out->bpp = 8;

    // 描画可能であると通知する
    return true;
}

/**
 * フォントのビットマップ配列を返すコールバック関数
 */
const uint8_t * lgfx_font_get_glyph_bitmap_cb(const lv_font_t * font, uint32_t unicode_letter) {
    // UTF-16にマッピングできる文字のみが対象
    if ((unicode_letter & 0xffff0000) != 0L) {
        ESP_LOGD("lgfx_lvgl", "unsupported unicode letter: 0x%x", unicode_letter);
        return NULL;
    }

    // 上位16ビットを捨てる
    uint16_t utf16_letter = unicode_letter & 0x0000ffff;

    // lgfxからフォント情報を取る
    lgfx::FontMetrics metric;
    memset(&metric, 0x00, sizeof(metric));
    ((lv_font_t_lgfx *)font)->font->getDefaultMetric(&metric);
    if (! ((lv_font_t_lgfx *)font)->font->updateFontMetric(&metric, utf16_letter)) {
        ESP_LOGD("lgfx_lvgl", "updateFontMetric return false: 0x%x", unicode_letter);
        return NULL;
    }

    // LVGLに返すバッファの大きさが、今から描画するスプライトの大きさよりも小さいときは拡張する
    if (metric.width * metric.height > lgfx_font_glyph_buf_size) {
        if (lgfx_font_glyph_buf_size != 0) {
            free(lgfx_font_glyph_buf);
        }
        lgfx_font_glyph_buf_size = metric.width * metric.height;
        lgfx_font_glyph_buf = (uint8_t *)malloc(lgfx_font_glyph_buf_size);
        if (lgfx_font_glyph_buf == NULL) {
            lgfx_font_glyph_buf_size = 0;
            ESP_LOGD("lgfx_lvgl", "out of memory: 0x%x", unicode_letter);
            return NULL;            
        }
    }

    // 描画用スプライトの大きさが、今から描画する文字の大きさよりも小さいときは拡張する
    if (lgfx_font_glyph_sprite.width() < metric.width || lgfx_font_glyph_sprite.height() < metric.height) {
        lgfx_font_glyph_sprite.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
        lgfx_font_glyph_sprite.createSprite(metric.width, metric.height);
    }

    // フォントを設定する
    lgfx_font_glyph_sprite.setFont(((lv_font_t_lgfx *)font)->font);

    // 背景は黒
    lgfx_font_glyph_sprite.fillScreen(TFT_BLACK);

    // 文字は白
    lgfx_font_glyph_sprite.setTextColor(TFT_WHITE);

    // 位置を合わせて文字を描画する
    lgfx_font_glyph_sprite.drawChar(utf16_letter, 0 - metric.x_offset, 0 - metric.y_offset);

    // 描画した文字をグレースケールに変換しつつ、LVGL用バッファに入れる
    lgfx_font_glyph_sprite.readRect(0, 0, metric.width, metric.height, (lgfx::grayscale_t *)lgfx_font_glyph_buf);

    // ※LVGLは文字の幅と高さの分しかメモリを見ないので、余白をゼロクリアする必要は無い
    return lgfx_font_glyph_buf;
}

/**
 * LVGLのディプレイバッファをLGFXで描画するコールバック関数
 */
void lgfx_lv_disp_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    //ESP_LOGD("lgfx_lvgl", "lgfx_lv_disp_flush_cb:%d, %d, %d, %d", area->x1, area->y1, area->x2, area->y2);

    if (! lgfx_lv_disp_flush_cb_in_progress) {
        lgfx_lv_disp_flush_cb_in_progress = true;
        lcd->startWrite();
    }

    #if LV_COLOR_16_SWAP
        lcd->pushImageDMA(area->x1, area->y1, (area->x2 - area->x1) + 1, (area->y2 - area->y1) + 1, (lgfx::swap565_t *)&color_p->full);
    #else
        lcd->pushImageDMA(area->x1, area->y1, (area->x2 - area->x1) + 1, (area->y2 - area->y1) + 1, (lgfx::rgb565_t *)&color_p->full);
    #endif

    if (lv_disp_flush_is_last(disp)) {
        lcd->endWrite();
        lgfx_lv_disp_flush_cb_in_progress = false;
    }

    ::lv_disp_flush_ready(disp);
}

/**
 * LGFXをLVGLのディスプレイドライバとして使う設定をする関数
 */
void lgfx_lv_disp_drv_register(lgfx::LGFXBase *lcd)
{
    ::lcd = lcd;

    if (lv_buf1 != NULL) {
        free(lv_buf1);
    }
    if (lv_buf2 != NULL) {
        free(lv_buf2);
    }
    lv_buf1 = (lv_color_t *)malloc(sizeof(lv_color_t) * buf_pix_count);
    if (lv_buf1 == NULL) {
        ESP_LOGD("lgfx_lvgl", "Out of memory");
        return;
    }
    lv_buf2 = (lv_color_t *)malloc(sizeof(lv_color_t) * buf_pix_count);
    if (lv_buf2 == NULL) {
        ESP_LOGD("lgfx_lvgl", "Out of memory");
        return;
    }

    lv_disp_draw_buf_init(&disp_buf, lv_buf1, lv_buf2, buf_pix_count);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    disp_drv.flush_cb = lgfx_lv_disp_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    lv_disp_drv_register(&disp_drv);
}