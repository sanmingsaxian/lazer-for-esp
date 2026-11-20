// Minimal host test harness (no external deps).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <string>

static int g_checks = 0;
static int g_failures = 0;
static const char* g_case = "";

#define CASE(name) static void case_##name(); \
    static struct Reg_##name { Reg_##name() { run(#name, case_##name); } } reg_##name; \
    static void case_##name()

static void run(const char* name, void (*fn)()) {
    g_case = name;
    int before = g_failures;
    fn();
    if (g_failures == before) printf("  ok  %s\n", name);
}

#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_failures; \
    printf("FAIL  %s:%d  %s  [%s]\n", __FILE__, __LINE__, #cond, g_case); } } while (0)

#define CHECK_EQ(a, b) do { ++g_checks; auto va = (a); auto vb = (b); \
    if (!(va == vb)) { ++g_failures; \
    printf("FAIL  %s:%d  %s == %s  [%s]\n", __FILE__, __LINE__, #a, #b, g_case); } } while (0)

static int test_summary() {
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
