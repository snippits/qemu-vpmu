#ifndef __PHASE_CLASSIFIER_NN_HPP_
#define __PHASE_CLASSIFIER_NN_HPP_
#pragma once

#include <cmath>  // std::abs, std::fabs
#include <limits> // std::numeric_limits
#include <tuple>  // std::tuple

#include "phase.hpp"            // Phase class
#include "phase-classifier.hpp" // PhaseClassifier class

class NearestCluster : public PhaseClassifier
{
public:
    Phase &classify(std::vector<Phase> &phase_list, const Phase &phase) override
    {
        auto n_vector = phase.get_normalized_vector();
        // Calaulate the distances
        double distance_array[phase_list.size()];
        _PragmaVectorize
        for (int i = 0; i < phase_list.size(); i++) {
            auto &phase_n_vector = phase_list[i].get_normalized_vector();
            distance_array[i]    = manhatten_distance(phase_n_vector, n_vector);
        }

        // Find cloest distance
        int    idx;
        double min_val;
        std::tie(idx, min_val) = min_distance(distance_array, phase_list.size());

        if (idx == -1 || min_val > this->similarity_threshold) return Phase::not_found;
        return phase_list[idx];
    }

    Phase &classify(std::vector<Phase> &phase_list, const Window &window) override
    {
        auto n_vector = window.branch_vector;
        vpmu::math::normalize(n_vector);
        // Calaulate the distances
        double distance_array[phase_list.size()];
        _PragmaVectorize
        for (int i = 0; i < phase_list.size(); i++) {
            auto &phase_n_vector = phase_list[i].get_normalized_vector();
            distance_array[i]    = manhatten_distance(phase_n_vector, n_vector);
        }

        // Find cloest distance
        int    idx;
        double min_val;
        std::tie(idx, min_val) = min_distance(distance_array, phase_list.size());

        if (idx == -1 || min_val > this->similarity_threshold) return Phase::not_found;
        return phase_list[idx];
    }

private:
    inline std::tuple<int, double> min_distance(double distance_array[], int array_size)
    {
        // Find cloest distance
        int    idx     = -1;
        double min_val = std::numeric_limits<double>::max();
        for (int i = 0; i < array_size; i++) {
            double val = distance_array[i];
            if (0.0 < val && val < min_val) {
                idx     = i;
                min_val = val;
            }
        }
        return std::make_tuple(idx, min_val);
    }

    template <typename T = std::valarray<double>>
    inline double manhatten_distance(const T &v1, const T &v2)
    {
        double m_distance = 0.0f;
        _PragmaVectorize
        for (int i = 0; i < v1.size(); i++) {
            m_distance += std::abs(v1[i] - v2[i]);
        }
        return m_distance;
    }
};

#endif
