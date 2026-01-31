from setuptools import setup, Extension

wily_ext = Extension(
    'wily',
    sources=['wily_module.c'],
    libraries=['msg'], 
    include_dirs=['../../include', '../..'], 
    #library_dirs=['../../libmsg', '../../libframe', '../../libXg', '../../wily']
    library_dirs=['../../libmsg']
)

setup(
    name='wily',
    version='1.0',
    description='Wily Editor RPC Extension',
    ext_modules=[wily_ext],
)

