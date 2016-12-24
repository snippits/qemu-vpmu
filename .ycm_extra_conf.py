# Generated by YCM Generator at 2016-12-02 22:40:32.092609

# This file is NOT licensed under the GPLv3, which is the license for the rest
# of YouCompleteMe.
#
# Here's the license text for this file:
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# For more information, please refer to <http://unlicense.org/>

import os
import ycm_core

flags = [
    '-x',
    'c++',
    '-std=c++11',
    '-DVPMU_CONFIG',
    '-DVPMU_CONFIG_SET',
    '-DVPMU_CONFIG_DEBUG',
    '-I.',
    '-I./audio',
    '-I./backends',
    '-I./block',
    '-I./crypto',
    '-I./disas',
    '-I./fsdev',
    '-I./hw/9pfs',
    '-I./hw/acpi',
    '-I./hw/audio',
    '-I./hw/block',
    '-I./hw/bt',
    '-I./hw/char',
    '-I./hw/core',
    '-I./hw/display',
    '-I./hw/dma',
    '-I./hw/gpio',
    '-I./hw/i2c',
    '-I./hw/ide',
    '-I./hw/input',
    '-I./hw/intc',
    '-I./hw/ipack',
    '-I./hw/ipmi',
    '-I./hw/isa',
    '-I./hw/mem',
    '-I./hw/misc',
    '-I./hw/misc/macio',
    '-I./hw/net',
    '-I./hw/net/rocker',
    '-I./hw/nvram',
    '-I./hw/pci',
    '-I./hw/pci-bridge',
    '-I./hw/pci-host',
    '-I./hw/pcmcia',
    '-I./hw/scsi',
    '-I./hw/sd',
    '-I./hw/smbios',
    '-I./hw/ssi',
    '-I./hw/timer',
    '-I./hw/tpm',
    '-I./hw/usb',
    '-I./hw/virtio',
    '-I./hw/watchdog',
    '-I./io',
    '-I./linux-headers',
    '-I./migration',
    '-I./nbd',
    '-I./net',
    '-I./qom',
    '-I./replay',
    '-I./slirp',
    '-I./ui',
    '-I./fpu',
    '-I./hw/9pfs',
    '-I./hw/adc',
    '-I./hw/alpha',
    '-I./hw/arm',
    '-I./hw/block',
    '-I./hw/block/dataplane',
    '-I./hw/char',
    '-I./hw/core',
    '-I./hw/cpu',
    '-I./hw/cris',
    '-I./hw/display',
    '-I./hw/dma',
    '-I./hw/gpio',
    '-I./hw/i2c',
    '-I./hw/i386',
    '-I./hw/i386/kvm',
    '-I./hw/input',
    '-I./hw/intc',
    '-I./hw/isa',
    '-I./hw/lm32',
    '-I./hw/m68k',
    '-I./hw/microblaze',
    '-I./hw/mips',
    '-I./hw/misc',
    '-I./hw/moxie',
    '-I./hw/net',
    '-I./hw/net/fsl_etsec',
    '-I./hw/net/rocker',
    '-I./hw/nvram',
    '-I./hw/openrisc',
    '-I./hw/pcmcia',
    '-I./hw/ppc',
    '-I./hw/s390x',
    '-I./hw/scsi',
    '-I./hw/sd',
    '-I./hw/sh4',
    '-I./hw/sparc',
    '-I./hw/sparc64',
    '-I./hw/ssi',
    '-I./hw/timer',
    '-I./hw/tricore',
    '-I./hw/unicore32',
    '-I./hw/usb',
    '-I./hw/vfio',
    '-I./hw/virtio',
    '-I./hw/xtensa',
    '-I./include',
    '-I./libdecnumber',
    '-I./libdecnumber/dpd',
    '-I./linux-headers',
    '-I./linux-user',
    '-I./linux-user/aarch64',
    '-I./linux-user/alpha',
    '-I./linux-user/arm',
    '-I./linux-user/arm/nwfpe',
    '-I./linux-user/cris',
    '-I./linux-user/host/x86_64',
    '-I./linux-user/i386',
    '-I./linux-user/m68k',
    '-I./linux-user/microblaze',
    '-I./linux-user/mips',
    '-I./linux-user/mips64',
    '-I./linux-user/openrisc',
    '-I./linux-user/ppc',
    '-I./linux-user/s390x',
    '-I./linux-user/sh4',
    '-I./linux-user/sparc',
    '-I./linux-user/sparc64',
    '-I./linux-user/tilegx',
    '-I./linux-user/x86_64',
    '-I./migration',
    '-I./s390x-linux-user',
    '-I./s390x-softmmu',
    '-I./target-alpha',
    '-I./target-arm',
    '-I./target-cris',
    '-I./target-i386',
    '-I./target-lm32',
    '-I./target-m68k',
    '-I./target-microblaze',
    '-I./target-mips',
    '-I./target-moxie',
    '-I./target-openrisc',
    '-I./target-ppc',
    '-I./target-s390x',
    '-I./target-sh4',
    '-I./target-sparc',
    '-I./target-tilegx',
    '-I./target-tricore',
    '-I./target-unicore32',
    '-I./target-xtensa',
    '-I./tcg',
    '-I./tcg/i386',
    '-I./tests',
    '-I./trace',
    '-I./vpmu',
    '-I./vpmu/include',
    '-I./vpmu/libs',
    '-I./vpmu/libs/d4-7',
    '-I./vpmu/simulators',
    '-I./vpmu/stream_impl',
    '-I/usr/include/SDL',
    '-I/usr/include/atk-1.0',
    '-I/usr/include/cacard',
    '-I/usr/include/cairo',
    '-I/usr/include/freetype2',
    '-I/usr/include/gdk-pixbuf-2.0',
    '-I/usr/include/glib-2.0',
    '-I/usr/include/gtk-2.0',
    '-I/usr/include/harfbuzz',
    '-I/usr/include/libdrm',
    '-I/usr/include/libpng16',
    '-I/usr/include/libusb-1.0',
    '-I/usr/include/nspr',
    '-I/usr/include/nss',
    '-I/usr/include/p11-kit-1',
    '-I/usr/include/pango-1.0',
    '-I/usr/include/pixman-1',
    '-I/usr/include/virgl',
    '-I/usr/lib/glib-2.0/include',
    '-I/usr/lib/gtk-2.0/include',
    '-Iaudio',
    '-Ibackends',
    '-Iblock',
    '-Icontrib/ivshmem-client',
    '-Icontrib/ivshmem-server',
    '-Icrypto',
    '-Idisas',
    '-Ifpu',
    '-Ifsdev',
    '-Ihw/9pfs',
    '-Ihw/acpi',
    '-Ihw/adc',
    '-Ihw/alpha',
    '-Ihw/arm',
    '-Ihw/audio',
    '-Ihw/block',
    '-Ihw/block/dataplane',
    '-Ihw/bt',
    '-Ihw/char',
    '-Ihw/core',
    '-Ihw/cpu',
    '-Ihw/cris',
    '-Ihw/display',
    '-Ihw/dma',
    '-Ihw/gpio',
    '-Ihw/i2c',
    '-Ihw/i386',
    '-Ihw/i386/kvm',
    '-Ihw/ide',
    '-Ihw/input',
    '-Ihw/intc',
    '-Ihw/ipack',
    '-Ihw/ipmi',
    '-Ihw/isa',
    '-Ihw/lm32',
    '-Ihw/m68k',
    '-Ihw/mem',
    '-Ihw/microblaze',
    '-Ihw/mips',
    '-Ihw/misc',
    '-Ihw/misc/macio',
    '-Ihw/moxie',
    '-Ihw/net',
    '-Ihw/net/fsl_etsec',
    '-Ihw/net/rocker',
    '-Ihw/nvram',
    '-Ihw/openrisc',
    '-Ihw/pci',
    '-Ihw/pci-bridge',
    '-Ihw/pci-host',
    '-Ihw/pcmcia',
    '-Ihw/ppc',
    '-Ihw/s390x',
    '-Ihw/scsi',
    '-Ihw/sd',
    '-Ihw/sh4',
    '-Ihw/smbios',
    '-Ihw/sparc',
    '-Ihw/sparc64',
    '-Ihw/ssi',
    '-Ihw/timer',
    '-Ihw/tpm',
    '-Ihw/tricore',
    '-Ihw/unicore32',
    '-Ihw/usb',
    '-Ihw/vfio',
    '-Ihw/virtio',
    '-Ihw/watchdog',
    '-Ihw/xtensa',
    '-Iio',
    '-Ilibdecnumber',
    '-Ilibdecnumber/dpd',
    '-Ilinux-user',
    '-Ilinux-user/arm/nwfpe',
    '-Imigration',
    '-Inbd',
    '-Inet',
    '-Iqapi',
    '-Iqga',
    '-Iqga/qapi-generated',
    '-Iqobject',
    '-Iqom',
    '-Ireplay',
    '-Islirp',
    '-Istubs',
    '-Itarget-alpha',
    '-Itcg',
    '-Itests/qemu-iotests',
    '-Itrace',
    '-Iui',
    '-Iutil',
    '-Wall',
    '-m64',
    '-I', 'qga/qapi-generated',
]


# Set this to the absolute path to the folder (NOT the file!) containing the
# compile_commands.json file to use that instead of 'flags'. See here for
# more details: http://clang.llvm.org/docs/JSONCompilationDatabase.html
#
# You can get CMake to generate this file for you by adding:
#   set( CMAKE_EXPORT_COMPILE_COMMANDS 1 )
# to your CMakeLists.txt file.
#
# Most projects will NOT need to set this to anything; you can just change the
# 'flags' list of compilation flags. Notice that YCM itself uses that approach.
compilation_database_folder = ''

if os.path.exists( compilation_database_folder ):
  database = ycm_core.CompilationDatabase( compilation_database_folder )
else:
  database = None

SOURCE_EXTENSIONS = [ '.C', '.cpp', '.cxx', '.cc', '.c', '.m', '.mm' ]

def DirectoryOfThisScript():
  return os.path.dirname( os.path.abspath( __file__ ) )


def MakeRelativePathsInFlagsAbsolute( flags, working_directory ):
  if not working_directory:
    return list( flags )
  new_flags = []
  make_next_absolute = False
  path_flags = [ '-isystem', '-I', '-iquote', '--sysroot=' ]
  for flag in flags:
    new_flag = flag

    if make_next_absolute:
      make_next_absolute = False
      if not flag.startswith( '/' ):
        new_flag = os.path.join( working_directory, flag )

    for path_flag in path_flags:
      if flag == path_flag:
        make_next_absolute = True
        break

      if flag.startswith( path_flag ):
        path = flag[ len( path_flag ): ]
        new_flag = path_flag + os.path.join( working_directory, path )
        break

    if new_flag:
      new_flags.append( new_flag )
  return new_flags


def IsHeaderFile( filename ):
  extension = os.path.splitext( filename )[ 1 ]
  return extension in [ '.H', '.h', '.hxx', '.hpp', '.hh' ]


def GetCompilationInfoForFile( filename ):
  # The compilation_commands.json file generated by CMake does not have entries
  # for header files. So we do our best by asking the db for flags for a
  # corresponding source file, if any. If one exists, the flags for that file
  # should be good enough.
  if IsHeaderFile( filename ):
    basename = os.path.splitext( filename )[ 0 ]
    for extension in SOURCE_EXTENSIONS:
      replacement_file = basename + extension
      if os.path.exists( replacement_file ):
        compilation_info = database.GetCompilationInfoForFile(
          replacement_file )
        if compilation_info.compiler_flags_:
          return compilation_info
    return None
  return database.GetCompilationInfoForFile( filename )


def FlagsForFile( filename, **kwargs ):
  if database:
    # Bear in mind that compilation_info.compiler_flags_ does NOT return a
    # python list, but a "list-like" StringVec object
    compilation_info = GetCompilationInfoForFile( filename )
    if not compilation_info:
      return None

    final_flags = MakeRelativePathsInFlagsAbsolute(
      compilation_info.compiler_flags_,
      compilation_info.compiler_working_dir_ )

  else:
    relative_to = DirectoryOfThisScript()
    final_flags = MakeRelativePathsInFlagsAbsolute( flags, relative_to )

  return {
    'flags': final_flags,
    'do_cache': True
  }
