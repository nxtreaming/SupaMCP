"""Setup script for KMCP Python package."""

from setuptools import setup, find_packages

setup(
    name='kmcp-python',
    version='0.1.0',
    author='SupaMCP Team',
    description='Python bindings for KMCP (Kernel MCP)',
    packages=find_packages(),
    install_requires=[
        'langchain>=0.1.0',
        'langchain-core>=0.1.0',
        'langchain-openai>=0.0.3',
        'pydantic>=2.0.0',
        'aiohttp>=3.8.0',
        'pytest>=7.0.0',
        'pytest-asyncio>=0.23.0'
    ],
    python_requires='>=3.8',
    package_data={
        'kmcp': ['../build/lib/*.dll', '../build/lib/*.so', '../build/lib/*.dylib']
    }
)
