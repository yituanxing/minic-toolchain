#include "cJSON.h"

#include <string.h>

int main(void)
{
    cJSON *root;
    cJSON *name;
    cJSON *values;
    cJSON *second;
    char *printed;
    cJSON *roundtrip;
    cJSON *roundtrip_name;
    cJSON *number_a;
    cJSON *number_b;
    double number_value;

    root = cJSON_Parse("{\"name\":\"minic\",\"values\":[1,2,3]}");
    if (root == NULL) {
        return 1;
    }

    name = cJSON_GetObjectItemCaseSensitive(root, "name");
    if (!cJSON_IsString(name) || name->valuestring == NULL ||
        strcmp(name->valuestring, "minic") != 0) {
        cJSON_Delete(root);
        return 2;
    }

    values = cJSON_GetObjectItemCaseSensitive(root, "values");
    if (!cJSON_IsArray(values) || cJSON_GetArraySize(values) != 3) {
        cJSON_Delete(root);
        return 3;
    }

    second = cJSON_GetArrayItem(values, 1);
    if (!cJSON_IsNumber(second) || second->valueint != 2) {
        cJSON_Delete(root);
        return 4;
    }

    printed = cJSON_PrintUnformatted(root);
    if (printed == NULL) {
        cJSON_Delete(root);
        return 5;
    }

    roundtrip = cJSON_Parse(printed);
    cJSON_free(printed);
    if (roundtrip == NULL) {
        cJSON_Delete(root);
        return 6;
    }

    roundtrip_name = cJSON_GetObjectItemCaseSensitive(roundtrip, "name");
    if (!cJSON_IsString(roundtrip_name) || roundtrip_name->valuestring == NULL ||
        strcmp(roundtrip_name->valuestring, "minic") != 0) {
        cJSON_Delete(roundtrip);
        cJSON_Delete(root);
        return 7;
    }

    number_a = cJSON_CreateNumber(3.5);
    number_b = cJSON_CreateNumber(3.5);
    if (number_a == NULL || number_b == NULL) {
        cJSON_Delete(number_b);
        cJSON_Delete(number_a);
        cJSON_Delete(roundtrip);
        cJSON_Delete(root);
        return 8;
    }
    if (!cJSON_Compare(number_a, number_b, 1)) {
        cJSON_Delete(number_b);
        cJSON_Delete(number_a);
        cJSON_Delete(roundtrip);
        cJSON_Delete(root);
        return 9;
    }
    number_value = cJSON_GetNumberValue(number_a);
    if (number_value != 3.5) {
        cJSON_Delete(number_b);
        cJSON_Delete(number_a);
        cJSON_Delete(roundtrip);
        cJSON_Delete(root);
        return 10;
    }

    cJSON_Delete(number_b);
    cJSON_Delete(number_a);
    cJSON_Delete(roundtrip);
    cJSON_Delete(root);
    return 0;
}
