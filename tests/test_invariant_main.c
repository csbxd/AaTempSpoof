#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Forward declaration of the function from main.c
extern void vulnerable_function(uint8_t *out, size_t out_capacity, 
                               const uint8_t *tmp, size_t m, size_t *out_len);

START_TEST(test_memcpy_bounds_invariant)
{
    // Invariant: memcpy must never write beyond out_capacity bytes
    const size_t payloads_m[] = {
        1024,           // Exact exploit case: m larger than remaining capacity
        256,            // Boundary case: m equals remaining capacity
        128             // Valid input: m smaller than remaining capacity
    };
    
    const size_t out_capacity = 512;
    uint8_t *out = malloc(out_capacity);
    uint8_t tmp[1024];
    memset(tmp, 0x41, sizeof(tmp));  // Fill with 'A's
    
    int num_payloads = sizeof(payloads_m) / sizeof(payloads_m[0]);
    
    for (int i = 0; i < num_payloads; i++) {
        size_t m = payloads_m[i];
        size_t initial_len = 256;  // Half of capacity already used
        size_t out_len = initial_len;
        
        // Create canary after buffer to detect overflow
        uint8_t *canary_region = malloc(out_capacity * 2);
        uint8_t canary_value = 0xCC;
        memset(canary_region, canary_value, out_capacity * 2);
        
        // Copy out buffer to canary region with padding
        memcpy(canary_region + out_capacity, out, out_capacity);
        
        vulnerable_function(out, out_capacity, tmp, m, &out_len);
        
        // Check that canary after buffer wasn't modified
        for (size_t j = 0; j < out_capacity; j++) {
            ck_assert_msg(canary_region[out_capacity + j] == canary_value,
                         "Buffer overflow detected! Canary corrupted at offset %zu", j);
        }
        
        free(canary_region);
    }
    
    free(out);
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_memcpy_bounds_invariant);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}