#include "i18n.h"

static const wchar_t *g_en[] = {
#define X(id, en, ko) en,
    STRING_TABLE(X)
#undef X
};
static const wchar_t *g_ko[] = {
#define X(id, en, ko) ko,
    STRING_TABLE(X)
#undef X
};

static Lang g_lang = LANG_KO;

void I18nSetLanguage(Lang l) { g_lang = l; }
Lang I18nGetLanguage() { return g_lang; }

Lang I18nDetectSystemLanguage() {
    LANGID id = GetUserDefaultUILanguage();
    return PRIMARYLANGID(id) == LANG_KOREAN ? LANG_KO : LANG_EN;
}

const wchar_t *T(StrId id) {
    if (id < 0 || id >= S_COUNT) return L"?";
    return g_lang == LANG_KO ? g_ko[id] : g_en[id];
}
