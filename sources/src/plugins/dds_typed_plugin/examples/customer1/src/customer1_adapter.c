/*
 * Example customer type plugin for dds_typed_plugin — see
 * DdsTypePluginAbi.h's doc comment for the ABI this implements, and this
 * directory's CMakeLists.txt for how it's built from customer1.idl.
 *
 * This is the ONE file a customer needs to hand-write. Everything else
 * (customer1.h/.c, the dds_topic_descriptor_t, alloc/free) comes from
 * running `idlc` on customer1.idl — see idlc_generate() in CMakeLists.txt.
 *
 * decode()/encode() define this customer's own plain-text grammar for
 * DDS_TYPED.CMD > PUBLISH/receive — here, a simple comma-separated
 * "key=value,key=value" list, chosen only because it is trivial to parse
 * and to type by hand from a script; a real customer is free to use
 * JSON, CDR-hex, or anything else instead. dds_typed_driver.cpp never
 * looks inside this text — it only ever forwards it to/from these two
 * functions unmodified (see dds_typed_driver.hpp's class doc comment).
 */
#include "DdsTypePluginAbi.h"
#include "customer1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void* VehicleState_alloc(void) { return customer1_VehicleState__alloc(); }
static void  VehicleState_free(void* d, dds_free_op_t op) { customer1_VehicleState_free((customer1_VehicleState*)d, op); }

/* "id=1,label=truck-07,speed=27.5" -> customer1_VehicleState */
static bool VehicleState_decode(const char* text, void* out_sample)
{
    customer1_VehicleState* v = (customer1_VehicleState*)out_sample;
    v->id = 0;
    v->label = strdup("");
    v->speed = 0.0f;

    bool sawAnyField = false;
    char* copy = strdup(text);
    if (!copy) return false;

    char* saveptr = NULL;
    for (char* field = strtok_r(copy, ",", &saveptr); field != NULL; field = strtok_r(NULL, ",", &saveptr)) {
        char* eq = strchr(field, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = field;
        const char* value = eq + 1;

        if (strcmp(key, "id") == 0) {
            v->id = atoi(value);
            sawAnyField = true;
        } else if (strcmp(key, "label") == 0) {
            free(v->label);
            v->label = strdup(value);
            sawAnyField = true;
        } else if (strcmp(key, "speed") == 0) {
            v->speed = (float)atof(value);
            sawAnyField = true;
        }
        /* Unknown keys are silently ignored — this customer's own choice;
           a stricter adapter could fail decode() on an unrecognized key. */
    }

    free(copy);
    return sawAnyField;
}

/* customer1_VehicleState -> "id=1,label=truck-07,speed=27.500000" */
static bool VehicleState_encode(const void* sample, char* out_buf, size_t out_cap)
{
    const customer1_VehicleState* v = (const customer1_VehicleState*)sample;
    const int n = snprintf(out_buf, out_cap, "id=%d,label=%s,speed=%f",
                            v->id, v->label ? v->label : "", (double)v->speed);
    return n > 0 && (size_t)n < out_cap;
}

static const DdsTypeEntry kTypes[] = {
    {
        .topic_name = "vehicle/state",
        .descriptor = &customer1_VehicleState_desc,
        .alloc_sample = VehicleState_alloc,
        .free_sample = VehicleState_free,
        .decode = VehicleState_decode,
        .encode = VehicleState_encode,
    },
};

static size_t get_type_count(void) { return sizeof(kTypes) / sizeof(kTypes[0]); }
static const DdsTypeEntry* get_type(size_t index) { return (index < get_type_count()) ? &kTypes[index] : NULL; }

static const DdsTypePlugin kPlugin = {
    .abi_version = DDS_TYPE_PLUGIN_ABI_VERSION,
    .customer_name = "customer1",
    .get_type_count = get_type_count,
    .get_type = get_type,
};

const DdsTypePlugin* dds_type_plugin_get(void) { return &kPlugin; }
