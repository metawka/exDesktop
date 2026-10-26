#!/usr/bin/env python3
import sys, re
path = 'tdesktop/Telegram/ThirdParty/tgcalls/tgcalls/CryptoHelper.h'
with open(path, 'r') as f:
    content = f.read()

# Move #include <cstdint> and <cstring> outside extern "C" block
# Find extern "C" { ... } block and extract those includes
lines = content.split('\n')
new_lines = []
moved_includes = []
in_extern = False
extern_depth = 0

for line in lines:
    if 'extern "C" {' in line:
        in_extern = True
        extern_depth = 1
    elif in_extern:
        if '{' in line:
            extern_depth += line.count('{')
        if '}' in line:
            extern_depth -= line.count('}')
            if extern_depth <= 0:
                in_extern = False
        if line.strip().startswith('#include <cstdint>') or line.strip().startswith('#include <cstring>'):
            moved_includes.append(line)
            continue
    new_lines.append(line)

# Insert moved includes before first extern "C" {
result = []
for line in new_lines:
    if 'extern "C" {' in line:
        for m in moved_includes:
            result.append(m)
        result.append('')
    result.append(line)

with open(path, 'w') as f:
    f.write('\n'.join(result))
print('Patched CryptoHelper.h')
