project('kbootd', 'c',
        version : run_command('git', 'rev-parse', '--short', 'HEAD', check: true)
                  .stdout().strip(),
        default_options: ['buildtype=release', 'werror=true', 'strip=true'])

includes = ['src']

sources = ['src/boot.c',
           'src/fastboot.c',
           'src/fb_command.c',
           'src/main.c',
           'src/part.c',
           'src/utils.c']

add_global_arguments('-DREVISION="@0@"'.format(meson.project_version()), language: 'c')

executable('kbootd', sources, include_directories: includes,
           link_args: ['-static'], install: true)
