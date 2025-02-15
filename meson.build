project(
	'ball-game',
	['c', 'cpp'],
	default_options: [
		'b_vscrt=static_from_buildtype',
	],
)

subdir('include')

add_project_arguments(c_cpp_args + cpp_args, language: ['cpp'])
add_project_arguments(c_cpp_args + c_args, language: ['c'])

install_directory = host_machine.cpu_family()

subdir('src')

executable(
	'ballgame',
	ballgame,
	install: true,
	install_dir: install_directory,
	override_options: override_options_werror,
	dependencies: [
		lak_dep,
	],
	link_args: [
	],
)
