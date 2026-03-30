def convert_glsl_to_cstr(glsl_code: str) -> str:
  s = glsl_code.split('\n')
  s = list(filter(lambda x: len(x.strip()) > 0, s))
  s = list(map(lambda x: x.replace('"', '\\"'), s))
  s = list(map(lambda x: f'"{x}\\n"', s))
  return '\n'.join(s)

if __name__ == '__main__':
  import sys
  if len(sys.argv) != 2:
    print('Usage: glsl2cstr.py <glsl_file>')
    sys.exit(1)
  
  with open(sys.argv[1], 'r') as f:
    glsl_code = f.read()
  
  cstr = convert_glsl_to_cstr(glsl_code)
  print(cstr)