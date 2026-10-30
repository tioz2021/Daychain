#include "tests.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "infra/util.h"

static void test_dates(void) {
    int32_t d = 0;
    CHECK(date_parse("2026-10-30", &d));
    char buf[11];
    date_format(d, buf);
    CHECK_STR(buf, "2026-10-30");

    CHECK(!date_parse("2023-02-29", NULL));
    CHECK(date_parse("2024-02-29", NULL));
    CHECK(!date_parse("2026-13-01", NULL));
    CHECK(!date_parse("2026-00-10", NULL));
    CHECK(!date_parse("2026-1-01", NULL));
    CHECK(!date_parse("2026-10-32", NULL));
    CHECK(!date_parse("", NULL));
    CHECK(!date_parse(NULL, NULL));
    CHECK(!date_valid("30.10.2026"));

    CHECK(date_iso_weekday(0) == 4); /* 1970-01-01, Thursday */
    CHECK(date_parse("2026-10-30", &d));
    CHECK(date_iso_weekday(d) == 5); /* Friday */
    date_format(date_week_start(d), buf);
    CHECK_STR(buf, "2026-10-26");

    /* Round-trip over a wide range of dates. */
    for (int32_t x = -20000; x < 40000; x += 37) {
        date_format(x, buf);
        int32_t back = 0;
        CHECK(date_parse(buf, &back));
        CHECK(back == x);
    }

    date_format(0, buf);
    CHECK_STR(buf, "1970-01-01");
}

static void test_utf8(void) {
    CHECK(utf8_valid("hello", 5));
    CHECK(utf8_valid("\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", 12));
    CHECK(utf8_valid("\xf0\x9f\x98\x80", 4));
    CHECK(!utf8_valid("\xc3\x28", 2));
    CHECK(!utf8_valid("\x80", 1));
    CHECK(!utf8_valid("\xed\xa0\x80", 3)); /* surrogate */
    CHECK(!utf8_valid("\xf5\x80\x80\x80", 4)); /* > U+10FFFF */

    CHECK(utf8_chars("hello") == 5);
    CHECK(utf8_chars("\xd0\xbf\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82") == 6);

    char s[32];
    snprintf(s, sizeof s, "%s", "привет");
    utf8_trunc_chars(s, 3);
    CHECK_STR(s, "при");
    utf8_trunc_chars(s, 100);
    CHECK_STR(s, "при");
}

static void test_time(void) {
    char now[32];
    now_iso8601(now);
    CHECK(strlen(now) >= 25);
    CHECK(datetime_valid(now));
    CHECK(!datetime_valid("2026-10-30"));
    CHECK(datetime_valid("2026-10-30T19:45:00+02:00"));
}

static void test_strbuf(void) {
    StrBuf sb;
    sb_init(&sb);
    sb_adds(&sb, "abc");
    sb_addc(&sb, '/');
    sb_addf(&sb, "%d", 42);
    CHECK_STR(sb.data, "abc/42");
    char *s = sb_take(&sb);
    CHECK_STR(s, "abc/42");
    free(s);
    sb_free(&sb);
}

void test_util(void) {
    test_dates();
    test_utf8();
    test_time();
    test_strbuf();
}
