#include "os_info.h"

#include <string.h>
#include <pbnjson.h>

#include "lunasynccall.h"

int os_info_get(os_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->name = strdup("webOS");
    char *payload = NULL;
    const char *uri = "luna://com.webos.service.tv.systemproperty/getSystemInfo";
    if (!HLunaServiceCallSync(uri, "{\"keys\":[\"sdkVersion\", \"otaId\"]}", true, &payload) || !payload) {
        memset(&info->version, 0, sizeof(version_info_t));
        return -1;
    }

    JSchemaInfo schemaInfo;
    jschema_info_init(&schemaInfo, jschema_all(), NULL, NULL);
    jdomparser_ref parser = jdomparser_create(&schemaInfo, 0);
    jdomparser_feed(parser, payload, (int) strlen(payload));
    jdomparser_end(parser);
    jvalue_ref os_info = jdomparser_get_result(parser);
    jvalue_ref sdk_version = jobject_get(os_info, j_cstr_to_buffer("sdkVersion"));
    if (!jis_null(sdk_version)) {
        raw_buffer sdk_version_buf = jstring_get(sdk_version);
        char *version_str = strndup(sdk_version_buf.m_str, sdk_version_buf.m_len);
        version_info_parse(&info->version, version_str);
        free(version_str);
    }
    jvalue_ref ota_id = jobject_get(os_info, j_cstr_to_buffer("otaId"));
    if (!jis_null(ota_id)) {
        raw_buffer ota_id_buf = jstring_get(ota_id);
        info->extras = strndup(ota_id_buf.m_str, ota_id_buf.m_len);
    }
    jdomparser_release(&parser);
    return 0;
}
