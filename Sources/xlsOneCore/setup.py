from setuptools import setup, find_packages

setup(
    name="xlsOneCore",
    version="0.1.0",
    packages=find_packages(),
    python_requires=">=3.11",
    install_requires=[
        "openpyxl>=3.0.0",
    ],
)
