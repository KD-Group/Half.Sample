#ifndef CONSTANT_HPP
#define CONSTANT_HPP

namespace Constant {
    const int MaxSamplingFrequency = int(2e7);  // Hz
    const int MinSamplingFrequency = int(1e6);  // Hz
    const int HighSpeedSamplingThreshold = 10;  // Hz
    const int MaxSamplingPoints = int(16e6);

    const double HighSpeedEstimatedFrequencyUpperBound = 20480;  // Hz
    const double LowSpeedEstimatedFrequencyUpperBound = 8;  // Hz

    const int CroppedLength = 300;

    const int MaxBufferSize = MaxSamplingFrequency * 2;  // sample 2 second in max speed
    const int MaxAverageSize = 1000;

    const double MinVoltageAmplitude = 0.1;  // unit: V
    const double UpperBound = 0.4;  // percent
    const double LowerBound = 0.1;  // percent

    const double SearchEpsilon = 1e-2;

    const double MinTauValue = 0.5;  // V
    const double MaxTauValue = 2e5;  // V

    const double DefaultMockTau = 100;  // us
    const double DefaultMockV0 = 2.5;  // V
    const double DefaultMockVInf = 5.0;  // V
    const double DefaultMockNoise = 1.0;  // V

    const double WaveGoingDownThreshold = -0.05;  // V

    // 新增的快速下降检测百分比阈值
    // 如果后续RapidDeclineCheckPoints个点中，百分之的RapidDeclineCheckPoints个点的波形下降速度超过RapidDeclinePercentage，
    // 则认为该波形快速下降，截取到当前点
    const double RapidDeclinePercentage = 0.1;  // 10% of (maximum - minimum)
    const double RapidDeclineThreshold = 0.9;   // 90% of points must meet the decline condition
    const double RapidDeclineCheckPointsPercentage = 0.05;    // 检查占总点数 * RapidDeclineCheckPointsPercentage个点的数量
}

#endif
