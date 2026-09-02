#include "unity.h"
#include "features.h"

TEST_CASE("DC signal has zero AC rms", "[features]") {
    float w[64];
    for(int i=0; i<64; i++) w[i] = 1.0f;

    float rms, peak;
    compute_features(w, 64, &rms, &peak);

    TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.0f, rms);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 0.0f, peak);
}

TEST_CASE("a single spike is caught by peak", "[features]") {
    float w[4] = {0,0,0.5f, 0};

    float rms,peak;
    compute_features(w,4,&rms,&peak);

    TEST_ASSERT_TRUE(peak > 0.3f);
}

TEST_CASE("alternating +/-1 has rms of 1", "[features]") {
    float w[4] = {1,-1,1,-1};

    float rms,peak;
    compute_features(w,4, &rms, &peak);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 1.0f, rms);
}