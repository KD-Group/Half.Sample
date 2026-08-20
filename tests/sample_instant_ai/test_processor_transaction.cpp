#include "../../src/processor/processor.hpp"
#include "../../src/global/global.hpp"
#include "../../src/commander/measure.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>

namespace Commander { void measure(); }

namespace {

class RetryThenFailSampler : public Sampler::Sampler {
  public:
    bool sample(const Config::SamplingConfig&, Result::SamplingResult& result) override {
        ++attempts;
        if (attempts == 1) {
            result.totalSamplingBuffer = {0.0, 1.0};
            result.independent_cycle_accumulator.push_back({42.0});
            return true;
        }
        accumulator_preserved = result.independent_cycle_accumulator.size() == 1 &&
                                result.independent_cycle_accumulator[0][0] == 42.0;
        result.error_code = Error::ErrorHandleNotValid;
        return false;
    }

    std::string name() override { return "retry_then_fail"; }
    double get_value(const std::string&) override { return 0.0; }
    bool set_value(const std::string&, double) override { return false; }

    int attempts{};
    bool accumulator_preserved{};
};

void assert_non_finite_result_is_rejected(double Result::SamplingResult::* field, double value) {
    Config::SamplingConfig config;
    config.sampling_interval = 0.1;
    Result::SamplingResult result(false);
    result.success = true;
    result.maximum = 1.0;
    result.minimum = 0.0;
    result.estimate.interval = 0.1;
    result.estimate.tau = 1.0;
    result.estimate.w = 1.0;
    result.estimate.b = 1.0;
    result.estimate.loss = 0.0;
    result.estimate.y.reset(new Vector(1, 1.0));
    result.*field = value;

    assert(!Commander::Processor::validate_finite_result(config, result));
    assert(!result.success);
    assert(result.error_code == Error::SAMPLING_RESULT_NOT_FINITE);
    assert(!result.estimate.y);
}

void assert_non_finite_estimate_is_rejected(double Estimate::EstimatedResult::* field, double value) {
    Config::SamplingConfig config;
    config.sampling_interval = 0.1;
    Result::SamplingResult result(false);
    result.success = true;
    result.maximum = 1.0;
    result.minimum = 0.0;
    result.estimate.interval = 0.1;
    result.estimate.tau = 1.0;
    result.estimate.w = 1.0;
    result.estimate.b = 1.0;
    result.estimate.loss = 0.0;
    result.estimate.y.reset(new Vector(1, 1.0));
    result.estimate.*field = value;

    assert(!Commander::Processor::validate_finite_result(config, result));
    assert(result.error_code == Error::SAMPLING_RESULT_NOT_FINITE);
}

} // namespace

void test_processor_transaction() {
    assert(Error::to_string(Error::SAMPLING_RESULT_NOT_FINITE) == "sampling_result_not_finite");
    assert(Error::category(Error::SAMPLING_RESULT_NOT_FINITE) == "state");
    assert(Error::retryable(Error::SAMPLING_RESULT_NOT_FINITE));
    assert(!Error::retryable(Error::FIT_NOT_IDENTIFIABLE));

    const double non_finite_values[] = {
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };
    for (double value : non_finite_values) {
        assert_non_finite_result_is_rejected(&Result::SamplingResult::maximum, value);
        assert_non_finite_result_is_rejected(&Result::SamplingResult::minimum, value);
        assert_non_finite_result_is_rejected(&Result::SamplingResult::cycle_maximum, value);
        assert_non_finite_result_is_rejected(&Result::SamplingResult::cycle_minimum, value);
        assert_non_finite_result_is_rejected(&Result::SamplingResult::v_inf, value);
        assert_non_finite_estimate_is_rejected(&Estimate::EstimatedResult::interval, value);
        assert_non_finite_estimate_is_rejected(&Estimate::EstimatedResult::tau, value);
        assert_non_finite_estimate_is_rejected(&Estimate::EstimatedResult::w, value);
        assert_non_finite_estimate_is_rejected(&Estimate::EstimatedResult::b, value);
        assert_non_finite_estimate_is_rejected(&Estimate::EstimatedResult::loss, value);

        Config::SamplingConfig config;
        config.sampling_interval = value;
        Result::SamplingResult result(false);
        result.success = true;
        result.estimate.y.reset(new Vector(1, 1.0));
        assert(!Commander::Processor::validate_finite_result(config, result));

        config.sampling_interval = 0.1;
        Result::SamplingResult wave_result(false);
        wave_result.success = true;
        wave_result.estimate.y.reset(new Vector(2, 1.0));
        (*wave_result.estimate.y)[1] = value;
        assert(!Commander::Processor::validate_finite_result(config, wave_result));
    }

    const char* const processing_modes[] = {"threshold_accumulation", "independent_cycle"};
    for (const char* mode : processing_modes) {
        Config::SamplingConfig mode_config;
        mode_config.sampling_interval = 0.1;
        mode_config.waveform_processing_mode = mode;
        Result::SamplingResult mode_result(false);
        mode_result.success = true;
        mode_result.maximum = std::numeric_limits<double>::infinity();
        assert(!Commander::Processor::validate_finite_result(mode_config, mode_result));
        assert(mode_result.error_code == Error::SAMPLING_RESULT_NOT_FINITE);
    }

    Config::SamplingConfig preserved_config;
    Result::SamplingResult preserved_error(false);
    preserved_error.success = false;
    preserved_error.error_code = Error::FIT_NOT_IDENTIFIABLE;
    preserved_error.maximum = std::numeric_limits<double>::infinity();
    assert(!Commander::Processor::validate_finite_result(preserved_config, preserved_error));
    assert(preserved_error.error_code == Error::FIT_NOT_IDENTIFIABLE);

    Result::SamplingResult invalid_voltage(false);
    invalid_voltage.v_inf = std::numeric_limits<double>::infinity();
    invalid_voltage.v_inf_valid = true;
    assert(!Commander::Processor::validate_finite_result(preserved_config, invalid_voltage));
    assert(invalid_voltage.error_code == Error::SUCCESS);
    assert(!invalid_voltage.v_inf_valid);
    assert(invalid_voltage.v_inf == 0.0);

    Result::SamplingResult missing_validity(false);
    missing_validity.success = true;
    missing_validity.maximum = 1.0;
    missing_validity.minimum = 0.0;
    missing_validity.estimate.interval = 0.1;
    missing_validity.estimate.tau = 1.0;
    missing_validity.estimate.w = 1.0;
    missing_validity.estimate.b = 1.0;
    missing_validity.estimate.loss = 0.0;
    missing_validity.estimate.y.reset(new Vector(1, 1.0));
    assert(!Commander::Processor::validate_finite_result(preserved_config, missing_validity));
    assert(missing_validity.error_code == Error::SAMPLING_RESULT_NOT_FINITE);

    Config::SamplingConfig failed_fit_config;
    failed_fit_config.auto_mode = false;
    assert(failed_fit_config.update(1, 50.0, 0.0, 100, 10.0, "independent_cycle"));
    Result::SamplingResult failed_fit(false);
    failed_fit.resultWave.assign(static_cast<std::size_t>(failed_fit_config.valid_length), 1.0);
    failed_fit.v_inf = 2.0;
    failed_fit.v_inf_valid = true;
    assert(!Commander::Processor::estimate(failed_fit_config, failed_fit));
    assert(failed_fit.error_code == Error::FIT_NOT_IDENTIFIABLE);
    assert(failed_fit.v_inf == 2.0);
    assert(failed_fit.v_inf_valid);

    const Sampler::SamplerPtr original_sampler = Global::sampler;
    const Config::SamplingConfig original_config = Global::config;
    std::shared_ptr<RetryThenFailSampler> retry_sampler(new RetryThenFailSampler());
    Global::sampler = retry_sampler;
    Global::config.auto_mode = false;
    assert(Global::config.update(2, 50.0, 0.0, 100, 10.0, "independent_cycle"));
    Global::result.totalSamplingBuffer.clear();
    Global::result.resultWave.assign(static_cast<std::size_t>(Global::config.valid_length), 0.0);
    Global::result.v_inf = 0.0;
    Global::result.v_inf_valid = false;
    Commander::measure();
    assert(retry_sampler->attempts == 2);
    assert(retry_sampler->accumulator_preserved);
    assert(!Global::result.success);
    assert(Global::result.error_code == Error::ErrorHandleNotValid);
    assert(Global::result.v_inf == 0.0);
    assert(!Global::result.v_inf_valid);
    Global::sampler = original_sampler;
    Global::config = original_config;

    Global::config.dump_file_path = "unchanged-config";
    Global::result.totalSamplingBuffer.assign(1, 123.0);
    Global::result.estimate.interval = 456.0;

    Config::SamplingConfig pending_config;
    pending_config.auto_mode = false;
    assert(pending_config.update(1, 0.05, 0.1, 100, 10.0));
    Result::SamplingResult pending_result(false);
    pending_result.resultWave.resize(50);
    for (std::size_t i = 0; i < pending_result.resultWave.size(); ++i)
        pending_result.resultWave[i] = 2.5 * std::exp(-static_cast<double>(i) / 10.0) + 5.0;

    assert(Commander::Processor::estimate(pending_config, pending_result));
    assert(pending_result.estimate.interval == pending_config.sampling_interval);
    assert(Global::config.dump_file_path == "unchanged-config");
    assert(Global::result.totalSamplingBuffer.size() == 1);
    assert(Global::result.totalSamplingBuffer[0] == 123.0);
    assert(Global::result.estimate.interval == 456.0);

    const char* const malformed_path = "cpp_build/transaction-malformed-v2.csv";
    {
        std::ofstream malformed(malformed_path);
        malformed << "#HALF_SAMPLE_INSTANT_AI_V2\nemitting_frequency=bad\n";
    }
    Global::result.success = true;
    std::istringstream command(std::string(malformed_path) + "\n");
    std::streambuf* original_input = std::cin.rdbuf(command.rdbuf());
    Commander::to_process();
    std::cin.rdbuf(original_input);
    assert(!Global::result.success);
    assert(Global::result.error_code == Error::INVALID_INSTANT_AI_CONFIG);
    assert(Global::config.dump_file_path == "unchanged-config");
    assert(Global::result.totalSamplingBuffer.size() == 1);
    assert(Global::result.totalSamplingBuffer[0] == 123.0);
    assert(Global::result.estimate.interval == 456.0);
    std::remove(malformed_path);
}
