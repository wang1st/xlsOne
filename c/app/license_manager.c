#include "license_manager.h"

#include "platform_dialog.h"

#include "cJSON.h"
#include "monocypher-ed25519.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef XLSONE_LICENSE_PUBLIC_KEY
#define XLSONE_LICENSE_PUBLIC_KEY \
    "0000000000000000000000000000000000000000000000000000000000000000"
#endif

#define XLSONE_ACTIVATION_BASE_URL "https://z-pulse.cn"
#define XLSONE_RESTRICTED_FILE_COUNT 3
#define XLSONE_GRACE_SECONDS (3LL * 24LL * 60LL * 60LL)
#define XLSONE_CLOCK_TOLERANCE_SECONDS (60LL * 60LL)

typedef struct sha256_context {
    uint32_t state[8];
    uint64_t bit_count;
    unsigned char block[64];
    size_t block_length;
} sha256_context;

static uint32_t rotate_right(uint32_t value, unsigned int amount)
{
    return (value >> amount) | (value << (32u - amount));
}

static void sha256_transform(
    sha256_context *context,
    const unsigned char block[64]
)
{
    static const uint32_t constants[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
        0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
        0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
        0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
        0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
        0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t index;
    for (index = 0u; index < 16u; ++index) {
        words[index] =
            ((uint32_t)block[index * 4u] << 24u)
            | ((uint32_t)block[index * 4u + 1u] << 16u)
            | ((uint32_t)block[index * 4u + 2u] << 8u)
            | (uint32_t)block[index * 4u + 3u];
    }
    for (index = 16u; index < 64u; ++index) {
        const uint32_t left = words[index - 15u];
        const uint32_t right = words[index - 2u];
        const uint32_t sigma0 =
            rotate_right(left, 7u) ^ rotate_right(left, 18u) ^ (left >> 3u);
        const uint32_t sigma1 =
            rotate_right(right, 17u) ^ rotate_right(right, 19u) ^ (right >> 10u);
        words[index] = words[index - 16u] + sigma0
            + words[index - 7u] + sigma1;
    }
    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];
    for (index = 0u; index < 64u; ++index) {
        const uint32_t sum1 = rotate_right(e, 6u)
            ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temporary1 =
            h + sum1 + choose + constants[index] + words[index];
        const uint32_t sum0 = rotate_right(a, 2u)
            ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256_init(sha256_context *context)
{
    static const uint32_t initial[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    memcpy(context->state, initial, sizeof(initial));
    context->bit_count = 0u;
    context->block_length = 0u;
}

static void sha256_update(
    sha256_context *context,
    const unsigned char *data,
    size_t length
)
{
    while (length > 0u) {
        size_t amount = 64u - context->block_length;
        if (amount > length) {
            amount = length;
        }
        memcpy(context->block + context->block_length, data, amount);
        context->block_length += amount;
        data += amount;
        length -= amount;
        context->bit_count += (uint64_t)amount * 8u;
        if (context->block_length == 64u) {
            sha256_transform(context, context->block);
            context->block_length = 0u;
        }
    }
}

static void sha256_final(
    sha256_context *context,
    unsigned char digest[32]
)
{
    size_t index;
    context->block[context->block_length++] = 0x80u;
    if (context->block_length > 56u) {
        while (context->block_length < 64u) {
            context->block[context->block_length++] = 0u;
        }
        sha256_transform(context, context->block);
        context->block_length = 0u;
    }
    while (context->block_length < 56u) {
        context->block[context->block_length++] = 0u;
    }
    for (index = 0u; index < 8u; ++index) {
        context->block[63u - index] =
            (unsigned char)(context->bit_count >> (index * 8u));
    }
    sha256_transform(context, context->block);
    for (index = 0u; index < 8u; ++index) {
        digest[index * 4u] = (unsigned char)(context->state[index] >> 24u);
        digest[index * 4u + 1u] =
            (unsigned char)(context->state[index] >> 16u);
        digest[index * 4u + 2u] =
            (unsigned char)(context->state[index] >> 8u);
        digest[index * 4u + 3u] = (unsigned char)context->state[index];
    }
}

static void sha256_hex(const char *text, char output[65])
{
    static const char hex[] = "0123456789abcdef";
    sha256_context context;
    unsigned char digest[32];
    size_t index;
    sha256_init(&context);
    sha256_update(
        &context,
        (const unsigned char *)text,
        text == NULL ? 0u : strlen(text)
    );
    sha256_final(&context, digest);
    for (index = 0u; index < sizeof(digest); ++index) {
        output[index * 2u] = hex[digest[index] >> 4u];
        output[index * 2u + 1u] = hex[digest[index] & 0x0fu];
    }
    output[64] = '\0';
}

static void set_message(char *message, size_t capacity, const char *text)
{
    if (capacity > 0u) {
        (void)snprintf(message, capacity, "%s", text == NULL ? "" : text);
    }
}

static int64_t current_time_seconds(void)
{
    return (int64_t)time(NULL);
}

static int is_placeholder(const char *text)
{
    static const char *const placeholders[] = {
        "TO BE FILLED BY O.E.M.",
        "NONE",
        "NOT AVAILABLE",
        "BASEBOARD SERIAL NUMBER",
        "SERIALNUMBER"
    };
    size_t index;
    for (index = 0u;
         index < sizeof(placeholders) / sizeof(placeholders[0]);
         ++index) {
        if (strcmp(text, placeholders[index]) == 0) {
            return 1;
        }
    }
    return 0;
}

static void normalize_component(char *text)
{
    char *start = text;
    char *end;
    size_t index;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1u);
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        *--end = '\0';
    }
    for (index = 0u; text[index] != '\0'; ++index) {
        text[index] = (char)toupper((unsigned char)text[index]);
    }
    if (is_placeholder(text)) {
        text[0] = '\0';
    }
}

static void collect_fingerprint(xls_license_manager *manager)
{
    char components[4][256];
    char fallback[256];
    char *ordered[4];
    char joined[1024];
    size_t raw_count;
    size_t valid_count = 0u;
    size_t index;
    size_t used = 0u;
    memset(components, 0, sizeof(components));
    memset(fallback, 0, sizeof(fallback));
    raw_count = xls_platform_device_components(
        components, sizeof(components) / sizeof(components[0])
    );
    for (index = 0u; index < raw_count; ++index) {
        normalize_component(components[index]);
        if (components[index][0] == '\0') {
            continue;
        }
        sha256_hex(
            components[index],
            manager->component_hashes[valid_count]
        );
        ordered[valid_count++] = components[index];
    }
    if (valid_count == 0u) {
        if (xls_platform_host_name(fallback, sizeof(fallback))) {
            normalize_component(fallback);
            sha256_hex(fallback, manager->component_hashes[0]);
            ordered[0] = fallback;
            valid_count = 1u;
        }
    }
    for (index = 0u; index < valid_count; ++index) {
        size_t other;
        for (other = index + 1u; other < valid_count; ++other) {
            if (strcmp(ordered[index], ordered[other]) > 0) {
                char *swap = ordered[index];
                ordered[index] = ordered[other];
                ordered[other] = swap;
            }
        }
    }
    joined[0] = '\0';
    for (index = 0u; index < valid_count; ++index) {
        const int written = snprintf(
            joined + used,
            sizeof(joined) - used,
            "%s%s",
            index == 0u ? "" : "|",
            ordered[index]
        );
        if (written < 0 || (size_t)written >= sizeof(joined) - used) {
            valid_count = 0u;
            break;
        }
        used += (size_t)written;
    }
    manager->component_count = valid_count;
    sha256_hex(joined, manager->device_fingerprint);
}

static int parse_hex_byte(char high, char low, unsigned char *byte)
{
    int first;
    int second;
    if (high >= '0' && high <= '9') first = high - '0';
    else if (high >= 'a' && high <= 'f') first = high - 'a' + 10;
    else if (high >= 'A' && high <= 'F') first = high - 'A' + 10;
    else return 0;
    if (low >= '0' && low <= '9') second = low - '0';
    else if (low >= 'a' && low <= 'f') second = low - 'a' + 10;
    else if (low >= 'A' && low <= 'F') second = low - 'A' + 10;
    else return 0;
    *byte = (unsigned char)((first << 4) | second);
    return 1;
}

static int license_public_key(unsigned char output[32])
{
    static const char encoded[] = XLSONE_LICENSE_PUBLIC_KEY;
    size_t index;
    unsigned int nonzero = 0u;
    if (strlen(encoded) != 64u) {
        return 0;
    }
    for (index = 0u; index < 32u; ++index) {
        if (!parse_hex_byte(
            encoded[index * 2u],
            encoded[index * 2u + 1u],
            &output[index]
        )) {
            return 0;
        }
        nonzero |= output[index];
    }
    return nonzero != 0u;
}

static int base64_url_decode(
    const char *encoded,
    unsigned char *output,
    size_t output_capacity,
    size_t *output_length
)
{
    static const signed char decode[128] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,62,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,0,-1,-1,
        -1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,63,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1
    };
    unsigned int accumulator = 0u;
    unsigned int bits = 0u;
    size_t written = 0u;
    size_t index;
    for (index = 0u; encoded[index] != '\0'; ++index) {
        const unsigned char character = (unsigned char)encoded[index];
        signed char value;
        if (character == '=') {
            break;
        }
        if (character >= sizeof(decode)
            || (value = decode[character]) < 0) {
            return 0;
        }
        accumulator = (accumulator << 6u) | (unsigned int)value;
        bits += 6u;
        if (bits >= 8u) {
            bits -= 8u;
            if (written >= output_capacity) {
                return 0;
            }
            output[written++] =
                (unsigned char)((accumulator >> bits) & 0xffu);
        }
    }
    *output_length = written;
    return 1;
}

static cJSON *unique_object_item(
    const cJSON *object,
    const char *name
)
{
    cJSON *child;
    cJSON *found = NULL;
    int count = 0;
    if (!cJSON_IsObject(object)) {
        return NULL;
    }
    for (child = object->child; child != NULL; child = child->next) {
        if (child->string != NULL && strcmp(child->string, name) == 0) {
            found = child;
            ++count;
        }
    }
    return count == 1 ? found : NULL;
}

static int valid_token_text(const char *text, size_t maximum)
{
    size_t index;
    const size_t length = text == NULL ? 0u : strlen(text);
    if (length == 0u || length > maximum) {
        return 0;
    }
    for (index = 0u; index < length; ++index) {
        const unsigned char character = (unsigned char)text[index];
        if (!isalnum(character)
            && character != '-' && character != '_' && character != '.') {
            return 0;
        }
    }
    return 1;
}

static int parse_plan_text(
    const char *plan,
    xls_license_plan *parsed
)
{
    if (strcmp(plan, "trial") == 0) {
        *parsed = XLS_LICENSE_PLAN_TRIAL;
        return 1;
    }
    if (strcmp(plan, "personal_yearly") == 0) {
        *parsed = XLS_LICENSE_PLAN_PERSONAL_YEARLY;
        return 1;
    }
    if (strcmp(plan, "enterprise_10") == 0) {
        *parsed = XLS_LICENSE_PLAN_ENTERPRISE;
        return 1;
    }
    if (strcmp(plan, "personal_lifetime") == 0) {
        *parsed = XLS_LICENSE_PLAN_PERSONAL_LIFETIME;
        return 1;
    }
    return 0;
}

static int checked_json_integer(const cJSON *item, int64_t *value)
{
    double number;
    if (!cJSON_IsNumber(item)) {
        return 0;
    }
    number = item->valuedouble;
    if (!isfinite(number) || number < 0.0
        || number > 9007199254740991.0 || floor(number) != number) {
        return 0;
    }
    *value = (int64_t)number;
    return 1;
}

static int check_device_binding(
    const xls_license_manager *manager,
    const char *device_hash,
    const cJSON *components
)
{
    size_t required;
    size_t matched = 0u;
    cJSON *component;
    size_t stored = 0u;
    const char *stored_hashes[32];
    if (device_hash[0] == '\0'
        || strcmp(device_hash, manager->device_fingerprint) == 0) {
        return 1;
    }
    if (!cJSON_IsArray(components)) {
        return 0;
    }
    cJSON_ArrayForEach(component, components) {
        size_t index;
        int duplicate = 0;
        if (!cJSON_IsString(component)
            || component->valuestring == NULL
            || strlen(component->valuestring) != 64u) {
            return 0;
        }
        if (stored >= sizeof(stored_hashes) / sizeof(stored_hashes[0])) {
            return 0;
        }
        for (index = 0u; index < stored; ++index) {
            if (strcmp(
                component->valuestring, stored_hashes[index]
            ) == 0) {
                duplicate = 1;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        stored_hashes[stored] = component->valuestring;
        ++stored;
        for (index = 0u; index < manager->component_count; ++index) {
            if (strcmp(
                component->valuestring,
                manager->component_hashes[index]
            ) == 0) {
                ++matched;
                break;
            }
        }
    }
    if (stored == 0u) {
        return 0;
    }
    required = (stored * 2u + 2u) / 3u;
    if (required < 2u) {
        required = 2u;
    }
    return matched >= required;
}

static int build_canonical_payload(
    const char *key_id,
    const char *plan,
    const char *device_hash,
    const cJSON *components,
    int64_t issued_at,
    int64_t expires_at,
    char *buffer,
    size_t capacity
)
{
    size_t used;
    cJSON *component;
    int written = snprintf(
        buffer,
        capacity,
        "{\"key_id\":\"%s\",\"plan\":\"%s\",\"device_hash\":\"%s\","
        "\"device_components\":[",
        key_id,
        plan,
        device_hash
    );
    int first = 1;
    if (written < 0 || (size_t)written >= capacity) {
        return 0;
    }
    used = (size_t)written;
    cJSON_ArrayForEach(component, components) {
        const char *value = component->valuestring;
        if (!cJSON_IsString(component) || !valid_token_text(value, 64u)) {
            return 0;
        }
        written = snprintf(
            buffer + used,
            capacity - used,
            "%s\"%s\"",
            first ? "" : ",",
            value
        );
        if (written < 0 || (size_t)written >= capacity - used) {
            return 0;
        }
        used += (size_t)written;
        first = 0;
    }
    written = snprintf(
        buffer + used,
        capacity - used,
        "],\"issued_at\":%lld,\"expires_at\":%lld}",
        (long long)issued_at,
        (long long)expires_at
    );
    return written >= 0 && (size_t)written < capacity - used;
}

static int apply_license_json(
    xls_license_manager *manager,
    const char *license_json,
    int permit_expired,
    char *message,
    size_t message_capacity
)
{
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        license_json,
        strlen(license_json) + 1u,
        &parse_end,
        1
    );
    cJSON *key_item;
    cJSON *plan_item;
    cJSON *device_item;
    cJSON *components_item;
    cJSON *issued_item;
    cJSON *expires_item;
    cJSON *signature_item;
    const char *key_id;
    const char *plan;
    const char *device_hash;
    const char *signature_text;
    int64_t issued_at;
    int64_t expires_at;
    xls_license_plan parsed_plan;
    char payload[2048];
    unsigned char signature[64];
    size_t signature_length = 0u;
    unsigned char public_key[32];
    const int64_t now = current_time_seconds();
    int result = 0;
    if (!cJSON_IsObject(root)) {
        set_message(message, message_capacity, "授权文件格式无效");
        goto cleanup;
    }
    key_item = unique_object_item(root, "key_id");
    plan_item = unique_object_item(root, "plan");
    device_item = unique_object_item(root, "device_hash");
    components_item = unique_object_item(root, "device_components");
    issued_item = unique_object_item(root, "issued_at");
    expires_item = unique_object_item(root, "expires_at");
    signature_item = unique_object_item(root, "signature");
    if (!cJSON_IsString(key_item) || !cJSON_IsString(plan_item)
        || !cJSON_IsString(device_item) || !cJSON_IsArray(components_item)
        || !cJSON_IsString(signature_item)
        || !checked_json_integer(issued_item, &issued_at)
        || !checked_json_integer(expires_item, &expires_at)) {
        set_message(message, message_capacity, "授权文件字段无效");
        goto cleanup;
    }
    key_id = key_item->valuestring;
    plan = plan_item->valuestring;
    device_hash = device_item->valuestring;
    signature_text = signature_item->valuestring;
    if (!valid_token_text(key_id, 79u)
        || !valid_token_text(plan, 40u)
        || (device_hash[0] != '\0'
            && (!valid_token_text(device_hash, 128u)
                || strlen(device_hash) < 16u))
        || !parse_plan_text(plan, &parsed_plan)
        || !build_canonical_payload(
            key_id,
            plan,
            device_hash,
            components_item,
            issued_at,
            expires_at,
            payload,
            sizeof(payload)
        )) {
        set_message(message, message_capacity, "授权文件内容无效");
        goto cleanup;
    }
    if (!base64_url_decode(
        signature_text,
        signature,
        sizeof(signature),
        &signature_length
    ) || signature_length != sizeof(signature)) {
        set_message(message, message_capacity, "授权签名格式无效");
        goto cleanup;
    }
    if (!license_public_key(public_key)
        || crypto_ed25519_check(
            signature,
            public_key,
            (const unsigned char *)payload,
            strlen(payload)
        ) != 0) {
        set_message(message, message_capacity, "授权签名验证失败");
        goto cleanup;
    }
    if (!check_device_binding(manager, device_hash, components_item)) {
        set_message(message, message_capacity, "授权文件与当前设备不匹配");
        goto cleanup;
    }
    if (issued_at > now + XLSONE_CLOCK_TOLERANCE_SECONDS) {
        set_message(message, message_capacity, "系统时间异常，请校准后重试");
        goto cleanup;
    }
    (void)snprintf(
        manager->info.key_id,
        sizeof(manager->info.key_id),
        "%s",
        key_id
    );
    (void)snprintf(
        manager->info.device_hash,
        sizeof(manager->info.device_hash),
        "%s",
        device_hash
    );
    manager->info.plan = parsed_plan;
    manager->info.issued_at = issued_at;
    manager->info.expires_at = expires_at;
    if (expires_at > 0 && expires_at + XLSONE_GRACE_SECONDS <= now) {
        manager->state = XLS_LICENSE_EXPIRED;
        set_message(message, message_capacity, "授权已过期");
        if (!permit_expired) {
            goto cleanup;
        }
    } else {
        manager->state = manager->info.plan == XLS_LICENSE_PLAN_TRIAL
            ? XLS_LICENSE_TRIAL
            : XLS_LICENSE_ACTIVATED;
        set_message(
            message,
            message_capacity,
            manager->state == XLS_LICENSE_TRIAL
                ? "试用已启用"
                : "激活成功"
        );
    }
    if (!xls_platform_write_license(license_json, now)) {
        set_message(message, message_capacity, "授权有效，但无法保存到本机");
        goto cleanup;
    }
    result = 1;

cleanup:
    cJSON_Delete(root);
    return result;
}

static int apply_server_response(
    xls_license_manager *manager,
    const char *body,
    long status,
    int trial,
    char *message,
    size_t message_capacity
)
{
    const char *parse_end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(
        body, strlen(body) + 1u, &parse_end, 1
    );
    cJSON *license;
    char *license_json;
    int result;
    if (status != 200 || !cJSON_IsObject(root)) {
        cJSON *error = cJSON_IsObject(root)
            ? cJSON_GetObjectItemCaseSensitive(root, "error")
            : NULL;
        cJSON *server_message = cJSON_IsObject(root)
            ? cJSON_GetObjectItemCaseSensitive(root, "message")
            : NULL;
        const char *code = error != NULL
            && cJSON_IsString(error)
            && error->valuestring != NULL
            ? error->valuestring
            : "";
        if (strcmp(code, "KEY_NOT_FOUND") == 0) {
            set_message(message, message_capacity, "激活码不存在");
        } else if (strcmp(code, "KEY_REVOKED") == 0) {
            set_message(message, message_capacity, "激活码已被吊销");
        } else if (strcmp(code, "EXHAUSTED") == 0
            || strcmp(code, "DEVICE_LIMIT") == 0) {
            set_message(message, message_capacity, "激活次数已用尽（最多 3 台设备）");
        } else if (strcmp(code, "SUBSCRIPTION_EXPIRED") == 0) {
            set_message(message, message_capacity, "该激活码已过期");
        } else if (strcmp(code, "TRIAL_EXPIRED") == 0) {
            set_message(message, message_capacity, "这台设备的免费试用已结束");
            manager->state = XLS_LICENSE_EXPIRED;
        } else if (strcmp(code, "RATE_LIMITED") == 0) {
            set_message(message, message_capacity, "请求过于频繁，请稍后重试");
        } else if (server_message != NULL
            && cJSON_IsString(server_message)
            && server_message->valuestring != NULL) {
            set_message(message, message_capacity, server_message->valuestring);
        } else {
            set_message(
                message,
                message_capacity,
                trial ? "试用启用失败" : "激活失败"
            );
        }
        cJSON_Delete(root);
        return 0;
    }
    license = unique_object_item(root, "license");
    if (!cJSON_IsObject(license)) {
        set_message(message, message_capacity, "激活服务器响应异常");
        cJSON_Delete(root);
        return 0;
    }
    license_json = cJSON_PrintUnformatted(license);
    if (license_json == NULL) {
        set_message(message, message_capacity, "内存不足，无法处理授权");
        cJSON_Delete(root);
        return 0;
    }
    result = apply_license_json(
        manager, license_json, 0, message, message_capacity
    );
    cJSON_free(license_json);
    cJSON_Delete(root);
    return result;
}

static char *activation_payload(
    const xls_license_manager *manager,
    const char *key
)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *components;
    char host[256];
    char *payload;
    size_t index;
    if (root == NULL) {
        return NULL;
    }
    if (key != NULL) {
        cJSON_AddStringToObject(root, "key", key);
    }
    cJSON_AddStringToObject(
        root, "device_hash", manager->device_fingerprint
    );
    memset(host, 0, sizeof(host));
    (void)xls_platform_host_name(host, sizeof(host));
    cJSON_AddStringToObject(root, "device_name", host);
    components = cJSON_AddArrayToObject(root, "device_components");
    if (components == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    for (index = 0u; index < manager->component_count; ++index) {
        cJSON_AddItemToArray(
            components,
            cJSON_CreateString(manager->component_hashes[index])
        );
    }
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

static int perform_activation_request(
    xls_license_manager *manager,
    const char *endpoint,
    const char *key,
    int trial,
    char *message,
    size_t message_capacity
)
{
    char url[256];
    char *payload = activation_payload(manager, key);
    char *response = NULL;
    long status = 0;
    int result;
    if (payload == NULL) {
        set_message(message, message_capacity, "内存不足，无法发起激活");
        return 0;
    }
    (void)snprintf(
        url, sizeof(url), "%s%s", XLSONE_ACTIVATION_BASE_URL, endpoint
    );
    if (!xls_platform_http_request(
        "POST", url, payload, &response, &status
    )) {
        cJSON_free(payload);
        set_message(message, message_capacity, "网络连接失败，请检查网络");
        return 0;
    }
    cJSON_free(payload);
    result = apply_server_response(
        manager,
        response,
        status,
        trial,
        message,
        message_capacity
    );
    free(response);
    return result;
}

void xls_license_manager_init(xls_license_manager *manager)
{
    char *persisted = NULL;
    int64_t last_seen = 0;
    char message[160];
    const int64_t now = current_time_seconds();
    memset(manager, 0, sizeof(*manager));
    manager->state = XLS_LICENSE_UNACTIVATED;
    collect_fingerprint(manager);
    if (xls_platform_read_license(&persisted, &last_seen)) {
        if (last_seen > now + XLSONE_CLOCK_TOLERANCE_SECONDS) {
            manager->state = XLS_LICENSE_EXPIRED;
            free(persisted);
            return;
        } else if (apply_license_json(
            manager, persisted, 1, message, sizeof(message)
        )) {
            free(persisted);
            return;
        }
        free(persisted);
    }
    manager->state = XLS_LICENSE_UNACTIVATED;
    memset(&manager->info, 0, sizeof(manager->info));
}

int xls_license_is_full(const xls_license_manager *manager)
{
    const int64_t now = current_time_seconds();
    if (manager->state != XLS_LICENSE_ACTIVATED
        && manager->state != XLS_LICENSE_TRIAL) {
        return 0;
    }
    return manager->info.expires_at == 0
        || manager->info.expires_at + XLSONE_GRACE_SECONDS > now;
}

int xls_license_remaining_days(const xls_license_manager *manager)
{
    const int64_t now = current_time_seconds();
    int64_t seconds;
    if (manager->info.expires_at <= now) {
        return 0;
    }
    seconds = manager->info.expires_at - now;
    return (int)((seconds + 24LL * 60LL * 60LL - 1LL)
        / (24LL * 60LL * 60LL));
}

int xls_license_grace_days(const xls_license_manager *manager)
{
    const int64_t now = current_time_seconds();
    int64_t seconds;
    if (manager->info.expires_at <= 0 || manager->info.expires_at >= now) {
        return 0;
    }
    seconds = manager->info.expires_at + XLSONE_GRACE_SECONDS - now;
    if (seconds <= 0) {
        return 0;
    }
    return (int)((seconds + 24LL * 60LL * 60LL - 1LL)
        / (24LL * 60LL * 60LL));
}

int xls_license_max_import_files(const xls_license_manager *manager)
{
    return xls_license_is_full(manager)
        ? 2147483647
        : XLSONE_RESTRICTED_FILE_COUNT;
}

const char *xls_license_watermark(const xls_license_manager *manager)
{
    if (xls_license_is_full(manager)) {
        return "";
    }
    return manager->state == XLS_LICENSE_EXPIRED
        ? "授权已过期 — xlsOne"
        : "未激活试用版 — xlsOne";
}

const char *xls_license_state_text(
    const xls_license_manager *manager,
    char *buffer,
    size_t capacity
)
{
    const int remaining = xls_license_remaining_days(manager);
    const int grace = xls_license_grace_days(manager);
    if (manager->state == XLS_LICENSE_ACTIVATED) {
        if (grace > 0) {
            (void)snprintf(buffer, capacity, "宽限期 · 剩余 %d 天", grace);
        } else if (remaining > 0
            && manager->info.plan == XLS_LICENSE_PLAN_PERSONAL_YEARLY) {
            (void)snprintf(buffer, capacity, "已激活 · 剩余 %d 天", remaining);
        } else {
            (void)snprintf(buffer, capacity, "已激活");
        }
    } else if (manager->state == XLS_LICENSE_TRIAL) {
        if (remaining > 0) {
            (void)snprintf(buffer, capacity, "试用期 · 剩余 %d 天", remaining);
        } else if (grace > 0) {
            (void)snprintf(buffer, capacity, "试用宽限 · 剩余 %d 天", grace);
        } else {
            (void)snprintf(buffer, capacity, "试用期");
        }
    } else if (manager->state == XLS_LICENSE_EXPIRED) {
        (void)snprintf(buffer, capacity, "已过期");
    } else {
        (void)snprintf(buffer, capacity, "未授权 · 功能受限");
    }
    return buffer;
}

const char *xls_license_plan_text(xls_license_plan plan)
{
    switch (plan) {
    case XLS_LICENSE_PLAN_TRIAL:
        return "试用版";
    case XLS_LICENSE_PLAN_PERSONAL_YEARLY:
        return "个人年度版";
    case XLS_LICENSE_PLAN_ENTERPRISE:
        return "企业版";
    case XLS_LICENSE_PLAN_PERSONAL_LIFETIME:
    default:
        return "个人终身版";
    }
}

int xls_license_normalize_key(
    const char *input,
    char *output,
    size_t capacity
)
{
    char compact[17];
    size_t compact_length = 0u;
    size_t index;
    if (input == NULL || capacity < 20u) {
        return 0;
    }
    for (index = 0u; input[index] != '\0'; ++index) {
        const unsigned char character = (unsigned char)input[index];
        if (isalnum(character)) {
            if (compact_length >= 16u) {
                return 0;
            }
            compact[compact_length++] = (char)toupper(character);
        } else if (character != '-' && !isspace(character)) {
            return 0;
        }
    }
    if (compact_length != 16u) {
        return 0;
    }
    compact[16] = '\0';
    (void)snprintf(
        output,
        capacity,
        "%.4s-%.4s-%.4s-%.4s",
        compact,
        compact + 4,
        compact + 8,
        compact + 12
    );
    return 1;
}

int xls_license_activate(
    xls_license_manager *manager,
    const char *key,
    char *message,
    size_t message_capacity
)
{
    char normalized[20];
    if (!xls_license_normalize_key(key, normalized, sizeof(normalized))) {
        set_message(message, message_capacity, "激活码格式不正确");
        return 0;
    }
    return perform_activation_request(
        manager,
        "/api/activate/windows",
        normalized,
        0,
        message,
        message_capacity
    );
}

int xls_license_request_trial(
    xls_license_manager *manager,
    char *message,
    size_t message_capacity
)
{
    return perform_activation_request(
        manager,
        "/api/trial/windows",
        NULL,
        1,
        message,
        message_capacity
    );
}

int xls_license_import_file(
    xls_license_manager *manager,
    const char *path,
    char *message,
    size_t message_capacity
)
{
    char *contents = NULL;
    int result;
    if (!xls_platform_read_text_file(path, &contents)) {
        set_message(message, message_capacity, "无法读取授权文件");
        return 0;
    }
    result = apply_license_json(
        manager, contents, 0, message, message_capacity
    );
    free(contents);
    return result;
}
