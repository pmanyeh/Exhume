/* The settings screen (ui/settings.c) in the HD font, its words in Chinese (settings_text.c). */
#ifndef UW1_SETTINGS_TEXT_H
#define UW1_SETTINGS_TEXT_H
#include <stdint.h>
struct HdFont;
/* Before the screen is first drawn: font (the presentation thread's; NULL: the screen's own
   bitmap font only), whether the screen is in Chinese, and the HD text's antialiasing now. */
void settings_text_start(struct HdFont *font, int chinese, int smooth);
/* whether the screen is in Chinese (a font, and the game in zh-TW) */
int settings_text_chinese(void);
/* The Language tab's sample's two lines (UTF-8, kept, not copied), the game's kind of text */
void settings_text_sample(const char *first, const char *second);
/* The Language tab's sample (the game's settings table's rows' preview); the game defines
   port_text_choice(&size, &line, &border10, &smooth), the text rows' values as the screen has them */
void port_text_preview(uint32_t *px, int x, int y, int w, int h, int value);
#endif
