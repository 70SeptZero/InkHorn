"""Compile the project's SVG icons to Direct2D path data; no raster conversion/dependency.

Supports the SVG elements and path commands used by the supplied artwork. Unsupported
constructs fail explicitly at build time rather than silently changing the artwork.
"""
import math
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

FILES = ['select', 'pen', 'pen', 'eraser', 'highlighter', 'rounded-rectangle',
         'undo', 'redo', 'save', 'circle', 'line', 'arrow', 'chevron-left', 'eraser-cursor']
TOKEN = re.compile(r'[a-zA-Z]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?')

def path_commands(text):
    words = TOKEN.findall(text)
    result, i, command, x, y, sx, sy = [], 0, '', 0., 0., 0., 0.
    counts = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'Q': 4, 'A': 7}
    while i < len(words):
        if words[i].isalpha():
            command = words[i]
            i += 1
        upper, relative = command.upper(), command.islower()
        if upper == 'Z':
            result.append(('Z', []))
            x, y, command = sx, sy, ''
            continue
        if upper not in counts:
            raise ValueError(f'Unsupported SVG path command {command!r}')
        n = counts[upper]
        values = list(map(float, words[i:i+n]))
        if len(values) != n or not all(map(math.isfinite, values)):
            raise ValueError('Invalid SVG coordinates')
        i += n
        ox, oy = (x, y) if relative else (0., 0.)
        if upper in ('M', 'L'):
            x, y = values[0]+ox, values[1]+oy
            result.append((upper, [x,y]))
            if upper == 'M':
                sx, sy = x, y
                command = 'l' if relative else 'L'
        elif upper == 'H':
            x = values[0]+ox
            result.append(('L', [x,y]))
        elif upper == 'V':
            y = values[0]+oy
            result.append(('L', [x,y]))
        elif upper == 'C':
            values = [v+(ox if j % 2 == 0 else oy) for j,v in enumerate(values)]
            x, y = values[-2:]
            result.append(('C', values))
        elif upper == 'Q':
            qx, qy, ex, ey = values[0]+ox, values[1]+oy, values[2]+ox, values[3]+oy
            result.append(('C', [x+(qx-x)*2/3,y+(qy-y)*2/3,
                                 ex+(qx-ex)*2/3,ey+(qy-ey)*2/3,ex,ey]))
            x, y = ex, ey
        elif upper == 'A':
            values[5] += ox
            values[6] += oy
            x, y = values[-2:]
            result.append(('A', values))
    return result

def color(value):
    if value == 'none': return 0
    if value.startswith('#'):
        value = value[1:]
        if len(value) == 3: value = ''.join(c*2 for c in value)
        if len(value) == 6: return (int(value,16) << 8) | 255
    raise ValueError(f'Unsupported SVG color {value}')

def matrix(text):
    a,b,c,d,e,f = 1.,0.,0.,1.,0.,0.
    for name,args in re.findall(r'(\w+)\s*\(([^)]*)\)',text):
        v = [float(s) for s in TOKEN.findall(args)]
        if name == 'translate': q = (1,0,0,1,v[0],v[1] if len(v)>1 else 0)
        elif name == 'scale': q = (v[0],0,0,v[1] if len(v)>1 else v[0],0,0)
        elif name == 'matrix' and len(v) == 6: q = v
        else: raise ValueError(f'Unsupported SVG transform {name}')
        aa,bb,cc,dd,ee,ff = q
        a,b,c,d,e,f = (a*aa+c*bb,b*aa+d*bb,a*cc+c*dd,b*cc+d*dd,
                       a*ee+c*ff+e,b*ee+d*ff+f)
    return [a,b,c,d,e,f]

def number(n):
    text = f'{n:.9g}'
    if '.' not in text and 'e' not in text: text += '.0'
    return text+'f'

def compile_icons(root, output):
    out = ['// Generated from assets/icons/*.svg. Do not edit.', '#pragma once',
           '#include "svg_types.hpp"', 'namespace pb {']
    assets = []
    for index,name in enumerate(FILES):
        source = root / f'{name}.svg'
        if not source.exists():
            assets.append('{nullptr,0,{0,0,32,32}}')
            continue
        tree = ET.parse(source).getroot()
        unknown_root=set(tree.attrib)-{'width','height','viewBox','fill','version'}
        if unknown_root:
            raise ValueError(f'Unsupported SVG root attributes {unknown_root} in {source}')
        view = list(map(float, tree.get('viewBox','0 0 32 32').split()))
        if len(view) != 4 or view[2] <= 0 or view[3] <= 0:
            raise ValueError(f'Invalid viewBox in {source}')
        elements = []
        for node in tree:
            tag = node.tag.split('}')[-1]
            if tag in ('title','desc','metadata'): continue
            supported={'id','d','fill','stroke','stroke-width','stroke-linecap','stroke-linejoin',
                       'transform','x','y','width','height','rx','cx','cy','r'}
            if set(node.attrib)-supported:
                raise ValueError(f'Unsupported SVG attributes in {source}: {set(node.attrib)-supported}')
            if tag == 'path': commands = path_commands(node.attrib['d'])
            elif tag == 'rect':
                x,y,w,h = [float(node.get(k,'0')) for k in ('x','y','width','height')]
                r = min(float(node.get('rx','0')),w/2,h/2)
                commands = path_commands(f'M{x+r} {y}H{x+w-r}a{r} {r} 0 0 1 {r} {r}'
                    f'V{y+h-r}a{r} {r} 0 0 1 {-r} {r}H{x+r}a{r} {r} 0 0 1 {-r} {-r}'
                    f'V{y+r}a{r} {r} 0 0 1 {r} {-r}Z') if r else path_commands(f'M{x} {y}h{w}v{h}h{-w}Z')
            elif tag == 'circle':
                x,y,r = [float(node.get(k,'0')) for k in ('cx','cy','r')]
                commands = path_commands(f'M{x-r} {y}a{r} {r} 0 1 1 {2*r} 0a{r} {r} 0 1 1 {-2*r} 0Z')
            else: raise ValueError(f'Unsupported SVG element {tag} in {source}')
            symbol = f'svg_{index}_{len(elements)}'
            out.append(f'inline constexpr SvgCommand {symbol}[] = {{')
            for op,values in commands:
                out.append("    {'"+op+"',{"+','.join(number(n) for n in values)+"}},")
            out.append('};')
            fill = color(node.get('fill',tree.get('fill','#000000')))
            stroke = color(node.get('stroke',tree.get('stroke','none')))
            width = float(node.get('stroke-width',tree.get('stroke-width','1')))
            cap = {'butt':0,'round':1,'square':2}[node.get('stroke-linecap','butt')]
            join = {'miter':0,'round':1,'bevel':2}[node.get('stroke-linejoin','miter')]
            tint = node.get('id','') in ('pen-body','pen-tip','highlighter-body','highlighter-tip')
            transform = ','.join(number(v) for v in matrix(node.get('transform','')))
            elements.append(f'{{{symbol},std::size({symbol}),0x{fill:08x},0x{stroke:08x},'
                            f'{number(width)},{cap},{join},{str(tint).lower()},{{{transform}}}}}')
        out.append(f'inline constexpr SvgElement svg_elements_{index}[] = {{'+','.join(elements)+'};')
        assets.append(f'{{svg_elements_{index},std::size(svg_elements_{index}),{{'+','.join(number(v) for v in view)+'}}')
    out.append('inline constexpr SvgAsset svgAssets[] = {'+','.join(assets)+'};')
    out.append('} // namespace pb\n')
    output.parent.mkdir(parents=True,exist_ok=True)
    content='\n'.join(out)
    if not output.exists() or output.read_text(encoding='utf-8') != content:
        output.write_text(content,encoding='utf-8')

if __name__ == '__main__':
    compile_icons(Path(sys.argv[1]),Path(sys.argv[2]))
