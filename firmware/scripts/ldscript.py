from os.path import join

from SCons.Script import DefaultEnvironment

env = DefaultEnvironment()

# The tle987x platform builder links without a linker script; attach ours here.
# Absolute path — SCons runs the link from the env build dir, not the project dir.
ldscript = join(env["PROJECT_DIR"], "ld", "tle9879-2qxa40.ld")
env.Append(LINKFLAGS=["-T", ldscript])
