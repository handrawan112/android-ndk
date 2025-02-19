#
# Copyright (C) 2015 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
import argparse
import datetime
import multiprocessing
import os
import shutil
import subprocess
import sys
import tempfile
import timeit
import zipfile


THIS_DIR = os.path.realpath(os.path.dirname(__file__))


# TODO: Make the x86 toolchain names just be the triple.
ALL_TOOLCHAINS = (
    'arm-linux-androideabi',
    'aarch64-linux-android',
    'mipsel-linux-android',
    'mips64el-linux-android',
    'x86',
    'x86_64',
)


ALL_TRIPLES = (
    'arm-linux-androideabi',
    'aarch64-linux-android',
    'mipsel-linux-android',
    'mips64el-linux-android',
    'i686-linux-android',
    'x86_64-linux-android',
)


ALL_ARCHITECTURES = (
    'arm',
    'arm64',
    'mips',
    'mips64',
    'x86',
    'x86_64',
)


ALL_ABIS = (
    'armeabi',
    'armeabi-v7a',
    'arm64-v8a',
    'mips',
    'mips64',
    'x86',
    'x86_64',
)


class Timer(object):
    def __init__(self):
        self.start_time = None
        self.end_time = None
        self.duration = None

    def start(self):
        self.start_time = timeit.default_timer()

    def finish(self):
        self.end_time = timeit.default_timer()

        # Not interested in partial seconds at this scale.
        seconds = int(self.end_time - self.start_time)
        self.duration = datetime.timedelta(seconds=seconds)

    def __enter__(self):
        self.start()

    def __exit__(self, _exc_type, _exc_value, _traceback):
        self.finish()


def arch_to_toolchain(arch):
    return dict(zip(ALL_ARCHITECTURES, ALL_TOOLCHAINS))[arch]


def arch_to_triple(arch):
    return dict(zip(ALL_ARCHITECTURES, ALL_TRIPLES))[arch]


def toolchain_to_arch(toolchain):
    return dict(zip(ALL_TOOLCHAINS, ALL_ARCHITECTURES))[toolchain]


def arch_to_abis(arch):
    return {
        'arm': ['armeabi', 'armeabi-v7a'],
        'arm64': ['arm64-v8a'],
        'mips': ['mips'],
        'mips64': ['mips64'],
        'x86': ['x86'],
        'x86_64': ['x86_64'],
    }[arch]


def abi_to_arch(arch):
    return {
        'armeabi': 'arm',
        'armeabi-v7a': 'arm',
        'arm64-v8a': 'arm64',
        'mips': 'mips',
        'mips64': 'mips64',
        'x86': 'x86',
        'x86_64': 'x86_64',
    }[arch]


def android_path(*args):
    top = os.path.realpath(os.path.join(THIS_DIR, '../../..'))
    return os.path.normpath(os.path.join(top, *args))


def sysroot_path(toolchain):
    arch = toolchain_to_arch(toolchain)
    version = default_api_level(arch)

    prebuilt_ndk = 'prebuilts/ndk/current'
    sysroot_subpath = 'platforms/android-{}/arch-{}'.format(version, arch)
    return android_path(prebuilt_ndk, sysroot_subpath)


def ndk_path(*args):
    return android_path('ndk', *args)


def toolchain_path(*args):
    return android_path('toolchain', *args)


def default_api_level(arch):
    if '64' in arch:
        return 21
    else:
        return 9


def jobs_arg():
    return '-j{}'.format(multiprocessing.cpu_count() * 2)


def build(cmd, args, intermediate_package=False):
    package_dir = args.out_dir if intermediate_package else args.dist_dir
    common_args = [
        '--verbose',
        '--package-dir={}'.format(package_dir),
    ]

    build_env = dict(os.environ)
    build_env['NDK_BUILDTOOLS_PATH'] = android_path('ndk/build/tools')
    build_env['ANDROID_NDK_ROOT'] = ndk_path()
    subprocess.check_call(cmd + common_args, env=build_env)


def _get_dir_from_env(default, env_var):
    path = os.path.realpath(os.getenv(env_var, default))
    if not os.path.isdir(path):
        os.makedirs(path)
    return path


def get_out_dir():
    return _get_dir_from_env(android_path('out'), 'OUT_DIR')


def get_dist_dir(out_dir):
    return _get_dir_from_env(os.path.join(out_dir, 'dist'), 'DIST_DIR')


def get_default_host():
    if sys.platform in ('linux', 'linux2'):
        return 'linux'
    elif sys.platform == 'darwin':
        return 'darwin'
    else:
        raise RuntimeError('Unsupported host: {}'.format(sys.platform))


def host_to_tag(host):
    if host in ['darwin', 'linux']:
        return host + '-x86_64'
    elif host == 'windows':
        return 'windows'
    elif host == 'windows64':
        return 'windows-x86_64'
    else:
        raise RuntimeError('Unsupported host: {}'.format(host))


def make_repo_prop(out_dir):
    file_name = 'repo.prop'

    dist_dir = os.environ.get('DIST_DIR')
    if dist_dir is not None:
        dist_repo_prop = os.path.join(dist_dir, file_name)
        shutil.copy(dist_repo_prop, out_dir)
    else:
        out_file = os.path.join(out_dir, file_name)
        with open(out_file, 'w') as prop_file:
            cmd = [
                'repo', 'forall', '-c',
                'echo $REPO_PROJECT $(git rev-parse HEAD)',
            ]
            subprocess.check_call(cmd, stdout=prop_file)


def make_package(name, directory, out_dir):
    """Pacakges an NDK module for release.

    Makes a zipfile of the single NDK module that can be released in the SDK
    manager. This will handle the details of creating the repo.prop file for
    the package.

    Args:
        name: Name of the final package, excluding extension.
        directory: Directory to be packaged.
        out_dir: Directory to place package.
    """
    if not os.path.isdir(directory):
        raise ValueError('directory must be a directory: ' + directory)

    path = os.path.join(out_dir, name + '.zip')
    if os.path.exists(path):
        os.unlink(path)

    cwd = os.getcwd()
    os.chdir(os.path.dirname(directory))
    basename = os.path.basename(directory)
    try:
        subprocess.check_call(
            ['zip', '-x', '*.pyc', '-x', '*.pyo', '-x', '*.swp',
             '-x', '*.git*', '-0qr', path, basename])
    finally:
        os.chdir(cwd)

    with zipfile.ZipFile(path, 'a', zipfile.ZIP_DEFLATED) as zip_file:
        tmpdir = tempfile.mkdtemp()
        try:
            make_repo_prop(tmpdir)
            arcname = os.path.join(basename, 'repo.prop')
            zip_file.write(os.path.join(tmpdir, 'repo.prop'), arcname)
        finally:
            shutil.rmtree(tmpdir)


def merge_license_files(output_path, files):
    licenses = []
    for license_path in files:
        with open(license_path) as license_file:
            licenses.append(license_file.read())

    with open(output_path, 'w') as output_file:
        output_file.write('\n'.join(licenses))


class ArgParser(argparse.ArgumentParser):
    def __init__(self):
        super(ArgParser, self).__init__()

        self.add_argument(
            '--host', choices=('darwin', 'linux', 'windows', 'windows64'),
            default=get_default_host(),
            help='Build binaries for given OS (e.g. linux).')

        self.add_argument(
            '--out-dir', help='Directory to place temporary build files.',
            type=os.path.realpath, default=get_out_dir())

        # The default for --dist-dir has to be handled after parsing all
        # arguments because the default is derived from --out-dir. This is
        # handled in run().
        self.add_argument(
            '--dist-dir', help='Directory to place the packaged artifact.',
            type=os.path.realpath)


def run(main_func, arg_parser=ArgParser):
    if 'ANDROID_BUILD_TOP' not in os.environ:
        top = os.path.join(os.path.dirname(__file__), '../../..')
        os.environ['ANDROID_BUILD_TOP'] = os.path.realpath(top)

    args = arg_parser().parse_args()

    if args.dist_dir is None:
        args.dist_dir = get_dist_dir(args.out_dir)

    # We want any paths to be relative to the invoked build script.
    main_filename = os.path.realpath(sys.modules['__main__'].__file__)
    os.chdir(os.path.dirname(main_filename))

    main_func(args)
