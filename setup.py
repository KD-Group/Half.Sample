import setuptools

setuptools.setup(
    name="half-sample",
    version="0.1.11",
    author="kunde",
    author_email="gzkunde@163.com",
    description="sample data and analysis",
    long_description="",
    url="https://github.com/KD-Group/Half.Sample",
    packages=["sample"],
    classifiers=(
        "Programming Language :: Python :: 3",
        "License :: OSI Approved :: MIT License",
        "Operating System :: OS Independent",
    ),
    data_files=[('cpp_build/sample.exe')]
)
