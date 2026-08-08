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
    int integer_values[3] = {4, 5, 6};
    float float_values[2] = {1.25f, 2.5f};
    double double_values[2] = {3.5, 4.75};
    const char *string_values[2] = {"alpha", "beta"};
    cJSON *integer_array;
    cJSON *float_array;
    cJSON *double_array;
    cJSON *string_array;
    cJSON *array_item;
    cJSON *built;
    cJSON *built_number;
    cJSON *duplicate;
    cJSON *replacement;
    cJSON *detached;
    char minified[64] = " { \"x\" : 1, \"y\" : [ true, false ] } ";

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

    integer_array = cJSON_CreateIntArray(integer_values, 3);
    float_array = cJSON_CreateFloatArray(float_values, 2);
    double_array = cJSON_CreateDoubleArray(double_values, 2);
    string_array = cJSON_CreateStringArray(string_values, 2);
    if (integer_array == NULL || float_array == NULL || double_array == NULL ||
        string_array == NULL) {
        return 11;
    }
    if (cJSON_GetArraySize(integer_array) != 3 || cJSON_GetArraySize(float_array) != 2 ||
        cJSON_GetArraySize(double_array) != 2 || cJSON_GetArraySize(string_array) != 2) {
        return 12;
    }
    array_item = cJSON_GetArrayItem(integer_array, 2);
    if (array_item == NULL || array_item->valueint != 6) {
        return 13;
    }
    array_item = cJSON_GetArrayItem(float_array, 1);
    if (array_item == NULL || array_item->valuedouble != 2.5) {
        return 14;
    }
    array_item = cJSON_GetArrayItem(double_array, 1);
    if (array_item == NULL || array_item->valuedouble != 4.75) {
        return 15;
    }
    array_item = cJSON_GetArrayItem(string_array, 1);
    if (!cJSON_IsString(array_item) || array_item->valuestring == NULL ||
        strcmp(array_item->valuestring, "beta") != 0) {
        return 16;
    }

    built = cJSON_CreateObject();
    if (built == NULL || cJSON_AddStringToObject(built, "name", "built") == NULL ||
        cJSON_AddNumberToObject(built, "score", 6.25) == NULL) {
        return 17;
    }
    built_number = cJSON_GetObjectItemCaseSensitive(built, "score");
    if (!cJSON_IsNumber(built_number) || built_number->valuedouble != 6.25) {
        return 18;
    }

    duplicate = cJSON_Duplicate(built, 1);
    if (duplicate == NULL || !cJSON_Compare(built, duplicate, 1)) {
        return 19;
    }

    replacement = cJSON_CreateString("updated");
    if (replacement == NULL ||
        !cJSON_ReplaceItemInObjectCaseSensitive(built, "name", replacement)) {
        cJSON_Delete(replacement);
        return 20;
    }
    name = cJSON_GetObjectItemCaseSensitive(built, "name");
    if (!cJSON_IsString(name) || name->valuestring == NULL ||
        strcmp(name->valuestring, "updated") != 0) {
        return 21;
    }

    detached = cJSON_DetachItemFromObjectCaseSensitive(built, "name");
    if (!cJSON_IsString(detached) || detached->valuestring == NULL ||
        strcmp(detached->valuestring, "updated") != 0) {
        return 22;
    }
    cJSON_Delete(detached);
    if (cJSON_HasObjectItem(built, "name")) {
        return 23;
    }

    cJSON_Minify(minified);
    if (strcmp(minified, "{\"x\":1,\"y\":[true,false]}") != 0) {
        return 24;
    }

    cJSON_Delete(duplicate);
    cJSON_Delete(built);
    cJSON_Delete(string_array);
    cJSON_Delete(double_array);
    cJSON_Delete(float_array);
    cJSON_Delete(integer_array);
    cJSON_Delete(number_b);
    cJSON_Delete(number_a);
    cJSON_Delete(roundtrip);
    cJSON_Delete(root);
    return 0;
}
