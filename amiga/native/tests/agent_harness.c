/* Synthetic transport only. Never uses a network or a real credential. */
#include "native.h"
#include "vendor/cJSON.h"
#include "workspace.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int step, builds, mode;
char *list_project_sources(void)
{
    char *s = malloc(8);
    strcpy(s, "main.c\n");
    return s;
}
int project_source_allowed(const char *p)
{
    return source_name(p);
}
int cancel_requested(void)
{
    return mode == 5;
}
int build_project(void)
{
    ++builds;
    return 1;
}
int cloud_request(const char *key, const char *json, char **reply)
{
    cJSON *r = cJSON_Parse(json), *resp, *output, *call;
    char id[32];
    assert(r && !strcmp(key, "test-key"));
    assert(strstr(json, "test-key") == NULL);
    assert(cJSON_IsFalse(cJSON_GetObjectItem(r, "parallel_tool_calls")));
    assert(cJSON_IsArray(cJSON_GetObjectItem(r, "tools")));
    if (step)
        assert(cJSON_IsString(cJSON_GetObjectItem(r, "previous_response_id")));
    if (mode == 10 && !step)
        assert(
            !strcmp(cJSON_GetObjectItem(r, "previous_response_id")->valuestring, "fixture_resume"));
    cJSON_Delete(r);
    if (mode == 1)
    {
        *reply = NULL;
        ++step;
        return 401;
    }
    resp = cJSON_CreateObject();
    sprintf(id, "fixture_%d", step);
    cJSON_AddStringToObject(resp, "id", id);
    cJSON_AddStringToObject(resp, "status", mode == 2 ? "incomplete" : "completed");
    output = cJSON_AddArrayToObject(resp, "output");
    call = cJSON_CreateObject();
    if ((mode < 7 && (step < 3 || mode == 4)) || (mode >= 7 && mode <= 9 && step == 0))
    {
        cJSON_AddStringToObject(call, "type", "function_call");
        cJSON_AddStringToObject(call, "call_id", id);
        cJSON_AddStringToObject(call, "name",
                                mode == 4 ? "read_source"
                                          : (step == 0   ? "read_source"
                                             : step == 1 ? "write_source"
                                                         : "build"));
        cJSON_AddStringToObject(
            call, "arguments",
            step == 1 && mode != 4 ? "{\"source\":\"int main(void) { return 0; }\\n\"}" : "{}");
        if (mode >= 7)
        {
            cJSON_ReplaceItemInObject(call, "name",
                                      cJSON_CreateString(mode == 7   ? "write_file"
                                                         : mode == 8 ? "write_file"
                                                                     : "list_sources"));
            cJSON_ReplaceItemInObject(
                call, "arguments",
                cJSON_CreateString(mode == 7
                                       ? "{\"path\":\"util.h\",\"source\":\"#define VALUE 2\\n\"}"
                                   : mode == 8 ? "{\"path\":\"../escape.c\",\"source\":\"bad\"}"
                                               : "{}"));
        }
    }
    else
    {
        cJSON *text;
        cJSON_AddStringToObject(call, "type", "message");
        text = cJSON_CreateObject();
        cJSON_AddStringToObject(text, "type", "output_text");
        cJSON_AddStringToObject(text, "text", "Fixture completed.");
        cJSON_AddItemToArray(cJSON_AddArrayToObject(call, "content"), text);
    }
    cJSON_AddItemToArray(output, call);
    if (mode == 3)
        cJSON_AddItemToArray(output, cJSON_Duplicate(call, 1));
    *reply = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    ++step;
    return 200;
}
int main(int argc, char **argv)
{
    int code;
    mode = argc > 1 ? atoi(argv[1]) : 0;
    code = run_agent();
    if (mode >= 7)
        assert(code == 0 && step == (mode == 10 ? 1 : 2) && builds == 0);
    else if (mode == 0)
        assert(code == 0 && step == 4 && builds == 1);
    else if (mode == 4)
        assert(code == 20 && step == 8 && builds == 0);
    else if (mode == 5 || mode == 6)
        assert(code == 20 && step == 0 && builds == 0);
    else
        assert(code == 20 && step == 1 && builds == 0);
    return 0;
}
