import sys

VariantDir('cpp_build', 'src', duplicate=0)
eigen_path = 'src/3rdparty/eigen-3.4.0'

if sys.platform == 'win32':
    env = Environment(
        CCFLAGS=["/MD", "/EHsc", "-O2"],
        CPPPATH=[eigen_path]  # 添加Eigen头文件路径
    )
    env.Program('cpp_build/sample.exe', Glob('cpp_build/*/*.cpp') + ['cpp_build/sample.cpp'])
elif sys.platform == 'darwin':
    env = Environment(
        CPPFLAGS=['-std=c++11', "-O2"],
        CPPPATH=[eigen_path]  # 添加Eigen头文件路径
    )
    env.Program('cpp_build/sample.exe', Glob('cpp_build/*/*.cpp') + ['cpp_build/sample.cpp'])
else:
    env = Environment(
        CXX='g++-4.9', 
        CPPFLAGS=['-std=c++11', '-pthread', "-O2"],
        CPPPATH=[eigen_path],  # 添加Eigen头文件路径
        LIBS=['pthread']
    )
    env.Program('cpp_build/sample.exe', Glob('cpp_build/*/*.cpp') + ['cpp_build/sample.cpp'], LIBS=['pthread'])
