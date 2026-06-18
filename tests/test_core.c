#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "json_utils.h"
#include "recommendation.h"

static void test_recommendations(void) {
    assert(recommendation_get(75, 70, 0) == REC_OPEN);
    assert(recommendation_get(75, 74, 0) == REC_NONE);
    assert(recommendation_get(70, 73, 1) == REC_CLOSE);
    assert(recommendation_get(70, 71, 1) == REC_NONE);

    assert(strcmp(recommendation_name(REC_OPEN), "open") == 0);
    assert(strcmp(recommendation_name(REC_CLOSE), "close") == 0);
    assert(strcmp(recommendation_name(REC_NONE), "none") == 0);

    assert(recommendation_should_send(REC_OPEN, REC_NONE, 0, 100));
    assert(!recommendation_should_send(REC_OPEN, REC_OPEN, 50, 100));
}

static void test_json_parse_int(void) {
    int value = 0;

    assert(json_parse_int("{\"inside\":72,\"oa\":65,\"power\":0}", "inside", &value));
    assert(value == 72);

    assert(json_parse_int("{ \"oa\" : -4 }", "oa", &value));
    assert(value == -4);

    assert(!json_parse_int("{\"inside\":\"warm\"}", "inside", &value));
    assert(!json_parse_int("{\"outside\": 65}", "inside", &value));
}

int main(void) {
    test_recommendations();
    test_json_parse_int();
    puts("core tests passed");
    return 0;
}
