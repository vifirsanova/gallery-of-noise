#pragma once

#ifdef BIOMUSIC_HAVE_CATCH2
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
using Catch::Approx;
#else
// Minimal Catch2-compatible fallback so tests compile & run even without Catch2 installed
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct Approx {
    double val;
    double eps = 1e-4;
    explicit Approx(double v) : val(v) {}
    Approx& margin(double m) { eps = m; return *this; }
    Approx& epsilon(double e) { eps = e * (1.0 + std::fabs(val)); return *this; }
    friend bool operator==(double lhs, const Approx& rhs) {
        return std::fabs(lhs - rhs.val) <= rhs.eps;
    }
    friend bool operator==(const Approx& lhs, double rhs) { return rhs == lhs; }
    friend bool operator!=(double lhs, const Approx& rhs) { return !(lhs == rhs); }
};

namespace minicatch {
struct TestReg {
    const char* name;
    std::function<void()> fn;
};
inline std::vector<TestReg>& registry() {
    static std::vector<TestReg> r;
    return r;
}
struct Registrar {
    Registrar(const char* name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};
} // namespace minicatch

#define BIOMUS_CAT2(a, b) a##b
#define BIOMUS_CAT(a, b) BIOMUS_CAT2(a, b)
#define TEST_CASE(name, ...) \
    static void BIOMUS_CAT(test_fn_, __LINE__)(); \
    static ::minicatch::Registrar BIOMUS_CAT(test_reg_, __LINE__)(name, BIOMUS_CAT(test_fn_, __LINE__)); \
    static void BIOMUS_CAT(test_fn_, __LINE__)()
#define SECTION(name) if (true)
#define REQUIRE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "REQUIRE failed at " << __FILE__ << ":" << __LINE__ << ": " #expr "\n"; \
            std::abort(); \
        } \
    } while (0)
#define CHECK(expr) REQUIRE(expr)
#endif
