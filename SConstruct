import sys

VariantDir('cpp_build', 'src', duplicate=0)
eigen_path = 'src/3rdparty/eigen-3.4.0'

sample_sources = [
    'cpp_build/commander/base.cpp',
    'cpp_build/commander/commander.cpp',
    'cpp_build/commander/measure.cpp',
    'cpp_build/config/sampling_config.cpp',
    'cpp_build/error/error.cpp',
    'cpp_build/estimate/estimate.cpp',
    'cpp_build/global/global.cpp',
    'cpp_build/processor/processor.cpp',
    'cpp_build/processor/independent_cycle.cpp',
    'cpp_build/sampler/mock_sampler.cpp',
    'cpp_build/sampler/instant_ai.cpp',
    'cpp_build/sampler/instant_acquisition.cpp',
    'cpp_build/sampler/origin_data.cpp',
    'cpp_build/sampler/sampler_factory.cpp',
    'cpp_build/sample.cpp',
]

if sys.platform == 'win32':
    sample_sources.append('cpp_build/sampler/real_sampler.cpp')
    env = Environment(CCFLAGS=['/MD', '/EHsc', '/std:c++17', '-O2'], CPPPATH=[eigen_path])
elif sys.platform == 'darwin':
    env = Environment(CPPFLAGS=['-std=c++11', '-O2'], CPPPATH=[eigen_path])
else:
    env = Environment(CXX='g++-4.9', CPPFLAGS=['-std=c++11', '-pthread', '-O2'],
                      CPPPATH=[eigen_path], LIBS=['pthread'])

sample_program = env.Program('cpp_build/sample.exe', sample_sources)
Alias('sample.exe', sample_program)

sample_instant_ai_test_sources = [
    'tests/sample_instant_ai/test_main.cpp',
    'tests/sample_instant_ai/test_sampling_config.cpp',
    'tests/sample_instant_ai/test_phase_schedule.cpp',
    'tests/sample_instant_ai/test_reconstruction.cpp',
    'tests/sample_instant_ai/test_mock_sampler.cpp',
    'tests/sample_instant_ai/test_progress.cpp',
    'tests/sample_instant_ai/test_controller_owner.cpp',
    'tests/sample_instant_ai/test_instant_acquisition.cpp',
    'tests/sample_instant_ai/test_dump_format.cpp',
    'tests/sample_instant_ai/test_processor_transaction.cpp',
    'tests/sample_instant_ai/test_phase2_emergency_stop_real_sample.cpp',
    'src/config/sampling_config.cpp',
    'src/commander/base.cpp',
    'src/error/error.cpp',
    'src/sampler/instant_ai.cpp',
    'src/sampler/instant_acquisition.cpp',
    'src/sampler/origin_data.cpp',
    'src/sampler/mock_sampler.cpp',
    'src/estimate/estimate.cpp',
    'src/global/global.cpp',
    'src/processor/processor.cpp',
    'src/processor/independent_cycle.cpp',
    'src/commander/measure.cpp',
]
sample_instant_ai_test = env.Program(
    'cpp_build/sample_instant_ai_unit_tests.exe',
    sample_instant_ai_test_sources,
)
Alias('sample_instant_ai_unit_tests.exe', sample_instant_ai_test)

daq_common_sources = [
    'src/daq_capability_test/json_result.cpp',
    'src/daq_capability_test/instant_ai_polling.cpp',
    'src/daq_capability_test/matrix.cpp',
    'src/daq_capability_test/acquisition_runner.cpp',
    'src/daq_capability_test/phase_stitcher.cpp',
    'src/daq_capability_test/result_writer.cpp',
    'src/daq_capability_test/cli.cpp',
    'src/daq_capability_test/suite_runner.cpp',
    'src/daq_capability_test/main.cpp',
]
legacy_sources = [
    'src/daq_capability_test/legacy_adapter.cpp',
    'src/daq_capability_test/legacy_adapter_factory.cpp',
]
xnavi_sources = [
    'src/daq_capability_test/xnavi_adapter.cpp',
    'src/daq_capability_test/xnavi_adapter_factory.cpp',
]
mock_sources = [
    'src/daq_capability_test/fake_daq_adapter.cpp',
    'src/daq_capability_test/mock_adapter_factory.cpp',
]
unit_test_sources = [
    'src/daq_capability_test/fake_daq_adapter.cpp',
    'tests/daq_capability_test/test_main.cpp',
    'tests/daq_capability_test/test_instant_ai_polling.cpp',
    'tests/daq_capability_test/test_matrix.cpp',
    'tests/daq_capability_test/test_results.cpp',
    'tests/daq_capability_test/test_acquisition.cpp',
    'tests/daq_capability_test/test_phase_stitcher.cpp',
    'tests/daq_capability_test/test_writer.cpp',
    'tests/daq_capability_test/test_legacy_adapter.cpp',
    'tests/daq_capability_test/test_xnavi_adapter.cpp',
    'tests/daq_capability_test/test_suite.cpp',
]

daq_env = env.Clone()
if sys.platform == 'win32':
    daq_env['CCFLAGS'] = [flag for flag in daq_env['CCFLAGS'] if flag != '/std:c++17'] + ['/std:c++14']

def objects_for(target_dir, sources, build_env=daq_env, defines=None):
    return [
        build_env.Object(target=target_dir + '/' + source.rsplit('/', 1)[-1].replace('.cpp', '.obj'),
                         source=source, CPPPATH=['src'], CPPDEFINES=defines or [])
        for source in sources
    ]

daq_common_objects = objects_for('cpp_build/daq_capability_common', daq_common_sources,
                                 defines=['DAQ_CAPABILITY_STANDALONE'])
mock_objects = objects_for('cpp_build/daq_capability_mock', mock_sources)
mock_program = daq_env.Program('cpp_build/daq_capability_test_mock.exe', daq_common_objects + mock_objects)
Alias('daq_capability_test_mock.exe', mock_program)

unit_common_sources = [source for source in daq_common_sources if not source.endswith('/main.cpp')]
unit_common_objects = objects_for('cpp_build/daq_capability_unit_common', unit_common_sources)
unit_test_objects = objects_for('cpp_build/daq_capability_unit_tests', unit_test_sources)
unit_program = daq_env.Program('cpp_build/daq_capability_unit_tests.exe', unit_common_objects + unit_test_objects)
Alias('daq_capability_unit_tests.exe', unit_program)

legacy_objects = objects_for('cpp_build/daq_capability_legacy', legacy_sources)
legacy_program = daq_env.Program('cpp_build/daq_capability_test_legacy.exe', daq_common_objects + legacy_objects,
                                 LIBS=['version'])
Alias('daq_capability_test_legacy.exe', legacy_program)

xnavi_objects = objects_for('cpp_build/daq_capability_xnavi', xnavi_sources)
xnavi_program = daq_env.Program('cpp_build/daq_capability_test_xnavi.exe', daq_common_objects + xnavi_objects,
                                LIBS=['version'])
Alias('daq_capability_test_xnavi.exe', xnavi_program)

legacy_smoke_objects = objects_for('cpp_build/legacy_adapter_smoke',
    ['src/daq_capability_test/legacy_adapter.cpp', 'tests/daq_capability_test/legacy_adapter_smoke.cpp'])
daq_env.Program('cpp_build/legacy_adapter_smoke.exe', legacy_smoke_objects, LIBS=['version'])

xnavi_smoke_objects = objects_for('cpp_build/xnavi_adapter_smoke',
    ['src/daq_capability_test/xnavi_adapter.cpp', 'tests/daq_capability_test/xnavi_adapter_smoke.cpp'])
daq_env.Program('cpp_build/xnavi_adapter_smoke.exe', xnavi_smoke_objects, LIBS=['version'])
