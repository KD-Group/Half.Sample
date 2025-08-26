#include <cmath>
#include <Eigen/Dense>
#include "estimate.hpp"
#include "../constant.hpp"

namespace Estimate {

    EstimatedResult::EstimatedResult(const Waveform& wave, double tau) : tau(tau), y(wave.values), interval(wave.interval) {
        calculate_para();
        calculate_loss();
    };

    void EstimatedResult::calculate_para() {
        int m = y->size();

        double sum_x = 0, sum_y = 0;
        double sum_xx = 0, sum_xy = 0;

        for (int i = 0; i < m; i++) {
            double vx = exp(i * interval / -tau);
            double vy = (*y)[i];

            sum_x += vx;
            sum_y += vy;
            sum_xx += vx * vx;
            sum_xy += vx * vy;
        }

        double mean_x = sum_x / m;
        double mean_y = sum_y / m;

        w = (sum_xy / m - mean_x * mean_y) / (sum_xx / m - mean_x * mean_x);
        b = mean_y - w * mean_x;
    }

    void EstimatedResult::calculate_loss() {
        int m = y->size();

        double loss_sum = 0;
        for (int i = 0; i < m; i++) {
            loss_sum += pow(exp(i * interval / -tau) * w + b - (*y)[i], 2);
        }

        loss = loss_sum / m;
    }

    double EstimatedResult::margin() {
        int m = y->size();
        double minimum = b + w;
        double maximum = b + w * exp(m * interval / -tau);
        return maximum - minimum;
    }

    EstimatedResult one_third_search(const Waveform& wave) {
        double l = Constant::MinTauValue / 10, r = Constant::MaxTauValue * 2;  // range of tau

        while (l + Constant::SearchEpsilon < r) {
            double tau_l = (l + l + r) / 3;
            double tau_r = (l + r + r) / 3;

            EstimatedResult estimate_l(wave, tau_l);
            EstimatedResult estimate_r(wave, tau_r);

            if (estimate_l.loss < estimate_r.loss) {
                r = tau_r;
            } else {
                l = tau_l;
            }
        }

        return {wave, (l + r) / 2};
    }

    // y = w * exp(α * x) + b
    EstimatedResult eigen_fit_exponential(const Waveform& wave) {
        // 使用三分法确定一个较好的初始tau值
        double l = Constant::MinTauValue / 10, r = Constant::MaxTauValue * 2;

        // 减少三分法的迭代次数，仅用于获取较好的初始值
        for (int i = 0; i < 20; ++i) { // 限制迭代次数
            double tau_l = (l + l + r) / 3;
            double tau_r = (l + r + r) / 3;

            EstimatedResult estimate_l(wave, tau_l);
            EstimatedResult estimate_r(wave, tau_r);

            if (estimate_l.loss < estimate_r.loss) {
                r = tau_r;
            } else {
                l = tau_l;
            }
        }

        // 使用三分法得到的较好初始值作为起点
        double initial_tau = (l + r) / 2;

        // 使用Eigen进行非线性最小二乘拟合
        double tau = initial_tau;
        double w;
        double b;

        const int maxIterations = 1000;
        const double tolerance = Constant::SearchEpsilon;

        // 首先计算初始参数下的loss作为参考
        {
            EstimatedResult initial_result(wave, initial_tau);
            w = initial_result.w;
            b = initial_result.b;
        }

        double current_loss = 0.0;
        for (int iter = 0; iter < maxIterations; ++iter) {
            Eigen::MatrixXd J(wave.values->size(), 3);
            Eigen::VectorXd r_vec(wave.values->size());

            // 计算每个数据点的残差和雅可比矩阵
            for (size_t i = 0; i < wave.values->size(); ++i) {
                double x = i * wave.interval;  // 使用实际的时间间隔作为x值
                double y_measured = wave.values->operator[](i);
                double y_predicted = w * std::exp(-x / tau) + b; // 注意这里是-x/tau

                // 残差: r = y_measured - y_predicted
                double residual = y_measured - y_predicted;
                r_vec(i) = residual;
                current_loss += residual * residual;

                // 计算偏导数（雅可比矩阵的行）
                // 注意：模型是 y = w * exp(-x/tau) + b
                // dy/dtau = w * exp(-x/tau) * x / (tau*tau)
                J(i, 0) = w * std::exp(-x / tau) * x / (tau * tau);
                // dy/dw = exp(-x/tau)
                J(i, 1) = std::exp(-x / tau);
                // dy/db = 1
                J(i, 2) = 1.0;
            }

            current_loss = current_loss / wave.values->size();

            // 使用QR分解求解: J * Δp = r
            Eigen::VectorXd delta = J.colPivHouseholderQr().solve(r_vec);

            // 更新参数
            double new_tau = tau + delta(0);
            double new_w = w + delta(1);
            double new_b = b + delta(2);

            // 参数边界检查
            if (new_tau > Constant::MinTauValue && new_tau < Constant::MaxTauValue) {
                tau = new_tau;
            }
            w = new_w;
            b = new_b;

            // 检查收敛性
            if (delta.norm() < tolerance) {
                break;
            }
        }

        // 创建结果对象并确保loss字段正确设置
        EstimatedResult result(wave, tau);
        // 重新计算最终的w和b参数以确保一致性
        result.w = w;
        result.b = b;
        result.y = wave.values;
        result.interval = wave.interval;

        // 重新计算loss以确保它被正确设置
        double final_loss = 0.0;
        for (size_t i = 0; i < wave.values->size(); ++i) {
            double x = i * wave.interval;
            double y_measured = wave.values->operator[](i);
            double y_predicted = w * std::exp(-x / tau) + b;
            double residual = y_measured - y_predicted;
            final_loss += residual * residual;
        }
        result.loss = final_loss / wave.values->size();
        return result;
    }

    bool is_wave_going_down(const Waveform& wave) {
        const auto &y = *wave.values;
        int m = y.size();
        int n = m / 3;

        double sum_x = 0, sum_y = 0;
        double sum_xx = 0, sum_xy = 0;

        for (int i = m - n; i < m; i++) {
            double vx = i;
            double vy = y[i];

            sum_x += vx;
            sum_y += vy;
            sum_xx += vx * vx;
            sum_xy += vx * vy;
        }

        double mean_x = sum_x / n;
        double mean_y = sum_y / n;

        double w = (sum_xy / n - mean_x * mean_y) / (sum_xx / n - mean_x * mean_x);
        return w * n <= Constant::WaveGoingDownThreshold;
    }

} // namespace Estimate
