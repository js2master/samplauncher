#pragma once

#include <cmath>
#include <cstdio>

namespace check {
inline int g_checked = 0;
inline int g_failed = 0;

inline int finish()
{
	std::printf("%d checks, %d failed\n", g_checked, g_failed);
	return g_failed ? 1 : 0;
}
}

#define CHECK(cond) \
	do { \
		++check::g_checked; \
		if (!(cond)) { \
			++check::g_failed; \
			std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

#define CHECK_EQ(a, b) \
	do { \
		++check::g_checked; \
		const long long va_ = static_cast<long long>(a); \
		const long long vb_ = static_cast<long long>(b); \
		if (va_ != vb_) { \
			++check::g_failed; \
			std::printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
		} \
	} while (0)

#define CHECK_NEAR(a, b, eps) \
	do { \
		++check::g_checked; \
		const double va_ = static_cast<double>(a); \
		const double vb_ = static_cast<double>(b); \
		if (std::fabs(va_ - vb_) > (eps)) { \
			++check::g_failed; \
			std::printf("  FAIL %s:%d: %s ~= %s (%g vs %g)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
		} \
	} while (0)

#define RUN(fn) \
	do { \
		std::printf("[ RUN ] %s\n", #fn); \
		fn(); \
	} while (0)
