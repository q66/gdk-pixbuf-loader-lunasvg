project(
    'gdk-pixbuf-loader-lunasvg',
    ['cpp'],
    default_options: [
        'cpp_std=c++17', 'warning_level=3',
        'buildtype=debugoptimized', 'cpp_rtti=false',
    ],
    license: 'MIT'
)

lunasvg = dependency('lunasvg', required: false)

if not lunasvg.found()
    # use subproject, this will make a static build
    cmake = import('cmake')

    copts = cmake.subproject_options()
    # more lenient warning level
    copts.set_override_option('warning_level', '1')
    # ensure we can use the static lib
    copts.append_compile_args('cpp', '-fPIC')
    copts.append_compile_args('c', '-fPIC')
    # use the options
    lunasvg_proj = cmake.subproject('lunasvg', options: copts)
    lunasvg = lunasvg_proj.dependency('lunasvg')
endif

gdk_pixbuf = dependency('gdk-pixbuf-2.0')
gio = dependency('gio-2.0')

shared_module(
    'libpixbufloader-svg', 'libpixbufloader-svg.cc',
    install: true,
    install_dir: gdk_pixbuf.get_pkgconfig_variable('gdk_pixbuf_moduledir'),
    name_prefix: '',
    dependencies: [lunasvg, gdk_pixbuf, gio],
    gnu_symbol_visibility: 'hidden',
)
