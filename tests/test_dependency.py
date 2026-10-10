"""Check LVGL target reuse, offline sources, failure paths and optional real builds."""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

COMPONENT = Path(__file__).resolve().parents[1]
PIN = 'c033a98afddd65aaafeebea625382a94020fe4a7'  # LVGL v9.3.0


def run(command, log, error=None):
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, encoding='utf-8', errors='replace')
    log.write_text(result.stdout, encoding='utf-8')
    if error is not None:
        if result.returncode == 0 or error not in result.stdout:
            raise RuntimeError(f'Expected {error!r}: see {log}')
    elif result.returncode:
        raise RuntimeError(f'Command failed: see {log}\n{result.stdout[-3000:]}')
    return result.stdout


def cmake_path(path):
    return Path(path).resolve().as_posix()


def check(args, root):
    # 隔离同级依赖，默认测试禁止网络；假 LVGL target 只验证 CMake 解析。
    component = root / 'component'
    shutil.copytree(COMPONENT, component, ignore=shutil.ignore_patterns('.git', 'build', '__pycache__'))
    fake = root / 'fake-lvgl'
    fake.mkdir()
    (fake / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.22)\nproject(fake_lvgl C)\n'
        'add_library(lvgl INTERFACE)\n'
        f'target_include_directories(lvgl INTERFACE "{cmake_path(COMPONENT / "tests")}")\n', encoding='utf-8')
    no_target = root / 'no-target'
    no_target.mkdir()
    (no_target / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.22)\n', encoding='utf-8')
    common = root / 'common'
    common.mkdir()
    # 使用调用方实际选择的公共错误码头文件，不复制一份测试错误码。
    shutil.copyfile(Path(args.common_source) / 'stm_err.h', common / 'stm_err.h')

    framework = Path(args.lcd_source).resolve()
    if not (framework / 'CMakeLists.txt').is_file():
        raise RuntimeError(f'Missing stm_lcd source: {framework}')

    def case(name, prelude='', flags=(), error=None, real=False, fetched=False):
        source = root / name
        source.mkdir()
        binary = source / 'out'
        cmake = ('cmake_minimum_required(VERSION 3.22)\n'
                 'project(lvgl_dependency_test C CXX)\n'
                 'add_library(stm_common INTERFACE)\n'
                 f'target_include_directories(stm_common INTERFACE "{cmake_path(common)}")\n'
                 + f'add_subdirectory("{cmake_path(framework)}" stm_lcd)\n'
                 + prelude + f'\nadd_subdirectory("{cmake_path(component)}" component)\n')
        if not error:
            if real:
                # 真实 LVGL 路径检查 C/C++ 头文件、链接及实例创建/销毁。
                for filename in ['headers.c', 'headers.cpp']:
                    shutil.copyfile(COMPONENT / 'tests' / filename, source / filename)
                (source / 'main.c').write_text('#include "stm_lvgl_port.h"\n#include "stm_lcd_impl.h"\nstatic stm_err_t draw(stm_lcd_panel_handle_t p, uint16_t x1, uint16_t y1,\n                     uint16_t x2, uint16_t y2, const void *pixels)\n{\n    (void)p; (void)x1; (void)y1; (void)x2; (void)y2; (void)pixels;\n    return STM_OK;\n}\nstatic uint32_t clock_ms(void)\n{\n    return 100;\n}\nstatic const stm_lcd_io_ops_t io_ops = {0};\nstatic void destroy(stm_lcd_panel_handle_t p)\n{\n    (void)p;\n}\nstatic const stm_lcd_panel_ops_t panel_ops = {.draw = draw, .destroy = destroy};\n_Alignas(64) static uint16_t pixels[16 * 4];\nint main(void)\n{\n    struct stm_lcd_io io = {0};\n    struct stm_lcd_panel panel = {0};\n    if (stm_lcd_io_init(&io, &io_ops, NULL) != STM_OK) return 1;\n    if (stm_lcd_panel_base_init(&panel, &panel_ops, &io, 16, 16) != STM_OK) return 2;\n    lvgl_port_handle_t port = NULL;\n    lvgl_port_config_t cfg = {.io = &io, .panel = &panel, .width = 16, .height = 16,\n                             .draw_buffer = pixels, .draw_buffer_bytes = sizeof(pixels),\n                             .clock_ms = clock_ms};\n    if (lvgl_port_create(&cfg, &port) != STM_OK || !port) return 3;\n    lv_display_t *display = NULL;\n    if (lvgl_port_get_display(port, &display) != STM_OK || !display) return 4;\n    if (lvgl_port_process(port, 100) != STM_OK) return 5;\n    if (lvgl_port_delete(&port) != STM_OK || port) return 6;\n    stm_lcd_panel_handle_t handle = &panel;\n    if (stm_lcd_panel_delete(&handle) != STM_OK || handle) return 7;\n    if (stm_lcd_io_deinit(&io) != STM_OK) return 8;\n    return 0;\n}\n', encoding='utf-8')
                (source / 'lv_conf.h').write_text(
                    '#ifndef LV_CONF_H\n#define LV_CONF_H\n#define LV_COLOR_DEPTH 16\n'
                    '#define LV_USE_THORVG_INTERNAL 0\n#endif\n', encoding='utf-8')
                cmake += ('add_library(headers OBJECT headers.c headers.cpp)\n'
                          'target_compile_features(headers PRIVATE c_std_11 cxx_std_17)\n'
                          'target_link_libraries(headers PRIVATE stm_lvgl_port)\n'
                          'target_compile_options(headers PRIVATE -Wall -Wextra -Werror)\n'
                          'target_compile_options(stm_lvgl_port PRIVATE -Wall -Wextra -Werror)\n'
                          'add_executable(consumer main.c)\n'
                          'target_link_libraries(consumer PRIVATE stm_lvgl_port)\n'
                          'enable_testing()\nadd_test(NAME consumer COMMAND consumer)\n')
                flags = [*flags, f'-DLV_BUILD_CONF_PATH={cmake_path(source / "lv_conf.h")}']
            cmake += ('get_target_property(lvgl_source lvgl SOURCE_DIR)\n'
                      'file(WRITE "${CMAKE_BINARY_DIR}/lvgl-source.txt" "${lvgl_source}")\n')
            if name in ['offline', 'override', 'precedence'] or real:
                cmake += ('foreach(setting CONFIG_LV_BUILD_DEMOS CONFIG_LV_BUILD_EXAMPLES CONFIG_LV_USE_THORVG_INTERNAL)\n'
                          '  if(${setting})\n    message(FATAL_ERROR "Unexpected ${setting}")\n  endif()\n'
                          'endforeach()\n')
            if name in ['target', 'alias', 'preserve_options', 'preserve_normal_options']:
                cmake += ('foreach(setting CONFIG_LV_BUILD_DEMOS CONFIG_LV_BUILD_EXAMPLES CONFIG_LV_USE_THORVG_INTERNAL)\n'
                          '  if(NOT ${setting})\n    message(FATAL_ERROR "Changed ${setting}")\n  endif()\n'
                          'endforeach()\n')
            if name == 'override':
                cmake += ('FetchContent_GetProperties(lvgl)\n'
                          'if(NOT lvgl_POPULATED)\nmessage(FATAL_ERROR "Not populated")\nendif()\n'
                          'get_property(details GLOBAL PROPERTY _FetchContent_lvgl_savedDetails)\n'
                          f'if(NOT "${{details}}" MATCHES "{PIN}")\nmessage(FATAL_ERROR "Wrong pinned commit")\nendif()\n'
                          'if(NOT "${details}" MATCHES "missing-repository")\nmessage(FATAL_ERROR "Mirror ignored")\nendif()\n')
        (source / 'CMakeLists.txt').write_text(cmake, encoding='utf-8')
        run(['cmake', '-S', str(source), '-B', str(binary), '-G', 'Ninja',
             f'-DCMAKE_C_COMPILER={args.c_compiler}', f'-DCMAKE_CXX_COMPILER={args.cxx_compiler}',
             '-DCMAKE_BUILD_TYPE=Debug', '-DSTM_COMMON_FETCH=OFF',
             f'-DSTM_LVGL_PORT_LVGL_GIT_REPOSITORY={cmake_path(root / "missing-repository")}',
             *flags], source / 'configure.log', error)
        if not error:
            actual = Path((binary / 'lvgl-source.txt').read_text()).resolve()
            if name in ['offline', 'override', 'precedence', 'preserve_options', 'preserve_normal_options']:
                assert actual == fake.resolve(), (name, actual)
            if fetched:
                revision = subprocess.check_output(['git', '-C', str(actual), 'rev-parse', 'HEAD'], text=True).strip()
                assert revision == PIN, revision
            run(['cmake', '--build', str(binary)], source / 'build.log')
            if real:
                run(['ctest', '--test-dir', str(binary), '--output-on-failure'], source / 'test.log')
        print(f'PASS: {name}', flush=True)

    options_on = [f'-D{name}=ON' for name in ['CONFIG_LV_BUILD_DEMOS', 'CONFIG_LV_BUILD_EXAMPLES', 'CONFIG_LV_USE_THORVG_INTERNAL']]
    no_fetch = ['-DSTM_LVGL_PORT_FETCH_LVGL=OFF']
    for mode in ['target', 'alias']:
        target = 'lvgl' if mode == 'target' else 'application_lvgl'
        prelude = (f'add_library({target} INTERFACE)\n'
                   f'target_include_directories({target} INTERFACE "{cmake_path(COMPONENT / "tests")}")\n')
        if mode == 'alias':
            prelude += 'add_library(lvgl ALIAS application_lvgl)\n'
        case(mode, prelude, [*no_fetch, *options_on,
                            '-DSTM_LVGL_PORT_LVGL_SOURCE_DIR=missing-source',
                            '-DFETCHCONTENT_SOURCE_DIR_LVGL=missing-override'])
    offline = f'-DSTM_LVGL_PORT_LVGL_SOURCE_DIR={cmake_path(fake)}'
    override = f'-DFETCHCONTENT_SOURCE_DIR_LVGL={cmake_path(fake)}'
    case('offline', flags=[*no_fetch, offline])
    case('override', flags=[*no_fetch, override])
    case('precedence', flags=[*no_fetch, offline, '-DFETCHCONTENT_SOURCE_DIR_LVGL=missing-override'])
    case('preserve_options', flags=[*no_fetch, offline, *options_on])
    normal_options = ''.join(f'set({name} ON)\n' for name in [
        'CONFIG_LV_BUILD_DEMOS', 'CONFIG_LV_BUILD_EXAMPLES', 'CONFIG_LV_USE_THORVG_INTERNAL'])
    case('preserve_normal_options', prelude=normal_options, flags=[*no_fetch, offline])
    case('disabled_missing', flags=no_fetch, error='LVGL is missing')
    case('invalid_source', flags=[*no_fetch, '-DSTM_LVGL_PORT_LVGL_SOURCE_DIR=missing-source'],
         error='Invalid STM_LVGL_PORT_LVGL_SOURCE_DIR')
    case('invalid_override', flags=[*no_fetch, '-DFETCHCONTENT_SOURCE_DIR_LVGL=missing-override'],
         error='FETCHCONTENT_SOURCE_DIR_LVGL -->')
    case('missing_target', flags=[*no_fetch, f'-DSTM_LVGL_PORT_LVGL_SOURCE_DIR={cmake_path(no_target)}'],
         error='LVGL dependency did not provide the lvgl target')
    if args.lvgl_source:
        lvgl = Path(args.lvgl_source).resolve()
        case('real_offline', flags=[*no_fetch, f'-DSTM_LVGL_PORT_LVGL_SOURCE_DIR={cmake_path(lvgl)}'], real=True)
        case('real_override', flags=[*no_fetch, f'-DFETCHCONTENT_SOURCE_DIR_LVGL={cmake_path(lvgl)}'], real=True)
        case('real_missing_config', flags=[*no_fetch, f'-DSTM_LVGL_PORT_LVGL_SOURCE_DIR={cmake_path(lvgl)}'],
             error='Configuration file:')
    if args.fetch:
        case('real_fetch', flags=[f'-DSTM_LVGL_PORT_LVGL_GIT_REPOSITORY={args.repository}'], real=True, fetched=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--common-source', default=str(COMPONENT.parent / 'stm_common'),
                        help='stm_common directory containing stm_err.h')
    parser.add_argument('--lcd-source', default=str(COMPONENT.parent / 'stm_lcd'),
                        help='Generic stm_lcd framework source directory')
    parser.add_argument('--c-compiler', default='gcc')
    parser.add_argument('--cxx-compiler', default='g++')
    parser.add_argument('--lvgl-source', help='Real LVGL 9 source for additional compile/link/run checks')
    parser.add_argument('--fetch', action='store_true', help='Download and verify the fixed LVGL commit (network required)')
    parser.add_argument('--repository', default='https://github.com/lvgl/lvgl.git')
    parser.add_argument('--build-dir', help='New empty directory to retain logs and build outputs')
    args = parser.parse_args()
    if args.build_dir:
        root = Path(args.build_dir).resolve()
        root.mkdir(parents=True, exist_ok=False)
        check(args, root)
    else:
        with tempfile.TemporaryDirectory(prefix='stm-lvgl-dependency-') as temp:
            check(args, Path(temp))


if __name__ == '__main__':
    main()
