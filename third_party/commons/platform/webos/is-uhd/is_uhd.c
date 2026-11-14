#include "is_uhd.h"

#include <stddef.h>
#include <pbnjson.h>

#include "lunasynccall.h"

int commons_webos_is_uhd(bool *uhd) {
    char *out = NULL;
    if (!HLunaServiceCallSync("luna://com.webos.service.tv.systemproperty/getSystemInfo", "{\"keys\":[\"UHD\"]}", true,
                              &out)) {
        return -1;
    }
    int ret = 0;
    JSchemaInfo schemaInfo;
    jschema_info_init(&schemaInfo, jschema_all(), NULL, NULL);
    jdomparser_ref parser = jdomparser_create(&schemaInfo, 0);
    if (!jdomparser_feed(parser, out, (int) strlen(out))) {
        ret = -1;
        goto finally;
    }
    if (!jdomparser_end(parser)) {
        ret = -1;
        goto finally;
    }
    jvalue_ref payload_obj = jdomparser_get_result(parser);
    jvalue_ref uhd_val = jobject_get(payload_obj, J_CSTR_TO_BUF("UHD"));
    if (jis_string(uhd_val)) {
        *uhd = jstring_equal2(uhd_val, J_CSTR_TO_BUF("true"));
    } else if (jis_valid(uhd_val)) {
        jboolean_get(uhd_val, uhd);
    } else {
        ret = -1;
    }
    finally:
    jdomparser_release(&parser);
    free(out);
    return ret;
}