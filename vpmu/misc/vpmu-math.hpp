#ifndef __VPMU_MATH_HPP_
#define __VPMU_MATH_HPP_
#pragma once

#include <cmath>
#include <valarray>
#include <vector>

namespace vpmu
{

namespace math
{
    template <typename T = std::valarray<double>>
    inline double l2_norm(const T &vec)
    {
        double accum = 0.;
        for (double x : vec) {
            accum += x * x;
        }
        return std::sqrt(accum);
    }

    template <typename T = std::valarray<double>>
    inline void normalize(T &vec)
    {
        double l2n = l2_norm(vec);

        for (int i = 0; i < vec.size(); i++) {
            vec[i] /= l2n;
        }
        return;
    }

    template <typename T = std::valarray<double>>
    inline void normalize(const T &in_v, T &out_v)
    {
        double l2n = l2_norm(in_v);

        if (in_v.size() != out_v.size())
            throw std::out_of_range("Two vectors size does not match");
        for (int i = 0; i < out_v.size(); i++) {
            out_v[i] = in_v[i] / l2n;
        }
        return;
    }

    inline uint64_t simple_hash(uint64_t key, uint64_t m) { return (key % m); }

    // http://zimbry.blogspot.tw/2011/09/better-bit-mixing-improving-on.html
    inline uint64_t bitmix_hash(uint64_t key)
    {
        key = (key ^ (key >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        key = (key ^ (key >> 27)) * UINT64_C(0x94d049bb133111eb);
        key = key ^ (key >> 31);

        return key;
    }

    inline uint32_t ilog2(uint32_t x)
    {
        uint32_t i;
        for (i = -1; x != 0; i++) x >>= 1;
        return i;
    }

    inline uint64_t sum_cores(uint64_t value[])
    {
        uint64_t sum = 0;
        for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
            sum += value[i];
        }
        return sum;
    }
} // End of namespace vpmu::math
}

#endif
