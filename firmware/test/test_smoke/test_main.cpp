// Task 0 smoke test: proves the native Unity test pipeline works end to end
// (pio test -e native). It intentionally does not depend on MirrorCore
// content yet — that arrives in later tasks.
#include <unity.h>

void setUp(void) {
    // no-op: nothing to initialize for this smoke test
}

void tearDown(void) {
    // no-op: nothing to tear down for this smoke test
}

static void test_native_pipeline_smoke(void) {
    TEST_ASSERT_EQUAL_INT(4, 2 + 2);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_native_pipeline_smoke);
    return UNITY_END();
}
