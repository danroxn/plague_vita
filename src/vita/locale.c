#include "vita/locale.h"

#include "app/log.h"

#include <psp2/apputil.h>
#include <psp2/system_param.h>
#include <pthread.h>

static pthread_once_t app_util_once = PTHREAD_ONCE_INIT;
static void initialize_app_util(void) {
    SceAppUtilInitParam init_parameters = {0};
    SceAppUtilBootParam boot_parameters = {0};
    int result = sceAppUtilInit(&init_parameters, &boot_parameters);
    if (result < 0)
        port_log_error("AppUtil init: 0x%08x", result);
}
const char *port_vita_language(void) {
    static const char *languages[] = {"ja", "en", "fr", "es", "de", "it", "nl", "pt", "ru", "ko",
                                      "zh", "zh", "fi", "sv", "da", "no", "pl", "pt", "en", "tr"};
    int language_id = 1;
    pthread_once(&app_util_once, initialize_app_util);
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &language_id) < 0)
        language_id = 1;
    return language_id >= 0 && (unsigned)language_id < sizeof(languages) / sizeof(*languages)
               ? languages[language_id]
               : "en";
}
