#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Set to 1 at compile time to restore OEM package/signature binding. */
#ifndef KKSDK_ENFORCE_PACKAGE_VERIFY
#define KKSDK_ENFORCE_PACKAGE_VERIFY 0
#endif

int kksdk_dev_bypass_package_verify_enabled(void);
int kksdk_verify_package_signature(void *env, void *context, void *expected_hash,
        char **package_name_out);
int kksdk_is_whitelisted_package(const char *package_name);
void kksdk_free_string(char *value);

#ifdef __cplusplus
}
#endif
