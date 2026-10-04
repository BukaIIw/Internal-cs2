import os, re, shutil, subprocess, sys, concurrent.futures as cf

_msvc_env = None

def msvc_env():
    global _msvc_env
    if _msvc_env is not None:
        return _msvc_env
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'), 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    if not os.path.isfile(vswhere):
        _msvc_env = {}
        return _msvc_env
    inst = subprocess.run([vswhere, '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], capture_output=True, text=True).stdout.strip().splitlines()
    if not inst:
        _msvc_env = {}
        return _msvc_env
    bat = os.path.join(inst[0], 'VC', 'Auxiliary', 'Build', 'vcvars64.bat')
    out = subprocess.run('call "' + bat + '" >nul && set', shell=True, capture_output=True, text=True, errors='ignore').stdout
    env = {}
    for line in out.splitlines():
        k, sep, v = line.partition('=')
        if sep:
            env[k] = v
    _msvc_env = env if 'INCLUDE' in env else {}
    return _msvc_env

def use_msvc():
    return os.name == 'nt' and shutil.which('clang++') is None and bool(msvc_env())

def check_msvc(root, f, extra):
    env = msvc_env()
    cl = shutil.which('cl.exe', path=env.get('Path') or env.get('PATH')) or 'cl.exe'
    cmd = [cl, '/nologo', '/Zs', '/std:c++20', '/EHa', '/utf-8', '/W0', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_CRT_SECURE_NO_WARNINGS', '/DUNICODE', '/D_UNICODE',
           '/I' + os.path.join(root, 'src'), '/I' + os.path.join(root, 'deps', 'imgui'), '/I' + os.path.join(root, 'deps', 'imgui', 'backends'), '/I' + os.path.join(root, 'deps', 'minhook', 'include'), '/I' + root] + extra + [os.path.join(root, f)]
    r = subprocess.run(cmd, capture_output=True, text=True, env=env, errors='ignore', cwd=root)
    text = r.stdout + r.stderr
    errs = [l for l in text.splitlines() if re.search(r'\b(fatal )?error [A-Z]+\d+', l)]
    return f, errs, text

def project_files(root):
    vcx = [f for f in os.listdir(root) if f.endswith('.vcxproj')]
    if not vcx:
        return []
    text = open(os.path.join(root, vcx[0]), encoding='utf-8', errors='ignore').read()
    out = []
    for m in re.finditer(r'ClCompile Include="([^"]+)"', text):
        p = m.group(1).replace('\\', '/')
        if p.startswith('src/'):
            out.append(p)
    return out

def deps_flags():
    d = os.environ.get('CXXCHECK_DEPS', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'deps'))
    if not os.path.isdir(d):
        return []
    return ['-I' + d + '/imgui', '-I' + d + '/imgui/backends', '-I' + d + '/minhook/include', '-I' + d + '/minhook/src', '-I' + d + '/minhook/src/hde']

def check(root, f, extra):
    cmd = ['clang++', '--target=x86_64-w64-windows-gnu', '-fsyntax-only', '-std=c++20', '-fms-extensions', '-include', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'winshim', 'shim.h'),
           '-Wno-everything', '-ferror-limit=50', '-DNOMINMAX', '-DWIN32_LEAN_AND_MEAN',
           '-I' + root + '/deps/imgui', '-I' + root + '/deps/imgui/backends', '-I' + root + '/deps/minhook/include',
           '-I' + root + '/src', '-I' + root, '-I' + os.path.join(os.path.dirname(os.path.abspath(__file__)), 'winshim')] + deps_flags() + extra + [os.path.join(root, f)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    ignore = ('default member initializer for',)
    errs = [l for l in r.stderr.splitlines() if (': error:' in l or 'fatal error' in l) and not any(i in l for i in ignore)]
    return f, errs, r.stderr

def main():
    args = sys.argv[1:]
    root = os.getcwd()
    if '--root' in args:
        i = args.index('--root'); root = os.path.abspath(args[i + 1]); del args[i:i + 2]
    verbose = '--verbose' in args
    args = [a for a in args if a != '--verbose']
    files = args or project_files(root)
    total = 0
    with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        checker = check_msvc if use_msvc() else check
        for f, errs, raw in ex.map(lambda f: checker(root, f, []), files):
            total += len(errs)
            if errs:
                print(f'== {f}: {len(errs)} error(s)')
                print(raw if verbose else '\n'.join(errs[:40]))
    print(f'checked {len(files)} file(s), {total} error(s)')
    sys.exit(1 if total else 0)

main()
