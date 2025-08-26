cmake_minimum_required(VERSION 3.28)
project(sample)

add_subdirectory(3rdparty/eigen-3.4.0)

set(CMAKE_CXX_STANDARD 11)

add_executable(sample sample.cpp
        constant.hpp
        types.hpp
        commander/base.hpp
        commander/base.cpp
        commander/commander.hpp
        commander/commander.cpp
        commander/measure.hpp
        commander/measure.cpp
        config/sampling_config.hpp
        config/sampling_config.cpp
        error/error.hpp
        error/error.cpp
        estimate/estimate.hpp
        estimate/estimate.cpp
        global/global.hpp
        global/global.cpp
        processor/processor.hpp
        processor/processor.cpp
        result/sampling_result.hpp
        sampler/bdaqctrl.h
        sampler/mock_sampler.hpp
        sampler/mock_sampler.cpp
        sampler/real_sampler.hpp
        sampler/sampler.hpp
        sampler/sampler_factory.hpp
        sampler/sampler_factory.cpp
)

target_compile_options(sample PRIVATE -fpermissive)
target_link_libraries(sample Eigen3::Eigen)
