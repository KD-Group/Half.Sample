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
        daq_headers/legacy/bdaqctrl.h
        sampler/mock_sampler.hpp
        sampler/controller_owner.hpp
        sampler/mock_sampler.cpp
        sampler/instant_ai.hpp
        sampler/instant_ai.cpp
        sampler/instant_acquisition.hpp
        sampler/instant_acquisition.cpp
        sampler/real_sampler.hpp
        sampler/sampler.hpp
        sampler/sampler_factory.hpp
        sampler/sampler_factory.cpp
)

if (WIN32)
    target_sources(sample PRIVATE sampler/real_sampler.cpp)
endif ()

target_compile_options(sample PRIVATE -fpermissive)
target_link_libraries(sample Eigen3::Eigen)

add_executable(sample_instant_ai_unit_tests
        ../tests/sample_instant_ai/test_main.cpp
        ../tests/sample_instant_ai/test_sampling_config.cpp
        ../tests/sample_instant_ai/test_phase_schedule.cpp
        ../tests/sample_instant_ai/test_reconstruction.cpp
        ../tests/sample_instant_ai/test_progress.cpp
        ../tests/sample_instant_ai/test_controller_owner.cpp
        ../tests/sample_instant_ai/test_instant_acquisition.cpp
        ../tests/sample_instant_ai/test_mock_sampler.cpp
        config/sampling_config.cpp
        commander/base.cpp
        error/error.cpp
        sampler/instant_ai.cpp
        sampler/instant_acquisition.cpp
        sampler/mock_sampler.cpp
)
target_link_libraries(sample_instant_ai_unit_tests Eigen3::Eigen)

set(DAQ_CAPABILITY_COMMON_SOURCES
        daq_capability_test/acquisition_runner.cpp
        daq_capability_test/cli.cpp
        daq_capability_test/instant_ai_polling.cpp
        daq_capability_test/json_result.cpp
        daq_capability_test/main.cpp
        daq_capability_test/matrix.cpp
        daq_capability_test/phase_stitcher.cpp
        daq_capability_test/result_writer.cpp
        daq_capability_test/suite_runner.cpp
)

file(GLOB DAQ_CAPABILITY_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/daq_capability_test/*.hpp")

function(add_daq_capability_target target)
    add_executable(${target}
            ${DAQ_CAPABILITY_COMMON_SOURCES}
            ${DAQ_CAPABILITY_HEADERS}
            ${ARGN}
    )
    target_compile_features(${target} PRIVATE cxx_std_14)
    target_compile_definitions(${target} PRIVATE DAQ_CAPABILITY_STANDALONE)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
endfunction()

add_daq_capability_target(daq_capability_test_mock
        daq_capability_test/fake_daq_adapter.cpp
        daq_capability_test/mock_adapter_factory.cpp
)

add_daq_capability_target(daq_capability_test_legacy
        daq_capability_test/legacy_adapter.cpp
        daq_capability_test/legacy_adapter_factory.cpp
        daq_headers/legacy/bdaqctrl.h
)

add_daq_capability_target(daq_capability_test_xnavi
        daq_capability_test/xnavi_adapter.cpp
        daq_capability_test/xnavi_adapter_factory.cpp
        daq_headers/xnavi/bdaqctrl.h
)

if (WIN32)
    target_link_libraries(daq_capability_test_legacy PRIVATE version)
    target_link_libraries(daq_capability_test_xnavi PRIVATE version)
endif ()
