import importlib.util
import math
import sys
from pathlib import Path

script=Path(sys.argv[1])
spec=importlib.util.spec_from_file_location('svg_compiler',script)
compiler=importlib.util.module_from_spec(spec)
spec.loader.exec_module(compiler)

assert compiler.path_commands('M6 26 26 6M16 6h10v10') == [
    ('M',[6.,26.]),('L',[26.,6.]),('M',[16.,6.]),('L',[26.,6.]),('L',[26.,16.])]
assert compiler.path_commands('m21 6-10 10 10 10') == [
    ('M',[21.,6.]),('L',[11.,16.]),('L',[21.,26.])]
assert compiler.matrix('translate(32 0) scale(-1 1)') == [-1.,0.,0.,1.,32.,0.]
arc=compiler.path_commands('M8 13.5v5l-5-1.7a.85.85 0 0 1 0-1.6Z')[-2]
assert arc[0]=='A' and all(math.isclose(a,b,abs_tol=1e-9) for a,b in zip(arc[1],[.85,.85,0.,0.,1.,3.,15.2]))
assert compiler.color('#abc') == 0xaabbccff
try:
    compiler.path_commands('M0 0X1 2')
except ValueError:
    pass
else:
    raise AssertionError('unknown SVG commands must not silently alter icons')
print('PASS: SVG relative/implicit commands, arcs, reflection, colors and unsupported input')
