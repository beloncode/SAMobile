#! /bin/python3

import os, argparse, pathlib, subprocess, shutil, glob, platform
import json as voorhees
from typing import Final

# You can change these fields too!
with open('env.json', 'r') as jf:
    env = voorhees.load(jf)
    install_in = env['install_dir']
    build_in = env['build_basedir']
    enb_debug = env['enable_debug']
    apk_origin = env['baseapk_file']
    apktool = env['apktool_program']
    envvar_keypath = env['android_keypath_env']
    envvar_keyalias = env['android_keyalias_env']
    envvar_keypass = env['android_keypass_env']

# Change this to your current input files
USER_PATH: Final[str] = os.path.expanduser('~').replace('\\', '/')

APK_BASE: Final[str] = f'{apk_origin}'

APKTOOL: Final[str] = f'{apktool}'

SIGNER_KSPATH: Final[str] = os.getenv(envvar_keypath).replace('\\', '/')
SIGNER_KSALIAS: Final[str] = os.getenv(envvar_keyalias)
SIGNER_KSPASSWORD: Final[str] = os.getenv(envvar_keypass)

# --- Try build now!

# Don't touch in nothing below, at least if you know exactly what are you doing!
ANDROID_SDK: Final[str] = f'{USER_PATH}/AppData/Local/Android/Sdk' if platform.system() == 'Windows' else f'{USER_PATH}/Android/Sdk'

NDK_PATH: Final[str] = f'{ANDROID_SDK}/ndk/' + os.listdir(f'{ANDROID_SDK}/ndk')[0]
BUILD_TOOLS: Final[str] = f'{ANDROID_SDK}/build-tools/' + os.listdir(f'{ANDROID_SDK}/build-tools')[0]
ANDROID_CMAKE: Final[str] = f'{ANDROID_SDK}/cmake/' + os.listdir(f'{ANDROID_SDK}/cmake')[0]
PLATFORM_TOOLS: Final[str] = f'{ANDROID_SDK}/platform-tools'
CMAKE_BIN: Final[str] = f'{ANDROID_CMAKE}/bin/cmake'
NINJA_BIN: Final[str] = f'{ANDROID_CMAKE}/bin/ninja'

ANDROID_MODEL = 'c++_shared'

ANDROID_TOOLCHAIN: Final[str] = f'{NDK_PATH}/build/cmake/android.toolchain.cmake'

ANDROID_PREBUILT = 'windows-x86_64' if platform.system() == 'Windows' else 'linux-x86_64'
ANDROID_SHARED: Final[
    str] = f'{NDK_PATH}/toolchains/llvm/prebuilt/{ANDROID_PREBUILT}/sysroot/usr/lib/aarch64-linux-android/lib{ANDROID_MODEL}.so'

ANDROID_MIN: Final[int] = 31
ANDROID_MAX: Final[int] = 33

ANDROID_TARGET: Final[str] = f'android-{ANDROID_MAX}'
ANDROID_MICRO_ABI: Final[str] = 'arm64-v8a'

APKSIGNER: Final[str] = f'{BUILD_TOOLS}/' + ('apksigner.bat' if platform.system() == 'Windows' else 'apksigner')
ADB: Final[str] = f'{PLATFORM_TOOLS}/adb'
ZIPALIGN: Final[str] = f'{BUILD_TOOLS}/zipalign'

BUILD_TYPES: Final[tuple] = ('Release', 'Debug')
BUILD_DIR: Final[str] = '{}.{}'.format(build_in, 'dbg' if enb_debug else 'rel')

COOP_VERSION: Final[str] = '0.104'
APK_OUT: Final[str] = f'{install_in}/gtasa-dir'

LIB_BASENAME: Final[str] = 'coop'

OUTPUT_COOP_FILE: Final[str] = f'{install_in}/{LIB_BASENAME} v{COOP_VERSION}.apk'
MALICIOUS_SMALI: Final[str] = 'smali/GTASA.smali'

parser = argparse.ArgumentParser()

parser.add_argument('-b', '--build', action='store_true')
parser.add_argument('-g', '--genapk', action='store_true')
parser.add_argument('-i', '--install', action='store_true')
parser.add_argument('-d', '--devices', action='store_true')
parser.add_argument('-C', '--connect', type=str)
parser.add_argument('-l', '--logcat', action='store_true')
parser.add_argument('-c', '--clean', action='store_true')

args = parser.parse_args()
def build_dir():
    os.mkdir(BUILD_DIR)
    CWD: Final[str] = os.getcwd()
    os.chdir(BUILD_DIR)

    CMAKE_BUILD_OPTIONS: Final[dict] = {
        '-DANDROID_NDK=': NDK_PATH,
        '-DANDROID_ABI=': ANDROID_MICRO_ABI,
        '-DANDROID_PLATFORM=': ANDROID_MIN,
        '-DCMAKE_ANDROID_ARCH_ABI=': ANDROID_MICRO_ABI,
        '-DANDROID_STL=': ANDROID_MODEL,
        '-DCMAKE_SYSTEM_NAME=': 'Android',
        '-DCMAKE_TOOLCHAIN_FILE=': ANDROID_TOOLCHAIN,

        '-DCOOP_OUTRELDIR=': install_in,
        '-DCOOP_SHARED_NAME=': LIB_BASENAME,
        '-DCOOP_VERSION=': COOP_VERSION,

        '-DCMAKE_BUILD_TYPE=': BUILD_TYPES[0] if not enb_debug else BUILD_TYPES[1],
        '-DCMAKE_MAKE_PROGRAM=': NINJA_BIN,
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=': 'On',
        '-G': 'Ninja'
    }

    FULL_CMAKE: Final[list] = ['{}{}'.format(*c) for c in sorted(CMAKE_BUILD_OPTIONS.items())]

    print('CMake options list: ', FULL_CMAKE)
    subprocess.run([CMAKE_BIN] + FULL_CMAKE + ['../..'], shell=False)
    os.chdir(CWD)
def generate_apk():
    if not os.path.exists(APK_OUT):
        subprocess.run(['java', '-jar', APKTOOL, 'd', '--output', APK_OUT, APK_BASE], shell=False)
        shutil.copy(MALICIOUS_SMALI, f'{APK_OUT}/smali/com/rockstargames/gtasa/GTASA.smali')

        # Removing all directories that we don't care inside of (lib) dir
        useless_libdirs = os.listdir(f'{APK_OUT}/lib')
        useless_libdirs.remove(ANDROID_MICRO_ABI)

        for useless in useless_libdirs:
            shutil.rmtree(f'{APK_OUT}/lib/{useless}')

    CWD: Final[str] = os.getcwd()
    os.chdir(BUILD_DIR)
    subprocess.run([NINJA_BIN, 'install'])
    os.chdir(CWD)

    # Copying all needed libraries
    CPP_LIB: Final[str] = f'{APK_OUT}/lib/{ANDROID_MICRO_ABI}/' + ANDROID_SHARED.split('/')[-1]

    if not pathlib.Path(CPP_LIB).is_file():
        shutil.copy(ANDROID_SHARED, CPP_LIB)

    for lib_file in glob.glob(f'{install_in}/lib*'):
        lib_file = lib_file.replace('\\', '/')
        shutil.copy(lib_file, f'{APK_OUT}/lib/{ANDROID_MICRO_ABI}/' + lib_file.split('/')[-1])

    subprocess.run(['java', '-jar', APKTOOL, 'b', '--output', f'{OUTPUT_COOP_FILE}.un', APK_OUT])
    subprocess.run([ZIPALIGN, '-p', '-v', '-f', '4', f'{OUTPUT_COOP_FILE}.un', f'{OUTPUT_COOP_FILE}.aligned'], )
    subprocess.run([
        APKSIGNER, 'sign', '-v',
        f'--min-sdk-version={ANDROID_MIN}',
        f'--max-sdk-version={ANDROID_MAX}',
        f'--ks={SIGNER_KSPATH}',
        f'--ks-key-alias={SIGNER_KSALIAS}',
        f'--ks-pass=pass:{SIGNER_KSPASSWORD}',
        f'--in={OUTPUT_COOP_FILE}.aligned',
        f'--out={OUTPUT_COOP_FILE}'
    ])

    os.remove(f'{OUTPUT_COOP_FILE}.un')
    os.remove(f'{OUTPUT_COOP_FILE}.aligned')
    subprocess.run([APKSIGNER, 'verify', '-v', f'{OUTPUT_COOP_FILE}'])
def list_devices():
    subprocess.run([ADB, 'devices'], shell=False)
def connect_device(dev_device: str):
    subprocess.run([ADB, 'connect', dev_device])
def install_apk():
    subprocess.run([ADB, 'install', '-r', '--streaming', OUTPUT_COOP_FILE])
def logcat():
    try:
        subprocess.run([ADB, 'logcat', 'coop,stargames.gtasa, *:S'])
    except KeyboardInterrupt:
        pass
def clean():
    CWD: Final[str] = os.getcwd()
    os.chdir(BUILD_DIR)
    subprocess.run([NINJA_BIN, 'clean'])
    os.chdir(CWD)

    delete_files = [f'{install_in}/{de}' for de in os.listdir(install_in)]
    delete_files.remove(APK_OUT)

    print(f'Cleaning files: {delete_files}')

    for delete in delete_files:
        os.remove(delete)
        
if args.build and not os.path.exists(BUILD_DIR):
    build_dir()
    
if args.genapk:
    if not os.path.exists(BUILD_DIR):
        build_dir()
    generate_apk()
    
if args.devices:
    list_devices()

if args.connect:
    connect_device(args.connect)
    
if args.install:
    if not pathlib.Path(OUTPUT_COOP_FILE).is_file():
        generate_apk()
    install_apk()

if args.logcat:
    logcat()

if args.clean:
    clean()
